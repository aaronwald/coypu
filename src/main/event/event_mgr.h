#ifndef __COYPU_EVENTMGR_H
#define __COYPU_EVENTMGR_H

#include <iostream>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <vector>
#include <memory>
#include <functional>
#include <sys/epoll.h>
#include <deque>

#include "event_hlpr.h"

namespace  coypu
{
    namespace event 
    {
        typedef std::function <int (int)> callback_type;
        template <typename LogTrait>
        class EventManager {
            public:
                EventManager (LogTrait logger) : _fd(0), _logger(logger),
                _timeout(1000), _maxEvents(16), _outEvents(nullptr) {
                    _closeList.reserve(_maxEvents);
                    _outEvents = reinterpret_cast<struct epoll_event *>(malloc(sizeof(struct epoll_event) * _maxEvents));
                }

                virtual ~EventManager () {
                    if (_outEvents) {
                        delete _outEvents;
                        _outEvents = nullptr;
                    }
                }

                int Init () {
                    _fd = coypu::event::EPollHelper::Create();
                    if (_fd < 0) {
                        _logger->perror(errno, "Failed to create epoll");
                        return -1;
                    }
                    return 0;
                }

                int Close () {
                    if (_fd > 0) {
                        ::close(_fd);
                    }

                    return 0;
                }

                int Register (int fd, callback_type read_func, callback_type write_func, callback_type close_func) {
						if (fd <=0) {
						  assert(false);
						  return -1;
						}
                    struct epoll_event event;
                    event.events = EPOLLIN | EPOLLRDHUP | EPOLLPRI;// always | EPOLLERR | EPOLLHUP;

                    auto cb = std::make_shared<event_cb_type>();
                    cb->_cf = close_func;
                    cb->_wf = write_func;
                    cb->_rf = read_func;
                    cb->_fd = fd;
                    _fdToCB.resize(fd+1);
                    assert(fd < _fdToCB.capacity());
                    _fdToCB[fd] = cb;

                    event.data.fd = fd;

                    int r =  EPollHelper::Add(_fd, fd, &event);
                    if (r != 0) {
                        Unregister(fd); // cleanup
                    }
                    return r;
                }

                int SetWrite (int fd) {
                    struct epoll_event event;
                    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLPRI; // always | EPOLLERR | EPOLLHUP;
                    event.data.fd = fd;
                    return EPollHelper::Modify(_fd, fd, &event);
                }

                int ClearWrite (int fd) {
                    struct epoll_event event;
                    event.events = EPOLLIN | EPOLLRDHUP | EPOLLPRI; // always | EPOLLERR | EPOLLHUP;
                    event.data.fd = fd;
                    return EPollHelper::Modify(_fd, fd, &event);
                }


                int Unregister (int fd) {
                    int r = EPollHelper::Delete(_fd, fd); 
                    
                    if (r == 0) {
                        _fdToCB[fd] = nullptr;
                    }
                    return r;
                }

                int Wait () {
                    int count = ::epoll_wait(_fd, _outEvents, _maxEvents, _timeout);
                    if (count > 0) {
                        if (count == _maxEvents) {
                            _logger->warn("Hit epoll _maxEvents [{0}].", _maxEvents);
                        }
                        for (int i = 0; i < count; ++i) {
                            std::shared_ptr<event_cb_type> &cb = _fdToCB[_outEvents[i].data.fd];
                            assert(cb);

                            if (_outEvents[i].events & (EPOLLIN|EPOLLPRI)) {
                                if (cb->_rf) {
                                    // ret < 0 : close                                    
                                    int ret = cb->_rf(cb->_fd);
                                    if (ret < 0) {
                                        _closeList.push_back(cb->_fd);
                                    }
                                }
                            } 

                            if (_outEvents[i].events & EPOLLOUT) {
                                if (cb->_wf) {
                                    // ret < 0 : close
                                    // ret 0 : clear
                                    // ret > 0 : keep EPOLLOUT bit set
                                    int ret = cb->_wf(cb->_fd);
                                    if (ret < 0) {
                                        _closeList.push_back(cb->_fd);
                                    } if (ret == 0) {
                                        ClearWrite(cb->_fd);
                                    }
                                }
                            }   
                            
                            if (_outEvents[i].events & (EPOLLHUP|EPOLLERR|EPOLLRDHUP)) {
                                if (cb->_cf) {
                                    cb->_cf(cb->_fd);
                                }
                                _closeList.push_back(cb->_fd);
                            } 
                        }

                        for (int i = 0; i < _closeList.size(); ++i) {
                            Unregister(_closeList[i]);
                        }

                        _closeList.clear();
                    } else if (count < 0) {
							 if (errno == EINTR) {
								_logger->perror(errno, "epoll_wait");
								return 0;
							 } else {
								_logger->perror(errno, "epoll_wait");
							 }
                    }
                    return count;
                }

            private:
                EventManager (const EventManager &other) = delete;
                EventManager &operator= (const EventManager &other) = delete;
                EventManager (const EventManager &&other) = delete;
                EventManager &operator= (const EventManager &&other) = delete;

                typedef struct EventCB {
                    callback_type _rf;
                    callback_type _wf;
                    callback_type _cf;
                    int _fd;
                } event_cb_type;

                std::vector <std::shared_ptr<event_cb_type>> _fdToCB;

                int _fd;
                LogTrait _logger;

                int _timeout;
                int _maxEvents;
                struct epoll_event * _outEvents;
                std::vector <int> _closeList;

        };

		  template <typename CBType>
			 class EventCBManager {
		  public:
			 typedef std::function<int(int)> write_cb_type;
			 typedef std::deque<uint64_t> queue_type;

			 // fd should be eventfd()
		  EventCBManager(int fd, write_cb_type set_write) : _fd(fd), _set_write(set_write) {
			 }

			 virtual ~EventCBManager() {
			 }

			 bool Register (uint64_t type, CBType &cb) {
				return _cbMap.insert(std::make_pair(type, cb)).second;
			 }

			 bool Unregister (uint64_t type) {
				auto b = _cbMap.find(type);
				if (b != _cbMap.end()) {
				  _cbMap.erase(b);
				  return true;
				}
				return false;
			 }

			 int Read (int fd) {
				uint64_t u = UINT64_MAX;
				int r = ::read(_fd, &u, sizeof(uint64_t));
				if (r > 0) {
				  assert(r == sizeof(uint64_t));
				  if (r < sizeof(uint64_t)) return -128;

				  while (!_queue.empty()) {
					 u = _queue.front(); // not thread safe
					 _queue.pop_front();
					 auto b = _cbMap.find(u);
					 if (b != _cbMap.end()) {
						(*b).second();
					 } else {
						assert(false);
					 }
				  }
				  return 0;
				}
				return r;
			 }

			 int Write (int fd) {
				// write queue
				uint64_t u = _queue.size();
				int r = ::write(_fd, &u, sizeof(uint64_t));
								
				if (r > 0) {
				  assert(r == sizeof(uint64_t));
				  if (r < sizeof(uint64_t)) return -128;
				  return 0;
				}
				assert(false);
				
				return -1;
			 }

			 int Close (int fd) {
				// nop ?? error 
				return -1;
			 }

			 void Queue (uint64_t u) {
				_queue.push_back(u);
				_set_write(_fd);
			 }

		  private:
			 EventCBManager (const EventCBManager &other) = delete;
			 EventCBManager &operator= (const EventCBManager &other) = delete;
			 EventCBManager (const EventCBManager &&other) = delete;
			 EventCBManager &operator= (const EventCBManager &&other) = delete;

			 queue_type _queue;
			 
			 typedef std::unordered_map <uint64_t, CBType> cb_map_type;
			 cb_map_type _cbMap;
			 
			 int _fd;
			 write_cb_type _set_write;
		  };
    } // event
} //  coypu


#endif
