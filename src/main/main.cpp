#include <stdio.h>
#include <signal.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <sys/uio.h>

#include <memory>
#include <vector>
#include <iostream>
#include <unordered_map>
#include <thread>

#include <openssl/rand.h>

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

using namespace coypu;
using namespace coypu::backtrace;
using namespace coypu::config;
using namespace coypu::event;
using namespace coypu::tcp;
using namespace coypu::net::ssl;
using namespace coypu::mem;
using namespace coypu::file;
using namespace coypu::store;

// Coypu Types
typedef std::shared_ptr<SPDLogger> LogType;
typedef coypu::event::EventManager<LogType> EventManagerType;
typedef coypu::store::LogRWStream<FileUtil, coypu::store::LRUCache, 128> RWBufType;
typedef coypu::store::PositionedStream <RWBufType> StreamType;
typedef coypu::store::MultiPositionedStreamLog <RWBufType> PublishStreamType;
typedef coypu::http::websocket::WebSocketManager <LogType, StreamType, PublishStreamType> WebSocketManagerType;
typedef LogWriteBuf<FileUtil> StoreType;

void bar (std::shared_ptr<EventManagerType> eventMgr, bool &done) {
	CPUManager::SetName("epoll");

	while (!done) {
		if(eventMgr->Wait() < 0) {
			done = true;
		}
	}
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <yaml_config>\n", argv[0]);
		exit(1);
	}

	int r = CPUManager::RunOnNode(0);
	if (r) {
		fprintf(stderr, "Numa failed %d\n", r);
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
	std::shared_ptr<PublishStreamType> publishStreamSP = nullptr; 
	std::string publishFile("publish.store");
	if (!publishFile.empty()) {
		bool b = false;
		FileUtil::Exists(publishFile.c_str(), b);
		// open in direct mode
		int fd = FileUtil::Open(publishFile.c_str(), O_CREAT|O_LARGEFILE|O_RDWR|O_DIRECT, 0600);
		if (fd >= 0) {
			size_t pageSize = MemManager::GetPageSize();
			off64_t curSize = 0;
			FileUtil::GetSize(fd, curSize);
			a->info("Current size {0}", curSize);

			std::shared_ptr<RWBufType> publishBufSP = std::make_shared<RWBufType>(pageSize, curSize, fd);
			publishStreamSP = std::make_shared<PublishStreamType>(publishBufSP);
		} else {
			a->perror(errno, "Open");
		}
	}
	std::weak_ptr<PublishStreamType> wPublishStreamSP = publishStreamSP; 

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
				// eth to usd ticker
				std::string subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"ticker\", \"product_ids\": [\"BTC-USD\", \"ETH-USD\"]}]}";
				bool queue = wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());

				subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"heartbeat\", \"product_ids\": [\"BTC-USD\", \"ETH-USD\"]}]}";
				queue = wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());

				subStr= "{\"type\": \"subscribe\", \"channels\": [{\"name\": \"level2\", \"product_ids\": [\"BTC-USD\", \"ETH-USD\"]}]}";
				queue = wsManager->Queue(fd, coypu::http::websocket::WS_OP_TEXT_FRAME, subStr.c_str(), subStr.length());
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

		// not weak
		std::function <void(uint64_t, uint64_t)> onText = [&console, wPublishStreamSP, wWsManager] (uint64_t offset, off64_t len) {
			static uint64_t seqNum = 0;
			++seqNum;
			if (!(seqNum % 1000)) {
				console->info("onText {0} {1} SeqNum[{2}]  ", len, offset, seqNum);
			}

			auto wsManager = wWsManager.lock();
			auto publish = wPublishStreamSP.lock();
			if (publish && wsManager) {
				// Create a websocket message and persist
				char pub[1024];
				size_t len = ::snprintf(pub, 1024, "Seqno [%zu]", seqNum);
				WebSocketManagerType::WriteFrame(publish, coypu::http::websocket::WS_OP_TEXT_FRAME, false, len);
				publish->Push(pub, len);
				wsManager->SetWriteAll();
			}
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
				a->info("Current size {0}", curSize);

				std::shared_ptr<RWBufType> bufSP = std::make_shared<RWBufType>(pageSize, curSize, fd);
				streamSP = std::make_shared<StreamType>(bufSP);
			} else {
				a->perror(errno, "Open");
			}
		}

		
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

	// watch out for threading on logggers
	std::thread t1(bar, eventMgr, std::ref(done));
	t1.join();
	eventMgr->Close();

	return 0;
}
