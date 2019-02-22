#ifndef __COYPU_ADMIN_H
#define __COYPU_ADMIN_H

#include <functional>
#include <unordered_map>
#include <memory>
#include <iostream>
#include "buf/buf.h"
#include "util/string-util.h"

namespace coypu {
    namespace admin {
        template <typename LogTrait>
        class AdminManager {
            public:
                typedef std::function<void(const std::vector<std::string> &cmd)> callback_type;
                typedef std::function<int(int)> write_cb_type;

                AdminManager (LogTrait logger, 
                                write_cb_type set_write) noexcept : _logger(logger),
                                _capacity(64*1024), _set_write(set_write)  {
                    }

                virtual ~AdminManager () {
                }

                bool Register(int fd, 
                                std::function<int(int,const struct iovec *,int)> &readv,
                                std::function<int(int,const struct iovec *,int)> &writev
                ) {
                    auto sp = std::make_shared<con_type>(fd, _capacity, readv, writev); // 32k capacity
                    auto p = std::make_pair(fd, sp);
                    return _connections.insert(p).second;
                }

                int Unregister (int fd) {
                    auto x = _connections.find(fd);
                    if (x == _connections.end()) return -1;
                    // std::shared_ptr<con_type> &con = (*x).second;
                    _connections.erase(fd);
                    return 0;
                }

                bool RegisterCommand (const std::string &name, callback_type cb) {
                    if (_commands.find(name) == _commands.end()) {
                        _commands.insert(std::make_pair(name, cb));
                        return true;
                    }
                    return false;
                }

                int Read (int fd) {
                    auto x = _connections.find(fd);
                    if (x == _connections.end()) return -1;
                    std::shared_ptr<con_type> &con = (*x).second;
                    if (!con) return -2;

                    int r = con->_readBuf->Readv(fd, con->_readv);
                    uint64_t offset = 0;
                    if (con->_readBuf->Find('\n', offset)) {
                        char buf[1024*32];
                        if (offset+1 > sizeof(buf)) return -1;
                        if (con->_readBuf->Pop(buf, offset+1)) {
                            buf[offset] = 0;
                            std::string s(buf);
									 coypu::util::StringUtil::Trim(s);
                            std::vector<std::string> tokens;
                            coypu::util::StringUtil::Split(s, ' ', tokens);
                            if (tokens.size() > 0) {
                                auto b = _commands.find(tokens[0]);
                                if (b != _commands.end()) {
                                    (*b).second(tokens);
                                }
                            }
                            // con->_writeBuf->Push('f');
                            // con->_writeBuf->Push('o');
                            // con->_writeBuf->Push('o');
                            // con->_writeBuf->Push('\r');
                            // con->_writeBuf->Push('\n');
                            // _set_write(fd);
                        }
                    }

                    return r;
                }

                int Write (int fd) {
                    auto x = _connections.find(fd);
                    if (x == _connections.end()) return -1;
                    std::shared_ptr<con_type> &con = (*x).second;
                    if (!con) return -2;
                    
                    int ret = con->_writeBuf->Writev(fd, con->_writev);
                    if (ret < 0) return ret; // error

                    // We could have EAGAIN/EWOULDBLOCK so we want to maintain write if data available
                    // 0 will clear write bit
                    // Can improve branching here if we just return is empty directly on the stack without another call
                    return con->_writeBuf->IsEmpty() ? 0 : 1;
                }
                

            private:
                AdminManager (const AdminManager &other);
                AdminManager &operator= (const AdminManager &other);

                typedef struct WebSocketConnection {
                        int _fd;
                        std::shared_ptr<coypu::buf::BipBuf <char, uint64_t>> _readBuf;
                        std::shared_ptr<coypu::buf::BipBuf <char, uint64_t>> _writeBuf;
                        std::function<int(int,const struct iovec *,int)> _readv;
                        std::function<int(int,const struct iovec *,int)> _writev;
                        char * _readData;
                        char * _writeData;

                        WebSocketConnection (int fd, 
                                            int capacity,
                                            std::function<int(int,const struct iovec *,int)> readv,
                                            std::function<int(int,const struct iovec *,int)> writev ) :
                            _fd(fd), _readv(readv), _writev(writev){ 
                            _readData = new char[capacity];
                            _writeData = new char[capacity];
                            _readBuf = std::make_shared<coypu::buf::BipBuf <char, uint64_t>>(_readData, capacity);
                            _writeBuf = std::make_shared<coypu::buf::BipBuf <char, uint64_t>>(_writeData, capacity);
                        }

                        virtual ~WebSocketConnection () {
                            if (_readData) delete [] _readData;
                            if (_writeData) delete [] _writeData;
                        }
                    } con_type;

                typedef std::unordered_map<int, std::shared_ptr<con_type>> con_map_type;
                typedef std::unordered_map<std::string, callback_type> cmd_map_type;

                LogTrait _logger;
                uint64_t _capacity;
                write_cb_type _set_write;
                con_map_type _connections;
                cmd_map_type _commands;
        };
    }
}

#endif
