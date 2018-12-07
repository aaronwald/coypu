#include <stdio.h>
#include <signal.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/uio.h>
#include <time.h>

#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <thread>

#include <openssl/rand.h>

#include <iomanip>
#include <sstream>
#include <nlohmann/json.hpp>


#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/basic_file_sink.h" // support for basic file logging

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
#include "cache/cache.h"
#include "book/level.h"

using json = nlohmann::json;

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

struct CoinLevel {
  uint64_t px;
  uint64_t qty;
  CoinLevel *next, *prev;

  CoinLevel (uint64_t px, uint64_t qty) : px(px), qty(qty), next(nullptr), prev(nullptr) {
  }

  CoinLevel () : px (UINT64_MAX), qty(UINT64_MAX), next(nullptr), prev(nullptr) {
  }

  bool operator()(const CoinLevel &lhs, const CoinLevel &rhs) const {
	 return lhs.px < rhs.px;
  }

  bool operator()(const CoinLevel *lhs, const CoinLevel *rhs) const {
	 return lhs->px < rhs->px;
  }
} __attribute__((packed, aligned(64))) ;



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
typedef coypu::store::LogRWStream<FileUtil, coypu::store::LRUCache, 128> RWBufType;
typedef coypu::store::PositionedStream <RWBufType> StreamType;
typedef coypu::store::MultiPositionedStreamLog <RWBufType> PublishStreamType;
typedef coypu::http::websocket::WebSocketManager <LogType, StreamType, PublishStreamType> WebSocketManagerType;
typedef LogWriteBuf<FileUtil> StoreType;
typedef SequenceCache<CoinCache, 128, PublishStreamType, void> CacheType;

const std::string COYPU_PUBLISH_PATH = "stream/publish/data";
const std::string COYPU_CACHE_PATH = "stream/cache/data";

void bar (std::shared_ptr<EventManagerType> eventMgr, bool &done) {
	CPUManager::SetName("epoll");

	while (!done) {
		if(eventMgr->Wait() < 0) {
			done = true;
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
		snprintf(storeFile, PATH_MAX, "%s.%09d.store", name.c_str(), index);
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
				size_t pageSize = MemManager::GetPageSize();
				off64_t curSize = 0;
				FileUtil::GetSize(fd, curSize);

				std::shared_ptr<BufType> bufSP = std::make_shared<BufType>(pageSize, curSize, fd);
				streamSP = std::make_shared<StreamType>(bufSP);
			} else {
				return nullptr;
			}
			return streamSP;
		}
	}
	return nullptr;
}

int main(int argc, char **argv)
{
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
	OpenSSLManager::Init();
	int rc = RAND_load_file("/dev/urandom", 32); // /dev/random can be slow
	if(rc != 32) {
		fprintf(stderr, "RAND_load_file fail.\n"); // ERR_*
		exit(1);
	}

	// Block all signals - wait til random is collected
	SignalFDHelper::BlockAllSignals();

	// Default console logger
	auto console = spdlog::stdout_color_st("console");
	auto error_logger = spdlog::stderr_color_st("stderr");
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
	
	// app
	CoypuApplication &c = CoypuApplication::instance();
	c.foo();

	auto a = std::make_shared<coypu::SPDLogger>(console);

	auto eventMgr = std::make_shared<EventManagerType>(a);
	eventMgr->Init();

    sigset_t mask;
	::sigemptyset(&mask);
	::sigaddset(&mask, SIGINT);
	::sigaddset(&mask, SIGKILL);
	::sigaddset(&mask, SIGQUIT);
	int signalFD = SignalFDHelper::CreateNonBlockSignalFD(&mask);
	if (signalFD == -1) {
		a->perror(errno, "CreateNonBlockSignalFD");
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

	if (eventMgr->Register(signalFD, readCB, nullptr, nullptr)) {
		a->perror(errno, "Register");
	}

	int sockFD = TCPHelper::CreateIPV4NonBlockSocket();
	if (sockFD < 0) {
		a->perror(errno, "CreateIPV4NonBlockSocket");
	}

	if (TCPHelper::SetReuseAddr(sockFD) < 0 ) {
		a->perror(errno, "SetReuseAddr");
	}

	const char * interface = "enp0s3";
	struct sockaddr_in interface_in;
	int ret = TCPHelper::GetInterfaceIPV4FromName (interface, strlen(interface), interface_in);
	if (ret) {
		a->perror(errno, "GetInterfaceIPV4FromName");
		exit(1);
	}

	struct sockaddr_in serv_addr= {0}; //v4 family

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY; //interface_in.sin_addr.s_addr;
	serv_addr.sin_port = htons(8080);

	ret = TCPHelper::BindIPV4(sockFD, &serv_addr);
	if (ret != 0) {
		a->perror(errno, "BindIPV4");
	}

	ret = TCPHelper::Listen(sockFD, 16);
	if (ret != 0) {
		a->perror(errno, "Listen");
	}

	std::shared_ptr<StoreType> store;
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
			if (x) {
				wsLogger = std::make_shared<SPDLogger>(x);
			}
		}

		CPUManager::SetName("websocket");
		if (!cpuStr.empty()) {
			int r = CPUManager::SetCPUs(cpuStr);
			assert(r == 0);
		}
	}
	assert(wsLogger);

	std::function<int(int)> set_write_ws = std::bind(&EventManagerType::SetWrite, eventMgr, std::placeholders::_1);
	std::shared_ptr<WebSocketManagerType> wsManager = std::make_shared<WebSocketManagerType>(wsLogger, set_write_ws);
	std::weak_ptr<WebSocketManagerType> wWsManager = wsManager;
	std::weak_ptr <EventManagerType> wEventMgr = eventMgr;

	std::function <int(int,const struct iovec*, int)> readvCB = [] (int fd, const struct iovec *iovec, int c) {
		return ::readv(fd, iovec, c);
	};

	std::function <int(int,const struct iovec*, int)> writevCB = [] (int fd, const struct iovec *iovec, int c) {
		return ::writev(fd, iovec, c);
	};

	// BEGIN Websocket Server Test

	std::shared_ptr<PublishStreamType> publishStreamSP = CreateStore<PublishStreamType, RWBufType>(COYPU_PUBLISH_PATH); 
	assert(publishStreamSP != nullptr);
	std::weak_ptr<PublishStreamType> wPublishStreamSP = publishStreamSP; 


	
	std::shared_ptr<PublishStreamType> cacheStreamSP = CreateStore<PublishStreamType, RWBufType>(COYPU_CACHE_PATH); 
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

	// coinCache->Dump(std::cout);
	console->info("Cache check seqnum[{0}]", coinCache->CheckSeq());
	coypu::event::callback_type acceptCB = [wPublishStreamSP, wEventMgr, wWsManager, readvCB, writevCB](int fd) {
		struct sockaddr_in client_addr= {0};
		socklen_t addrlen= sizeof(sockaddr_in);

		// using IP V4
		int clientfd = TCPHelper::AcceptNonBlock(fd, reinterpret_cast<struct sockaddr *>(&client_addr), &addrlen);
		if (TCPHelper::SetNoDelay(clientfd)) {
		}

		auto eventMgr = wEventMgr.lock();
		auto wsManager = wWsManager.lock();
		auto publish = wPublishStreamSP.lock();

		if (eventMgr && wsManager && publish) {
			publish->Register(clientfd);

			std::function<int(int)> readCB = std::bind(&WebSocketManagerType::Read, wsManager, std::placeholders::_1);
			std::function<int(int)> writeCB = std::bind(&WebSocketManagerType::Write, wsManager, std::placeholders::_1);
			std::function<int(int)> closeCB = [wWsManager, wPublishStreamSP] (int fd) {
				auto wsManager = wWsManager.lock();
				if (wsManager) {
					wsManager->Unregister(fd);
				}
				auto publish = wPublishStreamSP.lock();
				if (publish) {
					publish->Unregister(fd);
				}
				return 0;
			};
			// std::bind(&WebSocketManagerType::Unregister, wsManager, std::placeholders::_1);


			wsManager->RegisterConnection(clientfd, true, readvCB, writevCB, nullptr, nullptr, nullptr, publish);	
			eventMgr->Register(clientfd, readCB, writeCB, closeCB);

			int timerFD = TimerFDHelper::CreateMonotonicNonBlock();
			TimerFDHelper::SetRelativeRepeating(timerFD, 5, 0);

			std::function<int(int)> readTimerCB = [wEventMgr, wPublishStreamSP, wWsManager, clientfd] (int fd) {
				uint64_t x;
				if (read(fd, &x, sizeof(uint64_t)) != sizeof(uint64_t)) {
					// TODO some error
					assert(false);
				}

				auto eventMgr = wEventMgr.lock();
				auto publish = wPublishStreamSP.lock();
				if (publish && eventMgr) {
					// Create a websocket message and persist
					char pub[1024];
					static int count = 0;
					size_t len = ::snprintf(pub, 1024, "Foobar [%d]", count++);
					WebSocketManagerType::WriteFrame(publish, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
					publish->Push(pub, len);
					eventMgr->SetWrite(clientfd);
				}

				return 0;
			};
			eventMgr->Register(timerFD, readTimerCB, nullptr, nullptr);
		}

		return 0;
	};

	if (eventMgr->Register(sockFD, acceptCB, nullptr, nullptr)) {
		a->perror(errno, "Register");
	}
	// END Websocket Server Test


	// ws-feed.pro.coinbase.com
	std::function<int(int)> set_write = std::bind(&EventManagerType::SetWrite, eventMgr, std::placeholders::_1);
	auto openSSLMgr = std::make_shared<OpenSSLManager>(set_write);
	std::weak_ptr<OpenSSLManager> wOpenSSLMgr = openSSLMgr;
	
	std::function<int(int)> wsReadCB = std::bind(&WebSocketManagerType::Read, wsManager, std::placeholders::_1);
	std::function<int(int)> wsWriteCB = std::bind(&WebSocketManagerType::Write, wsManager, std::placeholders::_1);
	std::function<int(int)> wsCloseCB = std::bind(&WebSocketManagerType::Unregister, wsManager, std::placeholders::_1);

	bool doCB = false;
	config->GetValue("do-gdax", doCB);
	if (doCB) {
		

		int wsFD = TCPHelper::ConnectStream("ws-feed.pro.coinbase.com", 443);

		if (wsFD < 0) {
			a->error("failed to connect to ws-feed.");
			exit(1);
		}

		openSSLMgr->Register(wsFD);

		std::function <int(int,const struct iovec*,int)> sslReadCB = std::bind(&OpenSSLManager::ReadvNonBlock, openSSLMgr, std::placeholders::_1,  std::placeholders::_2,  std::placeholders::_3);
		std::function <int(int,const struct iovec *,int)> sslWriteCB = std::bind(&OpenSSLManager::WritevNonBlock, openSSLMgr, std::placeholders::_1,  std::placeholders::_2,  std::placeholders::_3);

		std::function <void(int)> onOpen = [wWsManager] (int fd) {
			auto wsManager = wWsManager.lock();
			if (wsManager) {
				std::vector<std::string> pairs;
				//				pairs.push_back("BTC-USD");
				pairs.push_back("ETH-USD");
				//pairs.push_back("ETH-EUR");
				//pairs.push_back("ETH-BTC");
				//pairs.push_back("ZRX-USD");
				//pairs.push_back("EOS-USD");

				std::vector<std::string> channels;
				channels.push_back("ticker");
				channels.push_back("heartbeat");
				channels.push_back("level2");

				std::string subStr;
				bool queue;

				for (std::string channel : channels) {
					for (std::string pair : pairs) {
						subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"" + channel + "\", \"product_ids\": [\"" + pair + "\"]}]}";
						queue = wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());
					}
				}

				// subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"heartbeat\", \"product_ids\": [\"BTC-USD\", \"ETH-USD\"]}]}";
				// queue = wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());

				// subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"level2\", \"product_ids\": [\"BTC-USD\", \"ETH-USD\"]}]}";
				// queue = wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());
			}
		};

		std::function<int(int)> closeSSL = [wOpenSSLMgr, wWsManager] (int fd) {
			auto wsManager = wWsManager.lock();
			auto sslMgr = wOpenSSLMgr.lock();
			if (wsManager && sslMgr) {
				sslMgr->Unregister(fd);
				wsManager->Unregister(fd);
			}
			return 0;
		};

		std::string storeFile("gdax.store");
		std::shared_ptr<StreamType> streamSP = nullptr; 

		if (!storeFile.empty()) {
			bool b = false;
			FileUtil::Exists(storeFile.c_str(), b);
			// open in direct mode
			int fd = FileUtil::Open(storeFile.c_str(), O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
			if (fd >= 0) {
				size_t pageSize = MemManager::GetPageSize();
				off64_t curSize = 0;
				FileUtil::GetSize(fd, curSize);
				a->info("Current size [{0}]", curSize);

				std::shared_ptr<RWBufType> bufSP = std::make_shared<RWBufType>(pageSize, curSize, fd);
				streamSP = std::make_shared<StreamType>(bufSP);
			} else {
				a->perror(errno, "Open");
			}
		}
		std::weak_ptr<StreamType> wStreamSP = streamSP; 

		// not weak
		std::function <void(uint64_t, uint64_t)> onText = [wsFD, &console, wPublishStreamSP, wWsManager, wStreamSP, wCoinCache] (uint64_t offset, off64_t len) {
			static uint64_t seqNum = 0;
			++seqNum;
					  static LevelAllocator<CoinLevel, 4096*16> la;
					  static CLevelBook<CoinLevel> book;

			auto wsManager = wWsManager.lock();
			auto publish = wPublishStreamSP.lock();
			auto stream = wStreamSP.lock();
			auto coinCache = wCoinCache.lock();
			if (publish && wsManager && stream  && coinCache) {
				if (!(seqNum % 100)) {
					std::stringstream ss;
					ss << *coinCache;
					console->info("onText {0} {1} SeqNum[{2}] {3} ", len, offset, seqNum, ss.str());


				}

				char jsonDoc[1024*1024] = {};
				if (len < sizeof(jsonDoc)) {
					// sad copying but nice json library
					if(stream->Pop(jsonDoc, offset, len)) {
					  
						jsonDoc[len] = 0;
						json result = json::parse(jsonDoc);
						std::string &type = result["type"].get_ref<std::string &>();
						if (type == "snapshot") {
						  // bids
						  // asks
							std::string product = result["product_id"];

							std::vector<json> bids = result["bids"];
							if (product == "ETH-USD") {
							  auto b = bids.begin();
							  auto e = bids.end();
							  for(;b!=e; ++b) {
								 std::string px = (*b)[0].get<std::string>();
								 std::string qty = (*b)[1].get<std::string>();

								  uint64_t ipx = atof(px.c_str()) * 100000000;
								  uint64_t iqty = atof(qty.c_str()) * 100000000;
								  book.Insert(la.Allocate(ipx, iqty));
							  }
							}

							std::vector<json> asks = result["asks"];
							
						} else if (type == "l2update") {
							std::string product = result["product_id"];

							std::vector<json> changes = result["changes"];
							auto b = changes.begin();
							auto e = changes.end();
							for(;b!=e; ++b) {
								std::string side = (*b)[0].get<std::string>();
								std::string px = (*b)[1].get<std::string>();
								std::string qty = (*b)[2].get<std::string>();

								if (product == "ETH-USD" && side == "buy") {

								  uint64_t ipx = atof(px.c_str()) * 100000000;
								  uint64_t iqty = atof(qty.c_str()) * 100000000;
								  if (iqty == 0) {
									 // TODO Fix leak
									 CoinLevel *cl = book.Erase(ipx);
									 if (cl) {
									 }
								  } else {
									 if (!book.Update(ipx, iqty)) {
										book.Insert(la.Allocate(ipx, iqty));
									 }
								  }

								  std::cout << "---" << std::endl;
								  book.Dump(10);
								}

								// update side book for px and qty
							}
						} else if (type == "error") {
							std::stringstream s;
							s << result;
							console->error("{0}", s.str());
						} else if (type == "ticker") {
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

							// the last-value cache should be from ticker
							std::stringstream s;
							s << result;

							CoinCache cc(coinCache->NextSeq());
							if (!result["product_id"].is_null()) {
								std::string &product = result["product_id"].get_ref<std::string &>();
								memcpy(cc._key, product.c_str(), std::max(sizeof(cc._key)-1, product.length()));
							}

							if (!result["time"].is_null()) {
								std::string &timeStr = result["time"].get_ref<std::string &>();

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

							if (!result["high_24h"].is_null()) {
								cc._high24 = atof(result["high_24h"].get_ref<std::string &>().c_str());
							}

							if (!result["low_24h"].is_null()) {
								cc._low24 = atof(result["low_24h"].get_ref<std::string &>().c_str());
							}

							if (!result["volume_24h"].is_null()) {
								cc._vol24 = atof(result["volume_24h"].get_ref<std::string &>().c_str());
							}

							if (!result["sequence"].is_null()) {
								cc._origseqno = result["sequence"].get<uint64_t>();
							}
							
							//double px = atof(result["price"].get_ref<std::string &>().c_str());
							

							// WebSocketManagerType::WriteFrame(cache, coypu::http::websocket::WS_OP_BINARY_FRAME, false, sizeof(CoinCache));
							coinCache->Push(cc);
						} else if (type == "subscriptions") {
						} else if (type == "heartbeat") {
							// skip
						} else {
							std::stringstream s;
							s << result;
							console->warn("{0} {1}", type, s.str()); // spdlog does not do streams
						}
					} else {
						assert(false);
					}
				}
			

				char pub[1024];
				size_t len = ::snprintf(pub, 1024, "Seqno [%zu]", seqNum);
				WebSocketManagerType::WriteFrame(publish, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
				publish->Push(pub, len);
				wsManager->SetWriteAll();
			}
		};
		
		// stream is associated with the fd. socket can only support one websocket connection at a time.
		wsManager->RegisterConnection(wsFD, false, sslReadCB, sslWriteCB, onOpen, onText, streamSP, nullptr);	
		eventMgr->Register(wsFD, wsReadCB, wsWriteCB, closeSSL); // no race here as long as we dont call stream

		// Sets the end-point 
		wsManager->Stream(wsFD, "/", "ws-feed.pro.coinbase.com", "http://ws-feed.pro.coinbase.com"); 
	}

	// Test client
	config->GetValue("do-server-test", doCB);
	if (doCB)
	{
		int wsFD = TCPHelper::ConnectStream("localhost", 8765);
		assert(wsFD > 0);
		wsManager->RegisterConnection(wsFD, false, readvCB, writevCB, nullptr, nullptr, nullptr, nullptr);	
		eventMgr->Register(wsFD, wsReadCB, wsWriteCB, wsCloseCB);

		wsManager->Stream(wsFD, "/foo", "localhost", "http://localhost");
	}

	// watch out for threading on loggers
	std::thread t1(bar, eventMgr, std::ref(done));
	t1.join();
	eventMgr->Close();

	return 0;
}


