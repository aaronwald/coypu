#include <stdio.h>
#include <signal.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <time.h>
#include <openssl/rand.h>
#include <x86intrin.h>

#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <thread>
#include <iomanip>
#include <sstream>

#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h" // support for basic file logging
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/error/en.h"

#include "coypu/coypu.h"
#include "coypu/spdlogger.h"
#include "config/config.h"
#include "util/backtrace.h"
#include "spdlog/spdlog.h"
#include "event/event_hlpr.h"
#include "event/event_mgr.h"
#include "net/tcp.h"
#include "net/ssl/openssl_mgr.h"
#include "http/websocket.h"
#include "mem/mem.h"
#include "file/file.h"
#include "store/store.h"
#include "buf/buf.h"
#include "cache/seqcache.h"
#include "book/level.h"
#include "util/backtrace.h"
#include "admin/admin.h"

#include "proto/coincache.pb.h"

using namespace rapidjson;
// using json = nlohmann::json;

using namespace coypu;
using namespace coypu::backtrace;
using namespace coypu::config;
using namespace coypu::event;
using namespace coypu::tcp;
using namespace coypu::net::ssl;
using namespace coypu::mem;
using namespace coypu::file;
using namespace coypu::store;
using namespace coypu::cache;
using namespace coypu::book;
using namespace coypu::backtrace;
using namespace coypu::admin;

extern "C" void processRust(uint32_t);

struct CoinLevel
{
  uint64_t px;
  uint64_t qty;
  CoinLevel *next, *prev;

	CoinLevel(uint64_t px, uint64_t qty) : px(px), qty(qty), next(nullptr), prev(nullptr)
	{
	}

	void Set(uint64_t px, uint64_t qty)
	{
		this->px = px;
		this->qty = qty;
	}

	CoinLevel() : px(UINT64_MAX), qty(UINT64_MAX), next(nullptr), prev(nullptr)
	{
	}

	bool operator()(const CoinLevel &lhs, const CoinLevel &rhs) const
	{
		return lhs.px < rhs.px;
	}

	bool operator()(const CoinLevel *lhs, const CoinLevel *rhs) const
	{
		return lhs->px < rhs->px;
	}
} __attribute__((packed, aligned(64)));

struct CoinCache {
  char _key[64]; // keep product first. if first byte is null, then this is treated as a blank record
  uint64_t _seqno;
  uint64_t _origseqno;

  uint32_t _seconds;
	uint32_t _milliseconds;

	double _high24;
	double _low24;
	double _vol24;
	double _open;
	double _last;

	CoinCache (uint64_t seqNo) : _seqno(seqNo), _origseqno(0), 
		_seconds(0), _milliseconds(0), _high24(0), _low24(0),
		_vol24(0), _open(0), _last(0) {
	  ::memset(_key, 0, sizeof(_key));
	}
} __attribute__ ((packed, aligned(64)));

// Coypu Types
typedef std::shared_ptr<SPDLogger> LogType;
typedef coypu::event::EventManager<LogType> EventManagerType;

typedef coypu::store::LogRWStream<MMapShared, coypu::store::LRUCache, 128> RWBufType;
typedef coypu::store::PositionedStream <RWBufType> StreamType;

typedef coypu::store::LogRWStream<MMapAnon, coypu::store::OneShotCache, 128> AnonRWBufType;
typedef coypu::store::PositionedStream <AnonRWBufType> AnonStreamType;

typedef coypu::store::MultiPositionedStreamLog <RWBufType> PublishStreamType;
typedef coypu::http::websocket::WebSocketManager <LogType, StreamType, PublishStreamType> WebSocketManagerType;

typedef coypu::http::websocket::WebSocketManager <LogType, AnonStreamType, PublishStreamType> AnonWebSocketManagerType;

typedef LogWriteBuf<MMapShared> StoreType;
typedef SequenceCache<CoinCache, 128, PublishStreamType, void> CacheType;
typedef CBook <CoinLevel, 4096*16>  BookType;
typedef std::unordered_map <std::string, std::shared_ptr<BookType> > BookMapType;
typedef AdminManager<LogType> AdminManagerType;
typedef OpenSSLManager <LogType> SSLType;

typedef std::function<void(void)> cb_type;
typedef std::unordered_map <int, std::shared_ptr<AnonStreamType>> TxtBufMapType;

const std::string COYPU_PUBLISH_PATH = "stream/publish/data";
const std::string COYPU_CACHE_PATH = "stream/cache/data";

typedef struct CoypuContextS {
  CoypuContextS (LogType &consoleLogger, LogType &wsLogger) {
	 _bookMap = std::make_shared<BookMapType>();
	 _txtBufs = std::make_shared<TxtBufMapType>();
	 _eventMgr = std::make_shared<EventManagerType>(consoleLogger);
	 _set_write_ws = std::bind(&EventManagerType::SetWrite, _eventMgr, std::placeholders::_1);
	 _wsManager = std::make_shared<WebSocketManagerType>(wsLogger, _set_write_ws);
	 _wsAnonManager = std::make_shared<AnonWebSocketManagerType>(wsLogger, _set_write_ws);
	 _adminManager = std::make_shared<AdminManagerType>(wsLogger, _set_write_ws);
	 _openSSLMgr = std::make_shared<SSLType>(wsLogger, _set_write_ws, "/etc/ssl/certs/"); 
  }
  
  std::shared_ptr <BookMapType> _bookMap;
  std::shared_ptr <TxtBufMapType> _txtBufs;
  std::shared_ptr <EventManagerType> _eventMgr;
  std::function<int(int)> _set_write_ws;
  std::shared_ptr <WebSocketManagerType> _wsManager;
  std::shared_ptr <AnonWebSocketManagerType> _wsAnonManager;
  std::shared_ptr <AdminManagerType> _adminManager;
  std::shared_ptr <SSLType> _openSSLMgr;
} CoypuContext;

void bar (std::shared_ptr<CoypuContext> context, bool &done) {
	CPUManager::SetName("epoll");

	while (!done) {
		if(context->_eventMgr->Wait() < 0) {
			done = true;
		}
	}
}

void EventClearBooks (std::shared_ptr<BookMapType> &bookMap) {
  for (auto &b : *bookMap) {
	 b.second->Clear();
  }
}

template <typename RecordType>
int RestoreStore (const std::string &name, const std::function<void(const char *, ssize_t)> &restore_record_cb) {
	uint32_t index = 0;
	bool b = false;
	constexpr size_t record_size = sizeof(RecordType);
	char data[record_size];
	do {
		char storeFile[PATH_MAX];
		::snprintf(storeFile, PATH_MAX, "%s.%09d.store", name.c_str(), index);
		FileUtil::Exists(storeFile, b);
		if (b) {
			int fd = FileUtil::Open(storeFile, O_LARGEFILE|O_RDONLY, 0600);
			if (fd >= 0) {
				off64_t curSize = 0;
				FileUtil::GetSize(fd, curSize);
				assert(curSize % record_size == 0);
				// FileUtil::LSeekSet (fd, 0);

				int count = 0;
				for (off64_t c = 0; c < curSize; c += record_size) {
					ssize_t r = ::read(fd, data, record_size);
					if (r < 0) {
						::close(fd);
						return r;
					}

					if (r != record_size) {
						::close(fd);
						return -100;
					}

					if (data[0] != 0) {
						++count;
						restore_record_cb(data, r);
					}
				}	

				::close(fd);
			}
		}
	} while (b && ++index < UINT32_MAX);

	return 0;
}

template <typename StreamType, typename BufType>
std::shared_ptr <StreamType> CreateStore (const std::string &name) {
	bool fileExists = false;
	char storeFile[PATH_MAX];
	std::shared_ptr<StreamType> streamSP = nullptr; 
		
	FileUtil::Mkdir(name.c_str(), 0777, true);
	
	// TODO if we go backward it will be quicker (less copies?)
	for (uint32_t index = 0; index < UINT32_MAX; ++index) {
		::snprintf(storeFile, PATH_MAX, "%s.%09d.store", name.c_str(), index);
		fileExists = false;
		FileUtil::Exists(storeFile, fileExists);
		if (!fileExists) {
			// open in direct mode
			int fd = FileUtil::Open(storeFile, O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
			if (fd >= 0) {
				int pageMult = 64;
				size_t pageSize = pageMult * MemManager::GetPageSize();
				off64_t curSize = 0;
				FileUtil::GetSize(fd, curSize);

				std::shared_ptr<BufType> bufSP = std::make_shared<BufType>(pageSize, curSize, fd, false);
				streamSP = std::make_shared<StreamType>(bufSP);
			} else {
				return nullptr;
			}
			return streamSP;
		}
	}
	return nullptr;
}

template <typename StreamType, typename BufType>
std::shared_ptr <StreamType> CreateAnonStore () {
	int pageMult = 64;
	size_t pageSize = pageMult * MemManager::GetPageSize();
	off64_t curSize = 0;
	std::shared_ptr<BufType> bufSP = std::make_shared<BufType>(pageSize, curSize, -1, true);
	return std::make_shared<StreamType>(bufSP);
}

int BindAndListen (const std::shared_ptr<coypu::SPDLogger> &logger, const std::string &interface, uint16_t port) {
	int sockFD = TCPHelper::CreateIPV4NonBlockSocket();
	if (sockFD < 0) {
		logger->perror(errno, "CreateIPV4NonBlockSocket");
		return -1;
	}

	if (TCPHelper::SetReuseAddr(sockFD) < 0 ) {
		logger->perror(errno, "SetReuseAddr");
		return -1;
	}
	struct sockaddr_in interface_in;
	int ret = TCPHelper::GetInterfaceIPV4FromName (interface.c_str(), interface.length(), interface_in);
	if (ret) {
	  logger->error("Failed to find interface[{0}]", interface);
		logger->perror(errno, "GetInterfaceIPV4FromName");
		return -1;
	}

	struct sockaddr_in serv_addr= {0}; //v4 family

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY; //interface_in.sin_addr.s_addr;
	serv_addr.sin_port = htons(port);

	ret = TCPHelper::BindIPV4(sockFD, &serv_addr);
	if (ret != 0) {
		::close(sockFD);
		logger->perror(errno, "BindIPV4");
		return -1;
	}

	ret = TCPHelper::Listen(sockFD, 16);
	if (ret != 0) {
		logger->perror(errno, "Listen");
		return -1;
	}
	return sockFD;
}

template <typename T, typename X>
std::shared_ptr<EventCBManager<T>> CreateCBManager (typename EventCBManager<T>::write_cb_type set_write, std::shared_ptr<X> eventMgr) {
  typedef EventCBManager<T> event_type;
  int fd = EventFDHelper::CreateNonBlockEventFD(0);
  if (fd < 0) return nullptr;

  std::shared_ptr <event_type> sp = std::make_shared<event_type>(fd, set_write);
  std::function<int(int)> readCB = std::bind(&event_type::Read, sp, std::placeholders::_1);
  std::function<int(int)> writeCB = std::bind(&event_type::Write, sp, std::placeholders::_1);
  std::function<int(int)> closeCB = std::bind(&event_type::Close, sp, std::placeholders::_1);
  if (eventMgr->Register(fd, readCB, writeCB, closeCB) < 0) return nullptr;
  
  return sp;
}

void DoServerTest (std::shared_ptr<CoypuContext> contextSP) 
{
  int wsFD = TCPHelper::ConnectStream("localhost", 8765);
  assert(wsFD > 0);
  std::function<int(int,const struct iovec*, int)> readvCB = [] (int fd, const struct iovec *iovec, int c) -> int { return ::readv(fd, iovec, c); };
  std::function<int(int,const struct iovec*, int)> writevCB = [] (int fd, const struct iovec *iovec, int c) -> int { return ::writev(fd, iovec, c); };
  std::function<int(int)> wsReadCB = std::bind(&WebSocketManagerType::Read, contextSP->_wsManager, std::placeholders::_1);
  std::function<int(int)> wsWriteCB = std::bind(&WebSocketManagerType::Write, contextSP->_wsManager, std::placeholders::_1);
  std::function<int(int)> wsCloseCB = std::bind(&WebSocketManagerType::Unregister, contextSP->_wsManager, std::placeholders::_1);
  
  contextSP->_wsManager->RegisterConnection(wsFD, false, readvCB, writevCB, nullptr, nullptr, nullptr, nullptr);	
  contextSP->_eventMgr->Register(wsFD, wsReadCB, wsWriteCB, wsCloseCB);
  contextSP->_wsManager->Stream(wsFD, "/foo", "localhost", "http://localhost");
}

void SetupAdmin (LogType &consoleLogger, std::string &interface, std::weak_ptr<CoypuContext> wContextSP) {
  int adminFD = BindAndListen(consoleLogger, interface, 9999);
  assert(adminFD > 0);
  
  coypu::event::callback_type adminAcceptCB = [wContextSP] (int fd) {
	 struct sockaddr_in client_addr= {0};
	 socklen_t addrlen= sizeof(sockaddr_in);
	 
	 // using IP V4
	 int clientfd = TCPHelper::AcceptNonBlock(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addrlen);
	 auto context = wContextSP.lock();
	 
	 if (context) {
		std::function<int(int,const struct iovec *,int)> rv = [] (int fd, const struct iovec *iov, int count) -> int { return ::readv(fd, iov, count); };
		std::function<int(int,const struct iovec *,int)> wv = [] (int fd, const struct iovec *iov, int count) -> int { return ::writev(fd, iov, count); };
		context->_adminManager->Register(clientfd, rv, wv);	
		
		std::function<int(int)> readCB = std::bind(&AdminManagerType::Read, context->_adminManager, std::placeholders::_1);
		std::function<int(int)> writeCB = std::bind(&AdminManagerType::Write, context->_adminManager, std::placeholders::_1);
		std::function<int(int)> closeCB = [wContextSP] (int fd) {
		  auto context = wContextSP.lock();
		  if (context) {
			 context->_adminManager->Unregister(fd);
		  }
		  return 0;
		};
		context->_eventMgr->Register(clientfd, readCB, writeCB, closeCB);
	 }
	 
	 return 0;
  };

  auto contextSP = wContextSP.lock();
  if (contextSP && contextSP->_eventMgr->Register(adminFD, adminAcceptCB, nullptr, nullptr)) {
	 consoleLogger->perror(errno, "Register");
  }
}

int main(int argc, char **argv)
{
	processRust(10);

	static_assert(sizeof(CoinCache) == 128, "CoinCache Size Check");

	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <yaml_config>\n", argv[0]);
		exit(1);
	}

	int r = CPUManager::RunOnNode(0);
	if (r) {
	  fprintf(stderr, "Num failed %d\n", r);
	  exit(1);
	}
	
	//ssl 
	SSLType::Init();
	int rc = RAND_load_file("/dev/urandom", 32); // /dev/random can be slow
	if(rc != 32) {
	  fprintf(stderr, "RAND_load_file fail.\n"); // ERR_*
	  exit(1);
	}
	
	// set terminate
	std::set_terminate([](){ std::cout << "Unhandled exception\n";
							BackTrace::bt();
							std::abort();});
	
	// Block all signals - wait til random is collected
	SignalFDHelper::BlockAllSignals();
	
	// Default console logger
	auto console = spdlog::stdout_color_mt("console"); // ,ultithread
	auto error_logger = spdlog::stderr_color_mt("stderr");
	spdlog::set_level(spdlog::level::info); // Set global log level to info
	console->info("Coypu [{1}] Git Revision [{0}]", _GIT_REV, COYPU_VERSION);
	// config
	std::shared_ptr<CoypuConfig> config = CoypuConfig::Parse(argv[1]);
	if (!config) {
	  error_logger->error("Failed to parse config");
	  exit(1);
	}
	config = config->GetConfig("<<ROOT>>");
	
	// loggers
	std::shared_ptr<CoypuConfig> loggers = config->GetConfig("loggers");
	if (loggers) {
	  std::vector<std::string> loggerNames;
	  loggers->GetKeys(loggerNames);
	  
	  for (const std::string &logger : loggerNames) {
		 auto cfgLog = loggers->GetConfig(logger);
		 assert(cfgLog);
		 
		 std::string level, file;
		 cfgLog->GetKeyValue("level", level);
		 cfgLog->GetKeyValue("file", file);
		 
		 if (level.empty() || file.empty()) {
			error_logger->error("Logger config missing level or file.");
			exit(1);
		 }
		 
		 auto log = spdlog::basic_logger_st(logger, file);
		 log->flush_on(spdlog::level::warn); 
		 log->set_level(spdlog::level::from_str(level));
		 // spdlog::register_logger(log); not needed
	  }
	} else {
	  assert(false);
	}

	// Config BEGIN
	std::string wsStoreFile;
	LogType wsLogger;
	{
		auto cfg = config->GetConfig("coypu");
		std::string logger;
		std::string cpuStr;
		if (cfg) {
			cfg = cfg->GetConfig("websocket");

			if (cfg) {
				cfg->GetValue("logger", logger);
				cfg->GetValue("cpu", cpuStr);
				cfg->GetValue("store-file", wsStoreFile);
			}
		}

		if (!logger.empty()) {
			std::shared_ptr<spdlog::logger> x = spdlog::get(logger);
			assert(x);
			wsLogger = std::make_shared<SPDLogger>(x);
		}

		CPUManager::SetName("websocket");
		if (!cpuStr.empty()) {
			int r = CPUManager::SetCPUs(cpuStr);
			assert(r == 0);
		}
	}
	assert(wsLogger);
	// Config END
	
	// app
	CoypuApplication &c = CoypuApplication::instance();
	c.foo();

	auto consoleLogger = std::make_shared<coypu::SPDLogger>(console);

	auto contextSP = std::make_shared<CoypuContext>(consoleLogger, wsLogger);
	std::weak_ptr <CoypuContext> wContextSP = contextSP;
	
	std::function<int(int)> set_write_ws = std::bind(&EventManagerType::SetWrite, contextSP->_eventMgr, std::placeholders::_1);
	contextSP->_eventMgr->Init();
	
	auto cbMgr = CreateCBManager<cb_type, EventManagerType>(set_write_ws, contextSP->_eventMgr);
	assert(cbMgr);

	sigset_t mask;
	::sigemptyset(&mask);
	::sigaddset(&mask, SIGINT);
	::sigaddset(&mask, SIGKILL);
	::sigaddset(&mask, SIGQUIT);
	int signalFD = SignalFDHelper::CreateNonBlockSignalFD(&mask);
	if (signalFD == -1) {
		consoleLogger->perror(errno, "CreateNonBlockSignalFD");
	}

	bool done = false;
	coypu::event::callback_type readCB = [&done](int fd) {
		printf("Read signal.\n");
		struct signalfd_siginfo signal;
		int count = ::read(fd, &signal, sizeof(signal));  
		if (count == -1) {
			fprintf(stderr, "Signal read error\n");
		}
		done = true;
		return 0;
	};

	if (contextSP->_eventMgr->Register(signalFD, readCB, nullptr, nullptr)) {
		consoleLogger->perror(errno, "Register");
	}

	std::string interface;
	config->GetValue("interface", interface);
	assert(!interface.empty());

	int sockFD = BindAndListen(consoleLogger, interface, 8080);


	// BEGIN Websocket Server Test
	std::string publish_path;
	config->GetValue("coypu-publish-path", publish_path, COYPU_PUBLISH_PATH);
	std::shared_ptr<PublishStreamType> publishStreamSP = CreateStore<PublishStreamType, RWBufType>(publish_path); 
	assert(publishStreamSP != nullptr);
	std::weak_ptr<PublishStreamType> wPublishStreamSP = publishStreamSP; 

	std::string cache_path;
	config->GetValue("coypu-cache-path", cache_path, COYPU_CACHE_PATH);
	std::shared_ptr<PublishStreamType> cacheStreamSP = CreateStore<PublishStreamType, RWBufType>(cache_path); 
	assert(cacheStreamSP != nullptr);

	std::shared_ptr<CacheType> coinCache = std::make_shared<CacheType>(cacheStreamSP);
	std::weak_ptr<CacheType> wCoinCache = coinCache;

	std::function<void(const char *, ssize_t)> restore = [&coinCache] (const char *data, ssize_t len) {
		assert(len == sizeof(CoinCache));
		const CoinCache *cc = reinterpret_cast<const CoinCache *>(data);
		coinCache->Restore(*cc);
	};
	int xx = RestoreStore<CoinCache>(COYPU_CACHE_PATH, restore);
	if (xx < 0) {
		wsLogger->perror(errno, "RestoreStore");
	}

	{
		std::stringstream ss;
		ss << *coinCache;
		console->info("Restore {0}", ss.str());
	}

	console->info("Cache check seqnum[{0}]", coinCache->CheckSeq());


	coypu::event::callback_type acceptCB = [wPublishStreamSP, wContextSP, logger=console](int fd) {
		struct sockaddr_in client_addr= {0};
		socklen_t addrlen= sizeof(sockaddr_in);

		// using IP V4
		int clientfd = TCPHelper::AcceptNonBlock(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addrlen);
		if (TCPHelper::SetNoDelay(clientfd)) {
		}

		logger->info("accept ws {0}", clientfd);
		
		auto publish = wPublishStreamSP.lock();
		auto context = wContextSP.lock();
		
		if (publish && context) {
		  std::shared_ptr<AnonStreamType> txtBuf = CreateAnonStore<AnonStreamType, AnonRWBufType>();
		  context->_txtBufs->insert(std::make_pair(clientfd, txtBuf));
			
		  uint64_t init_offset = UINT64_MAX;
		  publish->Register(clientfd, init_offset);

			std::function<int(int)> readCB = std::bind(&AnonWebSocketManagerType::Read, context->_wsAnonManager, std::placeholders::_1);
			std::function<int(int)> writeCB = std::bind(&AnonWebSocketManagerType::Write, context->_wsAnonManager, std::placeholders::_1);
			std::function<int(int)> closeCB = [wPublishStreamSP, wContextSP] (int fd) {
				auto publish = wPublishStreamSP.lock();
				if (publish) {
					publish->Unregister(fd);
				}
				auto context = wContextSP.lock();
				if (context) {
				  context->_wsAnonManager->Unregister(fd);

				  auto b = context->_txtBufs->find(fd);
				  if (b != context->_txtBufs->end()) {
					 context->_txtBufs->erase(b);
				  }
				}
				return 0;
			};


			// std::bind(&WebSocketManagerType::Unregister, wsManager, std::placeholders::_1);
			std::function <void(uint64_t, uint64_t)> onText = [clientfd, txtBuf, wPublishStreamSP, wContextSP, logger] (uint64_t offset, off64_t len) {
			  // could do something with a request from client here. json sub msg? with mark
			  char jsonDoc[1024*1024] = {};
			  if (len < sizeof(jsonDoc)) {
				 // sad copying but nice json library
				 if(txtBuf->Pop(jsonDoc, offset, len)) {
					jsonDoc[len] = 0;
					logger->debug("{0} doc {1}", clientfd, jsonDoc);
					
					Document jd;
					if (!jd.Parse(jsonDoc).HasParseError()) {
					  if (jd.HasMember("cmd")) {
						 const char *cmd = jd["cmd"].GetString();
						 if (!strncmp(cmd, "mark", 4)) {
							if (jd.HasMember("offset")) {
							  const uint64_t offset = jd["offset"].GetUint64();
							  auto publish = wPublishStreamSP.lock();
							  auto context = wContextSP.lock();
							  if (publish && context) {
								 if (publish->Mark(clientfd, offset)) {
									logger->info("Mark {0} {1}" , clientfd, offset);
									context->_eventMgr->SetWrite(clientfd);
								 } else {
									logger->error("Mark failed {0} {1}" , clientfd, offset);
								 }
							  }
							} else {
							  auto publish = wPublishStreamSP.lock();
							  auto context = wContextSP.lock();
							  if (publish && context) {
								 if (publish->MarkEnd(clientfd)) {
									logger->info("Mark end {0}" , clientfd);
									context->_eventMgr->SetWrite(clientfd);
								 } else {
									logger->error("Mark end failed {0}" , clientfd);
								 }
							  }
							}
						 } else {
							logger->error("Unsupported command {0}", cmd);
						 }
					  }
					} else {
					  logger->error("Error(offset {0}): {1}", 
										 (unsigned)jd.GetErrorOffset(),
										 GetParseError_En(jd.GetParseError()));
					}
				 } else {
					logger->error("Pop failed");
				 }
			  }

			  return;
			};

			std::function <int(int,const struct iovec*, int)> readvCB = [] (int fd, const struct iovec *iovec, int c) -> int { return ::readv(fd, iovec, c); };
			std::function <int(int,const struct iovec*, int)> writevCB = [] (int fd, const struct iovec *iovec, int c) -> int { return ::writev(fd, iovec, c); };
			context->_wsAnonManager->RegisterConnection(clientfd, true, readvCB, writevCB, nullptr, onText, txtBuf, publish);	
			context->_eventMgr->Register(clientfd, readCB, writeCB, closeCB);

			int timerFD = TimerFDHelper::CreateMonotonicNonBlock();
			TimerFDHelper::SetRelativeRepeating(timerFD, 5, 0);

			std::function<int(int)> readTimerCB = [wContextSP, wPublishStreamSP, clientfd] (int fd) {
				uint64_t x;
				if (read(fd, &x, sizeof(uint64_t)) != sizeof(uint64_t)) {
					// TODO some error
					assert(false);
				}

				auto context = wContextSP.lock();
				auto publish = wPublishStreamSP.lock();
				if (publish && context) {
					// Create a websocket message and persist
					char pub[1024];
					static int count = 0;
					size_t len = ::snprintf(pub, 1024, "Timer [%d]", count++);
					WebSocketManagerType::WriteFrame(publish, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
					publish->Push(pub, len);
					context->_eventMgr->SetWrite(clientfd);
				}

				return 0;
			};
			context->_eventMgr->Register(timerFD, readTimerCB, nullptr, nullptr);
		}

		return 0;
	};

	if (contextSP->_eventMgr->Register(sockFD, acceptCB, nullptr, nullptr)) {
		consoleLogger->perror(errno, "Register");
	}
	// END Websocket Server Test

	std::function<int(int)> wsReadCB = std::bind(&WebSocketManagerType::Read, contextSP->_wsManager, std::placeholders::_1);
	std::function<int(int)> wsWriteCB = std::bind(&WebSocketManagerType::Write, contextSP->_wsManager, std::placeholders::_1);
	std::function<int(int)> wsCloseCB = std::bind(&WebSocketManagerType::Unregister, contextSP->_wsManager, std::placeholders::_1);

	bool doCB = false;
	config->GetValue("do-gdax", doCB);
	if (doCB) {
		int wsFD = TCPHelper::ConnectStream("ws-feed.pro.coinbase.com", 443);

		if (wsFD < 0) {
			consoleLogger->error("failed to connect to ws-feed.");
			exit(1);
		}
		int r = TCPHelper::SetRecvSize(wsFD, 64*1024);
		assert(r == 0);
		r= TCPHelper::SetNoDelay(wsFD);
		assert(r == 0);

		contextSP->_openSSLMgr->Register(wsFD);

		std::function <int(int,const struct iovec*,int)> sslReadCB =
		  std::bind(&SSLType::ReadvNonBlock, contextSP->_openSSLMgr, std::placeholders::_1,  std::placeholders::_2,  std::placeholders::_3);
		std::function <int(int,const struct iovec *,int)> sslWriteCB =
		  std::bind(&SSLType::WritevNonBlock, contextSP->_openSSLMgr, std::placeholders::_1,  std::placeholders::_2,  std::placeholders::_3);

		std::vector <std::string> symbolList;
		std::vector <std::string> channelList;
		config->GetSeqValues("gdax-symbols", symbolList);
		config->GetSeqValues("gdax-channels", channelList);

		std::function <void(int)> onOpen = [wContextSP, symbolList, channelList] (int fd) {
			auto context = wContextSP.lock();
			if (context) {
				std::string subStr;
				bool queue;

				for (std::string channel : channelList) {
					for (std::string pair : symbolList) {
						subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"" + channel + "\", \"product_ids\": [\"" + pair + "\"]}]}";
						queue = context->_wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());
					}
				}
			}
		};

		std::function<int(int)> closeSSL = [wContextSP] (int fd) {
			auto context = wContextSP.lock();
			if (context) {
			  context->_openSSLMgr->Unregister(fd);
			  context->_wsManager->Unregister(fd);
			}
			std::cout << "Clear books" << std::endl;
			std::cout << "Reconnect!!" << std::endl;
			// BOOK_EVENT_CLEAR
			// WS_EVENT_CONNECT
			// event maps to fd.
			// when we fire. write to fd using eventfd, then read from eventfd to fire?

			// should just be one eventfd. which we write a uint64_t to which is the event.
			// read the eventId then fire the callback from the map. should be simple loop.
			// fire event to connect. to run through this code. initial connect should be event
			// that way there is no special logic. just fire event back to pool.
			return 0;
		};

		// TODO Fix
		std::string storeFile("gdax.store");
		std::shared_ptr<StreamType> streamSP = nullptr; 

		if (!storeFile.empty()) {
			bool b = false;
			FileUtil::Exists(storeFile.c_str(), b);
			// open in direct mode
			int fd = FileUtil::Open(storeFile.c_str(), O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
			if (fd >= 0) {
				size_t pageSize = 64 * MemManager::GetPageSize();
				off64_t curSize = 0;
				FileUtil::GetSize(fd, curSize);
				consoleLogger->info("Current size [{0}]", curSize);

				std::shared_ptr<RWBufType> bufSP = std::make_shared<RWBufType>(pageSize, curSize, fd, false);
				streamSP = std::make_shared<StreamType>(bufSP);
			} else {
				consoleLogger->perror(errno, "Open");
			}
		}
		std::weak_ptr<StreamType> wStreamSP = streamSP; 

		// not weak
		
		std::function <void(uint64_t, uint64_t)> onText = [&console, wPublishStreamSP, wStreamSP, wCoinCache, wContextSP] (uint64_t offset, off64_t len) {
			auto publish = wPublishStreamSP.lock();
			auto stream = wStreamSP.lock();
			auto coinCache = wCoinCache.lock();
			auto context = wContextSP.lock();
			if (publish && stream  && coinCache && context) {
				// std::cout << stream->Available() << std::endl;
				if (!(coinCache->CheckSeq() % 10000)) {
					std::stringstream ss;
					ss << *coinCache;
					console->info("onText {0} {1} SeqNum[{2}] {3} ", len, offset, coinCache->CheckSeq(), ss.str());
				}

				char jsonDoc[1024*1024] = {};
				if (len < sizeof(jsonDoc)) {
					// sad copying but nice json library
					if(stream->Pop(jsonDoc, offset, len)) {
						jsonDoc[len] = 0;
						Document jd;

			         uint64_t start = 0, end = 0;
			         unsigned int junk= 0;
						start = __rdtscp(&junk);
    					jd.Parse(jsonDoc);
						end = __rdtscp(&junk);
						//printf("%zu\n", (end-start));
					 

						const char * type = jd["type"].GetString();
						if (!strcmp(type, "snapshot")) {
							const char *product = jd["product_id"].GetString();
							context->_bookMap->insert(std::make_pair(product, std::make_shared<BookType>())); // create string
							std::shared_ptr<BookType> book = (*context->_bookMap)[product];
							assert(book);

							const Value& bids = jd["bids"];
							const Value& asks = jd["asks"];

							for (SizeType i = 0; i < bids.Size(); ++i) {
								const char * px = bids[i][0].GetString();
								const char * qty = bids[i][0].GetString();
								uint64_t ipx = atof(px) * 100000000;
								uint64_t iqty = atof(qty) * 100000000;
								int outindex = -1;
								book->InsertBid(ipx, iqty, outindex);
							}

							for (SizeType i = 0; i < asks.Size(); ++i) {
								const char * px = asks[i][0].GetString();
								const char * qty = asks[i][0].GetString();
								uint64_t ipx = atof(px) * 100000000;
								uint64_t iqty = atof(qty) * 100000000;
								int outindex = -1;
								book->InsertAsk(ipx, iqty, outindex);
							}
						} else if (!strcmp(type, "l2update")) {
							const char *product = jd["product_id"].GetString();
							static std::string lookup;
							lookup = product; // should call look.reserve(8);
							if (context->_bookMap->find(lookup) == context->_bookMap->end()) {
							  std::cerr << "Missing book " << lookup << std::endl;
							  //							  return;
							}
							std::shared_ptr<BookType> book = (*context->_bookMap)[lookup];
							assert(book);

							const Value& changes = jd["changes"];
							for (SizeType i = 0; i < changes.Size(); ++i) {
								const char * side = changes[i][0].GetString();
								const char * px = changes[i][1].GetString();
								const char * qty = changes[i][2].GetString();

								uint64_t ipx = atof(px) * 100000000;
								uint64_t iqty = atof(qty) * 100000000;
								int outindex = -1;

								if(!strcmp(side, "buy")) {
									if (iqty == 0) {
										book->EraseBid(ipx, outindex);
									} else {
										if (!book->UpdateBid(ipx, iqty, outindex)) {
											book->InsertBid(ipx, iqty, outindex);
										}
									}
								} else {
									if (iqty == 0) {
										book->EraseAsk(ipx, outindex);
									} else {
										if (!book->UpdateAsk(ipx, iqty, outindex)) {
											book->InsertAsk(ipx, iqty, outindex);
										}
									}
								}

								CoinLevel bid,ask;
								book->BestBid(bid);
								book->BestAsk(ask);
								char pub[1024];
								size_t len = ::snprintf(pub, 1024, "Tick %s %zu %zu %zu %zu", product, 
																bid.qty, bid.px, ask.px, ask.qty);
								WebSocketManagerType::WriteFrame(publish, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
								publish->Push(pub, len);
								context->_wsAnonManager->SetWriteAll();

									// std::cout << std::endl << std::endl;
									// book->RDumpAsk(20, true);			
									// std::cout << "---" << std::endl;
									// book->RDumpBid(20);			
							}
						} else if (!strcmp(type, "error")) {
							console->error("{0}", jsonDoc);
						} else if (!strcmp(type, "ticker")) {
							const char *product = jd["product_id"].GetString();
							const char *vol24 = jd["volume_24h"].GetString();
							
							// "best_ask":"6423.6",
							// "best_bid":"6423.59",
							// x"high_24h":"6471.00000000",
							// "last_size":"0.00147931",
							// x"low_24h":"6384.04000000",
							// "open_24h":"6388.00000000",
							// "price":"6423.60000000",
							// x"product_id":"BTC-USD",
							// x"sequence":7230193662,
							// "side":"buy",
							// x"time":"2018-10-24T16:48:56.559000Z",
							// "trade_id":52862394,
							// "type":"ticker",
							// x"volume_24h":"5365.44495907",
							// "volume_30d":"195043.05076131"

							// product_id, time, high_low,volume,open,price

							CoinCache cc(coinCache->NextSeq());
							if (product) {
								memcpy(cc._key, product, std::max(sizeof(cc._key)-1, ::strlen(product)));
							}

							const char *timeRaw =jd.HasMember("time") ? jd["time"].GetString(): nullptr;
							if (timeRaw) {
								std::string timeStr(timeRaw);

								if (!timeStr.empty()) {
									struct tm tm;
									memset(&tm, 0, sizeof(struct tm));
									strptime(timeStr.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);

									size_t i = timeStr.find('.');
									if (i > 0) {
										cc._milliseconds = atoi(timeStr.substr(i+1, 6).c_str());
										cc._seconds  = mktime(&tm);
									}
								}
							}
							
							const uint64_t tradeId = jd.HasMember("trade_id") ? jd["trade_id"].GetUint64() : UINT64_MAX;
							const char *h24 = jd.HasMember("high_24h") ? jd["high_24h"].GetString() : nullptr;
							const char *l24 = jd.HasMember("low_24h") ? jd["low_24h"].GetString() : nullptr;
							const char *v24 = jd.HasMember("volume_24h") ? jd["volume_24h"].GetString() : nullptr;
							const char *px = jd.HasMember("price") ? jd["price"].GetString() : nullptr;
							const char *lastSize = jd.HasMember("last_size") ? jd["last_size"].GetString() : nullptr;

							if (h24) {
								cc._high24 = atof(h24);
							}

							if (l24) {
								cc._low24 = atof(l24);
							}

							if (v24) {
							  cc._vol24 = atof(v24);
							}

							if (px) {
							  cc._last = atof(px);
							}

							if (jd.HasMember("sequence")) {
								cc._origseqno = jd["sequence"].GetUint64();
							}

							coypu::msg::CoinCache gCC;
							gCC.set_high24(cc._high24);
							gCC.set_low24(cc._low24);
							gCC.set_vol24(cc._vol24);
							gCC.set_last(cc._last);
							std::string outStr;
							if (gCC.SerializeToString(&outStr)) {
							  
							} else {
							  assert(false);
							}
							
							
							// write protobuf to a websocket publish stream. doesnt need to be sequenced
							// // WebSocketManagerType::WriteFrame(cache, coypu::http::websocket::WS_OP_BINARY_FRAME, false, sizeof(CoinCache));
							
							start = __rdtscp(&junk);
							coinCache->Push(cc);
							end = __rdtscp(&junk);
							//printf("%zu\n", (end-start));

							if (tradeId != UINT64_MAX) {
							  char pub[1024];
							  size_t len = ::snprintf(pub, 1024, "Trade %s %s %s %zu %s", product, vol24, px, tradeId, lastSize);
							  WebSocketManagerType::WriteFrame(publish, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
							  publish->Push(pub, len);
							  context->_wsAnonManager->SetWriteAll();
							}
						} else if (!strcmp(type, "subscriptions")) {
							// skip
						} else if (!strcmp(type, "heartbeat")) {
							// skip
						} else {

						  console->warn("{0} {1}", type, jsonDoc); // spdlog does not do streams
						}
					} else {
						assert(false);
					}
				}
			}
		};
		
		// stream is associated with the fd. socket can only support one websocket connection at a time.
		contextSP->_wsManager->RegisterConnection(wsFD, false, sslReadCB, sslWriteCB, onOpen, onText, streamSP, nullptr);	
		contextSP->_eventMgr->Register(wsFD, wsReadCB, wsWriteCB, closeSSL); // no race here as long as we dont call stream

		// Sets the end-point 
		contextSP->_wsManager->Stream(wsFD, "/", "ws-feed.pro.coinbase.com", "http://ws-feed.pro.coinbase.com"); 
	}
	
	// Test client
	config->GetValue("do-server-test", doCB);
	if (doCB) DoServerTest(contextSP);
	
	SetupAdmin(consoleLogger, interface, wContextSP);

	// watch out for threading on loggers
	std::thread t1(bar, contextSP, std::ref(done));
	t1.join();
	contextSP->_eventMgr->Close();

	google::protobuf::ShutdownProtobufLibrary();
	
	return 0;
}

