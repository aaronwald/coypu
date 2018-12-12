#ifndef __COYPU_ADMIN_H
#define __COYPU_ADMIN_H

#include <functional>
#include "buf/buf.h"

namespace coypu {
    namespace admin {
        class AdminManager {
            public:
                typedef std::function<void(const std::string &cmd)> callback_type;

                AdminManager () {
                }

                virtual ~AdminManager () {
                }

                bool RegisterConnection (int fd) {
                    return false;
                }

                bool UnregisterConnection (int fd) {
                    return false;

                }

                bool RegisterCommand (const std::string &name, callback_type cb) {
                    return false;
                }

                // read (fd - buf)
                // when new line, fire command callback
                // connection to buf 

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

        };
    }
}

#endif
