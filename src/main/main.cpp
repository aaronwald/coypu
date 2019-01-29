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

enum CoypuEvents {
  CE_GDAX_BOOK_CLEAR,
  CE_WS_CONNECT_GDAX,
  CE_WS_CONNECT_KRAKEN
};

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

// BEGIN Coypu Types
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
typedef std::function<void(void)> CBType;
typedef std::unordered_map <int, std::shared_ptr<AnonStreamType>> TxtBufMapType;
// END Coypu Types

const std::string COYPU_PUBLISH_PATH = "stream/publish/data";
const std::string COYPU_CACHE_PATH = "stream/cache/data";

typedef struct CoypuContextS {
  CoypuContextS (LogType &consoleLogger, LogType &wsLogger) {
	 _consoleLogger = consoleLogger;
	 _bookMap = std::make_shared<BookMapType>();
	 _txtBufs = std::make_shared<TxtBufMapType>();
	 _eventMgr = std::make_shared<EventManagerType>(consoleLogger);
	 _set_write_ws = std::bind(&EventManagerType::SetWrite, _eventMgr, std::placeholders::_1);
	 _wsManager = std::make_shared<WebSocketManagerType>(wsLogger, _set_write_ws);
	 _wsAnonManager = std::make_shared<AnonWebSocketManagerType>(wsLogger, _set_write_ws);
	 _adminManager = std::make_shared<AdminManagerType>(wsLogger, _set_write_ws);
	 _openSSLMgr = std::make_shared<SSLType>(wsLogger, _set_write_ws, "/etc/ssl/certs/"); 
  }
  CoypuContextS(const CoypuContextS &other) = delete;
  CoypuContextS &operator=(const CoypuContextS &other) = delete;

  LogType _consoleLogger;
  
  std::shared_ptr <BookMapType> _bookMap;
  std::shared_ptr <TxtBufMapType> _txtBufs;
  std::shared_ptr <EventManagerType> _eventMgr;
  std::function<int(int)> _set_write_ws;
  std::shared_ptr <WebSocketManagerType> _wsManager;
  std::shared_ptr <AnonWebSocketManagerType> _wsAnonManager;
  std::shared_ptr <AdminManagerType> _adminManager;
  std::shared_ptr <SSLType> _openSSLMgr;

  std::shared_ptr <PublishStreamType> _publishStreamSP;
  std::shared_ptr <PublishStreamType> _cacheStreamSP;
  std::shared_ptr <CacheType> _coinCache;
  std::shared_ptr <StreamType> _gdaxStreamSP;
  std::shared_ptr <StreamType> _krakenStreamSP;
  std::shared_ptr <EventCBManager<CBType>> _cbManager;

  std::unordered_map<int, std::pair<std::string, std::string>> _krakenChannelToPairType;
} CoypuContext;

void bar (std::shared_ptr<CoypuContext> context, bool &done) {
	CPUManager::SetName("coypu_epoll");

	while (!done) {
		if(context->_eventMgr->Wait() < 0) {
			done = true;
		}
	}
}

void EventClearBooks (std::weak_ptr<CoypuContext> wContext) {
  auto consoleLogger = spdlog::get("console");
  assert(consoleLogger);

  std::shared_ptr<CoypuContext> context = wContext.lock();
  if (context) {
	 for (auto &b : *context->_bookMap) {
		if (consoleLogger) {
		  consoleLogger->info("Clear {0}", b.first);
		}

		b.second->Clear();
	 }
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
std::shared_ptr<EventCBManager<T>> CreateCBManager (std::shared_ptr<CoypuContext> &contextSP) {
  typedef EventCBManager<T> event_type;
  int fd = EventFDHelper::CreateNonBlockEventFD(0);
  if (fd < 0) return nullptr;

  std::shared_ptr <event_type> sp = std::make_shared<event_type>(fd, contextSP->_set_write_ws);

  std::weak_ptr<CoypuContext> wContext = contextSP;
  std::function<void(void)> cb = [wContext] () -> void { EventClearBooks(wContext); }; 
  sp->Register(CE_GDAX_BOOK_CLEAR, cb);
  
  std::function<int(int)> readCB = std::bind(&event_type::Read, sp, std::placeholders::_1);
  std::function<int(int)> writeCB = std::bind(&event_type::Write, sp, std::placeholders::_1);
  std::function<int(int)> closeCB = std::bind(&event_type::Close, sp, std::placeholders::_1);
  if (contextSP->_eventMgr->Register(fd, readCB, writeCB, closeCB) != 0) {
	 std::cerr << "Failed to register queue" << std::endl;
	 assert(false);
	 return nullptr;
  }
  
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

void SetupAdmin (std::string &interface, std::shared_ptr<CoypuContext> context) {
  std::weak_ptr <CoypuContext> wContextSP = context;
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
  if (contextSP) {
	 int adminFD = BindAndListen(contextSP->_consoleLogger, interface, 9999);
	 if (adminFD > 0) {
		if (contextSP->_eventMgr->Register(adminFD, adminAcceptCB, nullptr, nullptr)) {
		  contextSP->_consoleLogger->perror(errno, "Register");
		}
	 } else {
		contextSP->_consoleLogger->error("Failed to created admin fd");
	 }
  }
}

void CreateStores(std::shared_ptr<CoypuConfig> &config, std::shared_ptr<CoypuContext> &contextSP) {
  	std::string publish_path;
	config->GetValue("coypu-publish-path", publish_path, COYPU_PUBLISH_PATH);
	contextSP->_publishStreamSP = CreateStore<PublishStreamType, RWBufType>(publish_path); 

	std::string cache_path;
	config->GetValue("coypu-cache-path", cache_path, COYPU_CACHE_PATH);
	contextSP->_cacheStreamSP = CreateStore<PublishStreamType, RWBufType>(cache_path); 

	contextSP->_coinCache = std::make_shared<CacheType>(contextSP->_cacheStreamSP);

	std::function<void(const char *, ssize_t)> restore = [&contextSP] (const char *data, ssize_t len) {
		assert(len == sizeof(CoinCache));
		const CoinCache *cc = reinterpret_cast<const CoinCache *>(data);
		contextSP->_coinCache->Restore(*cc);
	};

	if (RestoreStore<CoinCache>(COYPU_CACHE_PATH, restore) < 0) {
	  contextSP->_consoleLogger->perror(errno, "RestoreStore");
	}

	std::stringstream ss;
	ss << *(contextSP->_coinCache);
	contextSP->_consoleLogger->info("Restore {0}", ss.str());
	contextSP->_consoleLogger->info("Cache check seqnum[{0}]", contextSP->_coinCache->CheckSeq());

	std::string gdaxStoreFile("gdax.store");

	bool b = false;
	FileUtil::Exists(gdaxStoreFile.c_str(), b);
	// open in direct mode
	int fd = FileUtil::Open(gdaxStoreFile.c_str(), O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
	if (fd >= 0) {
	  size_t pageSize = 64 * MemManager::GetPageSize();
	  off64_t curSize = 0;
	  FileUtil::GetSize(fd, curSize);
	  contextSP->_consoleLogger->info("Current size [{0}]", curSize);
	  
	  std::shared_ptr<RWBufType> bufSP = std::make_shared<RWBufType>(pageSize, curSize, fd, false);
	  contextSP->_gdaxStreamSP = std::make_shared<StreamType>(bufSP);
	} else {
	  contextSP->_consoleLogger->perror(errno, "Open");
	}

	std::string krakenStoreFile("kraken.store");
	b = false;
	FileUtil::Exists(krakenStoreFile.c_str(), b);
	// open in direct mode
	fd = FileUtil::Open(krakenStoreFile.c_str(), O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
	if (fd >= 0) {
	  size_t pageSize = 64 * MemManager::GetPageSize();
	  off64_t curSize = 0;
	  FileUtil::GetSize(fd, curSize);
	  contextSP->_consoleLogger->info("Current size [{0}]", curSize);
	  
	  std::shared_ptr<RWBufType> bufSP = std::make_shared<RWBufType>(pageSize, curSize, fd, false);
	  contextSP->_krakenStreamSP = std::make_shared<StreamType>(bufSP);
	} else {
	  contextSP->_consoleLogger->perror(errno, "Open");
	}

}

void AcceptWebsocketClient (std::shared_ptr<CoypuContext> &context, const LogType &logger, int fd) {
  std::weak_ptr <CoypuContext> wContextSP = context;
  struct sockaddr_in client_addr= {0};
  socklen_t addrlen= sizeof(sockaddr_in);
  
  // using IP V4
  int clientfd = TCPHelper::AcceptNonBlock(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addrlen);
  if (TCPHelper::SetNoDelay(clientfd)) {
  }
  
  logger->info("accept ws {0}", clientfd);
  
  if (context) {
	 std::shared_ptr<AnonStreamType> txtBuf = CreateAnonStore<AnonStreamType, AnonRWBufType>();
	 context->_txtBufs->insert(std::make_pair(clientfd, txtBuf));
	 
	 uint64_t init_offset = UINT64_MAX;
	 context->_publishStreamSP->Register(clientfd, init_offset);
	 
	 std::function<int(int)> readCB = std::bind(&AnonWebSocketManagerType::Read, context->_wsAnonManager, std::placeholders::_1);
	 std::function<int(int)> writeCB = std::bind(&AnonWebSocketManagerType::Write, context->_wsAnonManager, std::placeholders::_1);
	 std::function<int(int)> closeCB = [wContextSP] (int fd) {
		auto context = wContextSP.lock();
		if (context) {
		  context->_publishStreamSP->Unregister(fd);
		  context->_wsAnonManager->Unregister(fd);

		  auto b = context->_txtBufs->find(fd);
		  if (b != context->_txtBufs->end()) {
			 context->_txtBufs->erase(b);
		  }
		}
		return 0;
	 };

	 std::function <void(uint64_t, uint64_t)> onText = [clientfd, txtBuf, wContextSP, logger] (uint64_t offset, off64_t len) {
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
						auto context = wContextSP.lock();
						if (context) {
						  if (context->_publishStreamSP->Mark(clientfd, offset)) {
							 logger->info("Mark {0} {1}" , clientfd, offset);
							 context->_eventMgr->SetWrite(clientfd);
						  } else {
							 logger->error("Mark failed {0} {1}" , clientfd, offset);
						  }
						}
					 } else {
						auto context = wContextSP.lock();
						if (context) {
						  if (context->_publishStreamSP->MarkEnd(clientfd)) {
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
	 context->_wsAnonManager->RegisterConnection(clientfd, true, readvCB, writevCB, nullptr, onText, txtBuf, context->_publishStreamSP);	
	 context->_eventMgr->Register(clientfd, readCB, writeCB, closeCB);
	 
	 int timerFD = TimerFDHelper::CreateMonotonicNonBlock();
	 TimerFDHelper::SetRelativeRepeating(timerFD, 5, 0);
	 
	 std::function<int(int)> readTimerCB = [wContextSP, clientfd] (int fd) {
		uint64_t x;
		if (read(fd, &x, sizeof(uint64_t)) != sizeof(uint64_t)) {
		  // TODO some error
		  assert(false);
		}
		
		auto context = wContextSP.lock();
		if (context) {
		  // Create a websocket message and persist
		  char pub[1024];
		  static int count = 0;
		  size_t len = ::snprintf(pub, 1024, "Timer [%d]", count++);
		  WebSocketManagerType::WriteFrame(context->_publishStreamSP, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
		  context->_publishStreamSP->Push(pub, len);
		  context->_eventMgr->SetWrite(clientfd);
		}
		
		return 0;
	 };
	 context->_eventMgr->Register(timerFD, readTimerCB, nullptr, nullptr);
  }
}

void StreamGDAX (std::shared_ptr<CoypuContext> contextSP, const std::string &hostname, uint32_t port,
					  const std::vector<std::string> &symbolList,
					  const std::vector<std::string> &channelList) {
  int wsFD = TCPHelper::ConnectStream(hostname.c_str(), port);
  std::weak_ptr <CoypuContext> wContextSP = contextSP;

  if (wsFD < 0) {
	 contextSP->_consoleLogger->error("failed to connect to {0}", hostname);
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
		context->_cbManager->Queue(CE_GDAX_BOOK_CLEAR);
		context->_cbManager->Queue(CE_WS_CONNECT_GDAX);
	 }


	 return 0;
  };

  std::function <void(uint64_t, uint64_t)> onText = [wContextSP] (uint64_t offset, off64_t len) {
	 auto context = wContextSP.lock();

	 if (context) {
		char jsonDoc[1024*1024] = {};
		if (len < sizeof(jsonDoc)) {
		  // sad copying but nice json library
		  if(context->_gdaxStreamSP->Pop(jsonDoc, offset, len)) {
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
				  WebSocketManagerType::WriteFrame(context->_publishStreamSP, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
				  context->_publishStreamSP->Push(pub, len);
				  context->_wsAnonManager->SetWriteAll();

				  // std::cout << std::endl << std::endl;
				  // book->RDumpAsk(20, true);			
				  // std::cout << "---" << std::endl;
				  // book->RDumpBid(20);			
				}
			 } else if (!strcmp(type, "error")) {
				context->_consoleLogger->error("{0}", jsonDoc);
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

				CoinCache cc(context->_coinCache->NextSeq());

				if (!(context->_coinCache->CheckSeq() % 10000)) {
				  std::stringstream ss;
				  ss << *(context->_coinCache);
				  context->_consoleLogger->info("onText {0} {1} SeqNum[{2}] {3} ", len, offset, context->_coinCache->CheckSeq(), ss.str());
				}


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
							
				// gRPC?			
				// write protobuf to a websocket publish stream. doesnt need to be sequenced
				// // WebSocketManagerType::WriteFrame(cache, coypu::http::websocket::WS_OP_BINARY_FRAME, false, sizeof(CoinCache));
							
				start = __rdtscp(&junk);
				context->_coinCache->Push(cc);
				end = __rdtscp(&junk);
				//printf("%zu\n", (end-start));

				if (tradeId != UINT64_MAX) {
				  char pub[1024];
				  size_t len = ::snprintf(pub, 1024, "Trade %s %s %s %zu %s", product, vol24, px, tradeId, lastSize);
				  WebSocketManagerType::WriteFrame(context->_publishStreamSP, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
				  context->_publishStreamSP->Push(pub, len);
				  context->_wsAnonManager->SetWriteAll();
				}
			 } else if (!strcmp(type, "subscriptions")) {
				// skip
			 } else if (!strcmp(type, "heartbeat")) {
				// skip
			 } else {
				context->_consoleLogger->warn("{0} {1}", type, jsonDoc); // spdlog does not do streams
			 }
		  } else {
			 assert(false);
		  }
		}
	 }
  };
		
  // stream is associated with the fd. socket can only support one websocket connection at a time.
  std::function<int(int)> wsReadCB = std::bind(&WebSocketManagerType::Read, contextSP->_wsManager, std::placeholders::_1);
  std::function<int(int)> wsWriteCB = std::bind(&WebSocketManagerType::Write, contextSP->_wsManager, std::placeholders::_1);
  std::function<int(int)> wsCloseCB = std::bind(&WebSocketManagerType::Unregister, contextSP->_wsManager, std::placeholders::_1);
		
  contextSP->_wsManager->RegisterConnection(wsFD, false, sslReadCB, sslWriteCB, onOpen, onText, contextSP->_gdaxStreamSP, nullptr);	
  contextSP->_eventMgr->Register(wsFD, wsReadCB, wsWriteCB, closeSSL); // no race here as long as we dont call stream

  // Sets the end-point
  char uri[1024];
  snprintf(uri, 1024, "http://%s", hostname.c_str());
  contextSP->_wsManager->Stream(wsFD, "/", hostname, uri);
}

void StreamKraken (std::shared_ptr<CoypuContext> contextSP, const std::string &hostname, uint32_t port,
						 const std::vector<std::string> &symbolList) {
  int wsFD = TCPHelper::ConnectStream(hostname.c_str(), port);
  std::weak_ptr <CoypuContext> wContextSP = contextSP;

  if (wsFD < 0) {
	 contextSP->_consoleLogger->error("failed to connect to {0}", hostname);
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

  
  std::function <void(int)> onOpen = [wContextSP, symbolList] (int fd) {
	 auto context = wContextSP.lock();
	 if (context) {
		std::string subStr;
		bool queue;

		std::string pairList;
		for (int i = 0; i < symbolList.size(); ++i) {
		  pairList += "\"" + symbolList[i] + "\"";
		  if (i + 1 < symbolList.size()) {
			 pairList += ",";
		  }
		}

		subStr= "{\"event\": \"subscribe\", \"pair\": [" + pairList + "], \"subscription\": { \"name\" : \"*\", \"depth\": 100}}";
		queue = context->_wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());
		
	 }
  };

    std::function<int(int)> closeSSL = [wContextSP] (int fd) {
	 auto context = wContextSP.lock();
	 if (context) {
		context->_openSSLMgr->Unregister(fd);
		context->_wsManager->Unregister(fd);
		context->_cbManager->Queue(CE_GDAX_BOOK_CLEAR);
		context->_cbManager->Queue(CE_WS_CONNECT_GDAX);
	 }


	 return 0;
  };

  std::function <void(uint64_t, uint64_t)> onText = [wContextSP] (uint64_t offset, off64_t len) {
	 auto context = wContextSP.lock();

	 if (context) {
		char jsonDoc[1024*1024] = {};
		if (len < sizeof(jsonDoc)) {
		  // sad copying but nice json library
		  if(context->_gdaxStreamSP->Pop(jsonDoc, offset, len)) {
			 jsonDoc[len] = 0;
			 Document jd;

			 uint64_t start = 0, end = 0;
			 unsigned int junk= 0;
			 start = __rdtscp(&junk);
			 jd.Parse(jsonDoc);
			 end = __rdtscp(&junk);
			 //printf("%zu\n", (end-start));
			 if (!jd.IsArray()) {
				std::string eventType = jd.HasMember("event") ? jd["event"].GetString() : "unknown";

				if (eventType == "systemStatus") {
				  //{"connectionID":18119784074770833087,"event":"systemStatus","status":"online","version":"0.1.1"}				  
				} else if (eventType == "subscriptionStatus") {
				  //{"channelID":1,"event":"subscriptionStatus","pair":"XBT/USD","status":"subscribed","subscription":{"name":"trade"}}
				  std::string status = jd["status"].GetString();
				  if (status == "subscribed") {
					 uint64_t channelID = jd["channelID"].GetUint64();
					 std::string pair = jd["pair"].GetString();
					 pair += ".KR";
					 const Value& subscription = jd["subscription"];
					 std::string subType = subscription["name"].GetString();
					 std::pair <std::string, std::string> pairType = std::make_pair(pair, subType);
					 context->_krakenChannelToPairType.insert(std::make_pair(channelID, pairType));
					 if (subType == "book") {
						context->_bookMap->insert(std::make_pair(pair, std::make_shared<BookType>())); 
					 }
				  } else if (status == "error") {
					 context->_consoleLogger->error("{0}", jsonDoc);
				  } else {
					 context->_consoleLogger->error("{0}", jsonDoc);
					 assert(false);
				  }
				} else if (eventType == "heartbeat") {
				  //{"event":"heartbeat"}
				} else if (jd.HasMember("Error")) {
				  context->_consoleLogger->error("{0}", jsonDoc);
				} else {
				  std::cerr << jsonDoc << std::endl;
				  assert(false);
				}
			 } else {
				int channelId = jd[0].GetInt();
				auto p = context->_krakenChannelToPairType.find(channelId);
				assert(p != context->_krakenChannelToPairType.end());
				std::string &pair = (*p).second.first;
				std::string &type = (*p).second.second;
				if (type == "ohlc") {
				  // nop
				} else if (type == "spread") {
				  // nop
				} else if (type == "trade") {
				  std::shared_ptr<BookType> book = (*context->_bookMap)[pair];
				  assert(book);

				  const Value& trades = jd[1];
				  for (SizeType i = 0; i < trades.Size(); ++i) {
					 const char * px = trades[i][0].GetString();
					 const char * qty = trades[i][1].GetString();
					 //					 uint64_t ipx = atof(px) * 100000000;
					 //uint64_t iqty = atof(qty) * 100000000;
					 char pub[1024];
					 size_t len = ::snprintf(pub, 1024, "Trade %s 0 %s 0 %s", pair.c_str(), px, qty);
					 WebSocketManagerType::WriteFrame(context->_publishStreamSP, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
					 context->_publishStreamSP->Push(pub, len);
					 context->_wsAnonManager->SetWriteAll();
				  }
				} else if (type == "ticker") {
				  // nop
				} else if (type == "heartbeat") {
				  // nop
				} else if (type == "book") {
				  std::shared_ptr<BookType> book = (*context->_bookMap)[pair];
				  assert(book);
				  //				  std::cout << jsonDoc << std::endl;
				  
				  const Value& snap = jd[1];
				  if (snap.HasMember("as")) {
					 const Value& levels = snap["as"];

					 for (SizeType i = 0; i < levels.Size(); ++i) {
						const char * px = levels[i][0].GetString();
						const char * qty = levels[i][1].GetString();
						uint64_t ipx = atof(px) * 100000000;
						uint64_t iqty = atof(qty) * 100000000;
						int outindex = -1;
						book->InsertAsk(ipx, iqty, outindex);
					 }
				  }
				  if (snap.HasMember("a")) {
					 const Value& levels = snap["a"];

					 for (SizeType i = 0; i < levels.Size(); ++i) {
						const char * px = levels[i][0].GetString();
						const char * qty = levels[i][1].GetString();
						uint64_t ipx = atof(px) * 100000000;
						uint64_t iqty = atof(qty) * 100000000;
						int outindex = -1;
						
						if (iqty == 0) {
						  book->EraseAsk(ipx, outindex);
						} else {
						  if (!book->UpdateAsk(ipx, iqty, outindex)) {
							 book->InsertAsk(ipx, iqty, outindex);
						  }
						}
					 }
				  }

				  if (snap.HasMember("bs")) {
					 const Value& levels = snap["bs"];

					 for (SizeType i = 0; i < levels.Size(); ++i) {
						const char * px = levels[i][0].GetString();
						const char * qty = levels[i][1].GetString();
						uint64_t ipx = atof(px) * 100000000;
						uint64_t iqty = atof(qty) * 100000000;
						int outindex = -1;
						book->InsertBid(ipx, iqty, outindex);
					 }
				  }
				  if (snap.HasMember("b")) {
					 const Value& levels = snap["b"];

					 for (SizeType i = 0; i < levels.Size(); ++i) {
						const char * px = levels[i][0].GetString();
						const char * qty = levels[i][1].GetString();
						uint64_t ipx = atof(px) * 100000000;
						uint64_t iqty = atof(qty) * 100000000;
						int outindex = -1;
						
						if (iqty == 0) {
						  ///std::cout << "bid ask " << ipx << std::endl;
						  book->EraseBid(ipx, outindex);
						  //assert(outindex != -1);
						} else {
						  if (!book->UpdateBid(ipx, iqty, outindex)) {
							 book->InsertBid(ipx, iqty, outindex);
						  }
						}
					 }
				  }

				  // publish kraken
				  CoinLevel bid,ask;
				  book->BestBid(bid);
				  book->BestAsk(ask);
				  char pub[1024];
				  size_t len = ::snprintf(pub, 1024, "Tick %s %zu %zu %zu %zu", pair.c_str(), 
												  bid.qty, bid.px, ask.px, ask.qty);
				  WebSocketManagerType::WriteFrame(context->_publishStreamSP, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
				  context->_publishStreamSP->Push(pub, len);
				  context->_wsAnonManager->SetWriteAll();
				} else {
				  std::cerr << "unsupported " << type << std::endl;
				  assert(false);
				}
			 }
		  }
		}
	 }
  };
		
  // stream is associated with the fd. socket can only support one websocket connection at a time.
  std::function<int(int)> wsReadCB = std::bind(&WebSocketManagerType::Read, contextSP->_wsManager, std::placeholders::_1);
  std::function<int(int)> wsWriteCB = std::bind(&WebSocketManagerType::Write, contextSP->_wsManager, std::placeholders::_1);
  std::function<int(int)> wsCloseCB = std::bind(&WebSocketManagerType::Unregister, contextSP->_wsManager, std::placeholders::_1);
		
  contextSP->_wsManager->RegisterConnection(wsFD, false, sslReadCB, sslWriteCB, onOpen, onText, contextSP->_gdaxStreamSP, nullptr);	
  contextSP->_eventMgr->Register(wsFD, wsReadCB, wsWriteCB, closeSSL); // no race here as long as we dont call stream

  // Sets the end-point
  char uri[1024];
  snprintf(uri, 1024, "http://%s", hostname.c_str());
  contextSP->_wsManager->Stream(wsFD, "/", hostname, uri);
}

int main(int argc, char **argv)
{
  //processRust(10);

	static_assert(sizeof(CoinCache) == 128, "CoinCache Size Check");

	if (argc != 2) {
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
	  fprintf(stderr, "RAND_load_file fail.\n"); 
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
	(void)c;
	
	auto consoleLogger = std::make_shared<coypu::SPDLogger>(console);

	auto contextSP = std::make_shared<CoypuContext>(consoleLogger, wsLogger);
	contextSP->_eventMgr->Init(); // needs to happens before cb manager so we can register the queue.

	contextSP->_cbManager = CreateCBManager<CBType, EventManagerType>(contextSP);


	CreateStores(config, contextSP);

	// Init event manager

	// BEGIN Signal
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
	// END Signal

	// BEGIN Create websocket service
	std::string interface;
	config->GetValue("interface", interface);
	assert(!interface.empty());
	int sockFD = BindAndListen(consoleLogger, interface, 8080);
	if (sockFD > 0) {
	  std::weak_ptr <CoypuContext> wContextSP = contextSP;

	  coypu::event::callback_type acceptCB = [wContextSP, logger=consoleLogger](int fd) {
		 auto context = wContextSP.lock();
		 if (context) {
			AcceptWebsocketClient(context, logger, fd);
		 }
		 return 0;
	  };
	  
	  if (contextSP->_eventMgr->Register(sockFD, acceptCB, nullptr, nullptr)) {
		 consoleLogger->perror(errno, "Register");
	  }
	} else {
	  consoleLogger->error("Failed to create websocket fd");
	}
	// END Websocket service

	bool doCB = false;

	// GDAX BEGIN
	config->GetValue("do-gdax", doCB);
	if (doCB) {
	  std::weak_ptr<CoypuContext> wContext = contextSP;
	  std::vector <std::string> symbolList;
	  std::vector <std::string> channelList;
	  config->GetSeqValues("gdax-symbols", symbolList);
	  config->GetSeqValues("gdax-channels", channelList);
  
	  std::function<void(void)> cb = [wContext, symbolList, channelList] () -> void {
		 auto contextSP = wContext.lock();
		 if (contextSP) {
			std::string gdax_hostname = "ws-feed.pro.coinbase.com";
			uint32_t gdax_port = 443;
			auto consoleLogger = spdlog::get("console");
			assert(consoleLogger);
			if (consoleLogger) {
			  consoleLogger->info("Reconnect GDAX {0}:{1}", gdax_hostname, gdax_port);
			}
			StreamGDAX(contextSP, gdax_hostname, gdax_port, symbolList, channelList);
		 }
	  };
	  contextSP->_cbManager->Register(CE_WS_CONNECT_GDAX, cb);

	  /// fire to start
	  contextSP->_cbManager->Queue(CE_WS_CONNECT_GDAX);
	}
	// GDAX END

	// Kracken BEGIN
	config->GetValue("do-kraken", doCB);
	if (doCB) {
	  std::weak_ptr<CoypuContext> wContext = contextSP;
	  std::vector <std::string> symbolList;
	  config->GetSeqValues("kraken-symbols", symbolList);
	  std::string kraken_hostname;
	  config->GetValue("kraken-host", kraken_hostname);
	  assert(!kraken_hostname.empty());
  
	  std::function<void(void)> cb = [wContext, symbolList, kraken_hostname] () -> void {
		 auto contextSP = wContext.lock();
		 if (contextSP) {
			uint32_t kraken_port = 443;
			auto consoleLogger = spdlog::get("console");
			assert(consoleLogger);
			if (consoleLogger) {
			  consoleLogger->info("Reconnect Kraken {0}:{1}", kraken_hostname, kraken_port);
			}
			StreamKraken(contextSP, kraken_hostname, kraken_port, symbolList);
		 }
	  };
	  contextSP->_cbManager->Register(CE_WS_CONNECT_KRAKEN, cb);

	  /// fire to start
	  contextSP->_cbManager->Queue(CE_WS_CONNECT_KRAKEN);
	}
	// Kracken END
	
	
	config->GetValue("do-server-test", doCB);
	if (doCB) DoServerTest(contextSP);
	
	SetupAdmin(interface, contextSP);

	// watch out for threading on loggers
	std::thread t1(bar, contextSP, std::ref(done));
	t1.join();
	contextSP->_eventMgr->Close();

	google::protobuf::ShutdownProtobufLibrary();
	
	return 0;
}

