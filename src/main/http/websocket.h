/*
 * Created on Mon Sep 24 2018
 *
 *  Copyright (c) 2018 Aaron Wald
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __COYPU_WEBSOCKET_H
#define __COYPU_WEBSOCKET_H

#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <arpa/inet.h>
#include <endian.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include "buf/buf.h"

namespace coypu
{
    namespace http
    {
        namespace websocket {
            /* https://tools.ietf.org/html/rfc6455

            0                   1                   2                   3
            0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
            +-+-+-+-+-------+-+-------------+-------------------------------+
            |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
            |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
            |N|V|V|V|       |S|             |   (if payload len==126/127)   |
            | |1|2|3|       |K|             |                               |
            +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
            |     Extended payload length continued, if payload len == 127  |
            + - - - - - - - - - - - - - - - +-------------------------------+
            |                               |Masking-key, if MASK set to 1  |
            +-------------------------------+-------------------------------+
            | Masking-key (continued)       |          Payload Data         |
            +-------------------------------- - - - - - - - - - - - - - - - +
            :                     Payload Data continued ...                :
            + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
            |                     Payload Data continued ...                |
            +---------------------------------------------------------------+
            */
            static constexpr const char * WEBSOCKET_GUID ="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
            static constexpr char CR = 0xD;
            static constexpr char LF = 0xA;
            static constexpr int MAX_HEADER_SIZE = 8*1024;

            static constexpr int WS_PAYLOAD_16       = 126; // network byte order following - error if less than 126
            static constexpr int WS_PAYLOAD_64       = 127; // network byte order following - error if less than 2^16-1
            static constexpr int WS_MASK_LEN         = 4;
            static constexpr const char * WS_VERSION = "13";
            
            static constexpr int WS_SEC_KEY_LEN      = 16;
            static constexpr int WS_SEC_KEY_SIZE = 1+(((WS_SEC_KEY_LEN/3)+1)*4);

            static constexpr const char * HEADER_SEC_WEBSOCKET_PROTOCOL= "Sec-WebSocket-Protocol";
            static constexpr const char * HEADER_SEC_WEBSOCKET_KEY     = "Sec-WebSocket-Key";
            static constexpr const char * HEADER_SEC_WEBSOCKET_ACCEPT  = "Sec-WebSocket-Accept";
            static constexpr const char * HEADER_SEC_WEBSOCKET_VERSION = "Sec-WebSocket-Version";
            static constexpr const char * HEADER_UPGRADE               = "Upgrade";
            static constexpr const char * HEADER_CONNECTION            = "Connection";
            static constexpr const char * HEADER_HOST                  = "Host";
            static constexpr const char * HEADER_ORIGIN                = "Origin";

            static constexpr const char * HEADER_HTTP_NEWLINE          = "\r\n";
            static constexpr const size_t HEADER_HTTP_NEWLINE_LEN      = 2;
            static constexpr const char * HEADER_SERVER                = "HTTP/1.1 101 Switching Protocols\r\n";
            static constexpr const size_t HEADER_SERVER_LEN            = 34;
            static constexpr const char * HEADER_SERVER_UPGRADE        = "Upgrade: websocket\r\n";
            static constexpr const size_t HEADER_SERVER_UPGRADE_LEN    = 20;
            static constexpr const char * HEADER_SERVER_CONNECTION     = "Connection: Upgrade\r\n";
            static constexpr const size_t HEADER_SERVER_CONNECTION_LEN = 21;

            static constexpr const char WS_FIN = 0x80;
            static constexpr const char WS_OP  = 0x0F;
            static constexpr const char WS_MASK        = 0x80;
            static constexpr const char WS_PAYLOAD_LEN = 0x7F;

            enum WSOPCode {
                WS_OP_UNKNOWN      = 0x99,
                WS_OP_CONTINUATION = 0x0,
                WS_OP_TEXT_FRAME   = 0x1,
                WS_OP_BINARY_FRAME = 0x2,
                WS_OP_CLOSE        = 0x8,
                WS_OP_PING         = 0x9,
                WS_OP_PONG         = 0x10
            };

            enum WSClientState {
                WS_CS_UNKNOWN,
                WS_CS_CONNECTING,
                WS_CS_OPEN,
                WS_CS_OPEN_DATA,
                WS_CS_CLOSING,
                WS_CS_CLOSED
            };

            // ProviderType to read from underlying connection which could be regular socket (writev/readv) or SSL (SSL_Read/SSL_Write)
            template <typename LogTrait, typename StreamTrait, typename PublishTrait>
            class WebSocketManager {
                public:
                    typedef std::function<int(int)> write_cb_type;

                    WebSocketManager (LogTrait logger, 
                                        write_cb_type set_write) : _logger(logger),
                                        _capacity(64*1024), _set_write(set_write)  {
                    }

                    virtual ~WebSocketManager () {
                    }

                    bool RegisterConnection(int fd, 
                                            bool serverCon,
                                            std::function<int(int,const struct iovec *,int)> readv,
                                            std::function<int(int,const struct iovec *,int)> writev,
                                            std::function <void(int)> onOpen,
                                            std::function <void(uint64_t, uint64_t)> onText,
                                            const std::shared_ptr<StreamTrait> stream,
                                            const std::shared_ptr<PublishTrait> publish) {
                        auto sp = std::make_shared<con_type>(fd, _capacity, !serverCon, serverCon, readv, writev, onOpen, onText, stream, publish); // 32k capacity
                        auto p = std::make_pair(fd, sp);
                        sp->_state = WS_CS_CONNECTING;

                        if (sp->_masked) {
                            int rc = RAND_bytes(sp->_mask, WS_MASK_LEN);
                            if (rc != 1) {
                                _logger->error("RAND_bytes failed");
                                return false;
                            }
                        }

                        return _connections.insert(p).second;
                    }

                    int Unregister (int fd) {
                        auto x = _connections.find(fd);
                        if (x == _connections.end()) return -1;
                        std::shared_ptr<con_type> &con = (*x).second;
                        con->_state = WS_CS_CLOSED;

                        _connections.erase(fd);
                        return 0;
                    }

                    int Write (int fd) {
                        auto x = _connections.find(fd);
                        if (x == _connections.end()) return -1;
                        std::shared_ptr<con_type> &con = (*x).second;
                        if (!con) return -2;

                        // check if stream buf has data.... then send over writeBuf? probably we should have
                        // a state here but we set to open once we upgrad eon server side.
                        if (!con->_writeBuf->IsEmpty()) {
                            int ret = con->_writeBuf->Writev(fd, con->_writev); 

                            if (ret < 0) return ret; // error

                            // We could have EAGAIN/EWOULDBLOCK so we want to maintain write if data available
                            // 0 will clear write bit
                            // Can improve branching here if we just return is empty directly on the stack without another call
                            return con->_writeBuf->IsEmpty() ? 0 : 1;
                        } else if (con->_publish) {
                            // could limit size of write
                            int ret = con->_publish->Writev(con->_publish->Available(fd), fd, con->_writev);
                            if (ret < 0) return ret; // error
                            return con->_publish->IsEmpty(fd) ? 0 : 1;
                        }

                        return 0;
                    }

                    int Read (int fd) {
                        auto x = _connections.find(fd);
                        if (x == _connections.end()) return -1;
                        std::shared_ptr<con_type> &con = (*x).second;
                        if (!con) return -2;

                        
                        int r = 0;
                        if (con->_stream && (con->_state == WS_CS_OPEN || con->_state == WS_CS_OPEN_DATA)) {
                            r = con->_stream->Readv(fd, con->_readv);
                        } else {
                            r = con->_httpBuf->Readv(fd, con->_readv);
                        }

                        if (r < 0) return -3;
                        
                        if (con->_state == WS_CS_CONNECTING) {
                            int r = HandleHTTP(con);
                            if (r < 0) return r;

                        }
								
								while (ProcessState(con)) {
								}

                        return 0;
                    }

                    // TODO This is natural entry place for the store to make sure we place the streamed data into a store for a given uri.
                    // should assign a stream token here?
                    bool Stream (int fd, 
                                const std::string &uri,
                                const std::string &host, 
                                const std::string &origin) {
                        auto x = _connections.find(fd);
                        if (x == _connections.end()) return -1;

                        unsigned char randkey[WS_SEC_KEY_LEN];
                        int rc = RAND_bytes(randkey, WS_SEC_KEY_LEN);
                        if (rc != 1) {
                            _logger->error("RAND_bytes failed");
                            return false;
                        }

                        std::shared_ptr<con_type> &con = (*x).second;
                        char keyHeader[1024];
                        int r = r = snprintf(keyHeader, 1024, "GET %s HTTP/1.1\r\n", uri.c_str());
                        con->_writeBuf->Push(keyHeader, r); 

                        r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_HOST, host.c_str());
                        con->_writeBuf->Push(keyHeader, r); 

                        r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_UPGRADE, "websocket");
                        con->_writeBuf->Push(keyHeader, r); 

                        r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_CONNECTION, "Upgrade");
                        con->_writeBuf->Push(keyHeader, r); 

                        r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_ORIGIN, origin.c_str());
                        con->_writeBuf->Push(keyHeader, r); 

                        r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_SEC_WEBSOCKET_VERSION, WS_VERSION);
                        con->_writeBuf->Push(keyHeader, r); 

                        EVP_EncodeBlock(con->_key, randkey, WS_SEC_KEY_LEN);
                        r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_SEC_WEBSOCKET_KEY, con->_key);
                        con->_writeBuf->Push(keyHeader, r); 

                        con->_writeBuf->Push(HEADER_HTTP_NEWLINE, HEADER_HTTP_NEWLINE_LEN); // \r\n

                        _set_write(con->_fd);

                        return true;
                    }

                    template <typename T>
                    static bool WriteFrame (T &t,  WSOPCode code, bool masked, uint64_t len) {
                        WebSocketFrame wsFrame;  
                        uint32_t freeSpace = t->Free();

                        wsFrame._hdr[0] = 0x80 | (0x0F & code);
                        if (len <= 125) {
                            if (freeSpace < 2 + len + (masked ? 0 : 4)) return false;
                            wsFrame._hdr[1] = 0x7F & len;
                            if (masked) wsFrame._hdr[1] |= 0x80;
                            t->Push(wsFrame._hdr, 2);

                        } else if (len > 125 && len < UINT16_MAX) {
                            if (freeSpace < 4 + len + (masked ? 0 : 4)) return false;
                            wsFrame._hdr[1] = 0x7F & WS_PAYLOAD_16;
                            if (masked) wsFrame._hdr[1] |= 0x80;
                            t->Push(wsFrame._hdr, 2); // TODO This can be optimized to one Push

                            uint16_t outlen = htons((uint16_t)len);
                            t->Push(reinterpret_cast<char *>(&outlen), sizeof(uint16_t));
                        } else {
                            if (freeSpace < 10 + len + (masked ? 0 : 4)) return false;
                            wsFrame._hdr[1] = 0x7F & WS_PAYLOAD_64; 
                            if (masked) wsFrame._hdr[1] |= 0x80;
                            t->Push(wsFrame._hdr, 2); // TODO This can be optimized to one Push

                            uint64_t outlen = htobe64(len);
                            t->Push(reinterpret_cast<char *>(&outlen), sizeof(uint64_t));
                        }
                        return true;
                    }

                    // Queue data
                    bool Queue (int fd, WSOPCode code, const char *data, uint64_t len) {
                        auto x = _connections.find(fd);
                        if (x == _connections.end()) return false;

                        std::shared_ptr<con_type> &con = (*x).second;

                        if (con && (con->_state == WS_CS_OPEN || 
                                    con->_state == WS_CS_OPEN_DATA )) {
                            if(!WriteFrame(con->_writeBuf, code, con->_masked, len)) return false;

                            if (con->_masked) {
                                /// write mask 
                                con->_writeBuf->Push(reinterpret_cast<char *>(con->_mask), WS_MASK_LEN);
                                PushMask(con, data, len);
                            } else {
                                con->_writeBuf->Push(data, len);
                            }

                            _set_write(fd);
                            return true;
                        }
                        return false;
                    }

                    // hack
                    void SetWriteAll () {
                        std::for_each(_connections.begin(), _connections.end(), [this] (const std::pair<int, std::shared_ptr<con_type>> p) { _set_write(p.first); });             
                    }
                private:

                    typedef struct WebSocketFrame {
                        // char _finop;    // first octext
                        // char _masklen;  // second octext
                        char _hdr[2];

                        char _mask[WS_MASK_LEN];

                        WSOPCode _opcode; // extracted
                        bool _masked;
                        uint64_t _len;    // can be up to unsigned 64 bit in length;
                    } WebSocketFrame;

                    typedef struct WebSocketConnection {
                        int _fd;
                        std::shared_ptr<coypu::buf::BipBuf <char, uint64_t>> _httpBuf;
                        std::shared_ptr<coypu::buf::BipBuf <char, uint64_t>> _writeBuf;
                        std::shared_ptr<StreamTrait> _stream;
                        std::shared_ptr<PublishTrait> _publish;
                        std::unordered_map <std::string, std::string> _headers;
                        char *_readData;
                        char *_writeData;
                        std::string _uri;
                        std::string _method;
                        std::string _version;
                        std::string _responsecode;
                        std::string _text;
                        WSClientState _state;
                        WebSocketFrame _frame;
                        unsigned char _mask[WS_MASK_LEN];
                        bool _masked;
                        bool _server;
                        std::function<int(int,const struct iovec *,int)> _readv;
                        std::function<int(int,const struct iovec *,int)> _writev;
                        std::function <void(int)> _onOpen;
                        std::function <void(uint64_t, uint64_t)> _onText;
                        unsigned char _key[WS_SEC_KEY_SIZE] = {};

                        WebSocketConnection (int fd, uint64_t capacity, bool masked, bool server,
                                            std::function<int(int,const struct iovec *,int)> readv,
                                            std::function<int(int,const struct iovec *,int)> writev,
                                            std::function <void(int)> onOpen,
                                            std::function <void(uint64_t, uint64_t)> onText,
                                            std::shared_ptr<StreamTrait> stream,
                                            std::shared_ptr<PublishTrait> publish) :
                            _fd(fd), _stream(stream), _publish(publish), _readData(nullptr), _writeData(nullptr), 
                            _state(WS_CS_UNKNOWN), _masked(masked), _server(server), _readv(readv), _writev(writev),
                            _onOpen(onOpen), _onText(onText) { 
                            _readData = new char[capacity];
                            _writeData = new char[capacity];
                            _httpBuf = std::make_shared<coypu::buf::BipBuf <char, uint64_t>>(_readData, capacity);
                            _writeBuf = std::make_shared<coypu::buf::BipBuf <char, uint64_t>>(_writeData, capacity);
                            _mask[0] = _mask[1] = _mask[2] = _mask[3] = 0;
                        }

                        virtual ~WebSocketConnection () {
                            if (_readData) delete [] _readData;
                            if (_writeData) delete [] _writeData;
                        }

                        bool HasHeader (const std::string &hdr) const {
                            return _headers.find(hdr) != _headers.end();
                        }

                        bool GetHeader (const std::string &hdr, std::string &out) const {
                            auto i = _headers.find(hdr);
                            if (i != _headers.end()) {
                                out = (*i).second;
                                return true;
                            }
                            return false;
                        }
                    } con_type;

                    typedef std::unordered_map <int, std::shared_ptr<con_type>> map_type;

                    map_type _connections;

                    LogTrait _logger;
                    uint64_t _capacity;
                    write_cb_type _set_write;

                    static inline void Unmask (const WebSocketFrame &frame, char *data, size_t len) {
                        for (size_t i = 0; i < len; ++i) data[i] ^= frame._mask[i%WS_MASK_LEN];
                    }

                    static inline void PushMask (std::shared_ptr<con_type> &con , const char *data, size_t len) {
                        for (size_t i = 0; i < len; ++i) {
                            con->_writeBuf->Push(data[i] ^ con->_mask[i%WS_MASK_LEN]);
                        }
                    }

                    // TODO Clean up bounds checks
                    bool AddHeader(const char *header, uint32_t hdr_len, std::shared_ptr<con_type> &con) const {
                        uint32_t i = 0;
                        for (; header[i] != ':' && i < hdr_len; ++i) { }
                        if (i > 0 && i < hdr_len) {
                            std::string key = std::string(header, i);

                            int skip = header[i+1] == ' ' ? 2 : 1;
                            std::string value = std::string(&header[i+skip], hdr_len-i-1-skip);
                            return con->_headers.insert(std::make_pair(key,value)).second;
                        }
                        return false;
                    }

                    static bool ComputeKey (const std::string &in, std::string &out) {
                        SHA_CTX ctx;
                        if (!SHA1_Init(&ctx)) return false;
                        if (!SHA1_Update(&ctx, in.c_str(), in.size())) return false;
                        unsigned char sha1[SHA_DIGEST_LENGTH] = {};
                        if (!SHA1_Final(sha1, &ctx)) return false;

                        constexpr int sfsize = 1+(((SHA_DIGEST_LENGTH/3)+1)*4);
                        unsigned char base64[sfsize] = {};
                        EVP_EncodeBlock(base64, sha1, SHA_DIGEST_LENGTH);
                        out = std::string(reinterpret_cast<char *>(base64));
                        return true;
                    }

                    template <typename T>
                    int DoOpenData (T &buf, std::shared_ptr<con_type> &con) {
                        if (con->_frame._len <= buf->Available()) {
                            uint64_t offset = buf->CurrentOffset();
                            if (!buf->Skip(con->_frame._len)) {
                                _logger->error("Skip failed Len[{0}]", con->_frame._len);
                            }

                            con->_state = WS_CS_OPEN;

                            if (con->_frame._opcode == WS_OP_TEXT_FRAME) {
                                if (con->_onText) {
                                    con->_onText(offset, con->_frame._len);
                                }
                            } else if (con->_frame._opcode == WS_OP_CONTINUATION) {
                                _logger->debug("Continutation");
                            } else if (con->_frame._opcode == WS_OP_BINARY_FRAME) {
                                _logger->debug("Binary Frame");
                            } else if (con->_frame._opcode == WS_OP_PING) {
                                _logger->debug("Ping");
                            } else if (con->_frame._opcode == WS_OP_PONG) {
                                _logger->debug("Pong");
                            } else if (con->_frame._opcode == WS_OP_CLOSE) {
                                con->_state = WS_CS_CLOSING;
                            } else {
                                _logger->error("Unsupported opcode [{0}]", con->_frame._opcode);
                            }
                        } else {
                            return 0;
                        }

                        return 1;
                    }

                    template <typename T>
                    int DoOpen (T &buf,std::shared_ptr<con_type> &con) {
                        uint32_t avail =buf->Available();
                        // check if state is read header or read data (can differ if not available)
                        if (avail >= 2) {
                            char peaklen = 0;
                            if (!buf->Peak(1, peaklen)) {
                                return 0; // error
                            }

                            uint8_t len = 0x7F & peaklen;

                            uint64_t needed = 2 + (peaklen & 0x80 ? 4 : 0); // check masked

                            if (len == WS_PAYLOAD_16) {
                                needed += 2;

                            } else if (len == WS_PAYLOAD_64) {
                                needed += 8;
                            }

                            if (needed <= avail) {
                                // read header and set state

                                // read hdr
                                buf->Pop(con->_frame._hdr, 2);
                                con->_frame._opcode = (WSOPCode)(con->_frame._hdr[0] & 0x0F);
                                con->_frame._masked = con->_frame._hdr[1] & 0x80;

                                if (con->_frame._masked) {
                                    // read mask
                                    buf->Pop(con->_frame._mask, 4);
                                }

                                if (len == WS_PAYLOAD_16) {
                                    uint16_t ilen = 0;
                                    buf->Pop(reinterpret_cast<char *>(&ilen), sizeof(uint16_t));
                                    con->_frame._len = ntohs(ilen);
                                } else if (len == WS_PAYLOAD_64) {
                                    uint64_t ilen = 0;
                                    buf->Pop(reinterpret_cast<char *>(&ilen), sizeof(uint64_t));
                                    con->_frame._len = be64toh(ilen);
                                } else {
                                    con->_frame._len = len;
                                }

                                con->_state = WS_CS_OPEN_DATA;
                            } else {
                                return 1; // wait for more data 
                            }
                        } else {
                            return 1;
                        }
                        return 2;
                    }

                    bool ProcessState (std::shared_ptr<con_type> &con) {
                        switch (con->_state) {
                            case WS_CS_UNKNOWN:
                            {
                                //error
                            }
                            break;

                            case WS_CS_CONNECTING:
                            {
                                if (con->_server) {
                                    int checkHeaderCount = 0;

                                    if (con->HasHeader(HEADER_HOST)) ++checkHeaderCount;
                                    if (con->HasHeader(HEADER_UPGRADE)) ++checkHeaderCount;
                                    if (con->HasHeader(HEADER_CONNECTION)) ++checkHeaderCount;
                                    if (con->HasHeader(HEADER_ORIGIN)) ++checkHeaderCount;
                                    if (con->HasHeader(HEADER_SEC_WEBSOCKET_KEY)) ++checkHeaderCount;
                                    if (con->HasHeader(HEADER_SEC_WEBSOCKET_VERSION)) ++checkHeaderCount;

                                    if (con->_method == "GET" && con->_version == "HTTP/1.1" && checkHeaderCount == 6) {
                                        std::string wskey;
                                        if(!con->GetHeader(HEADER_SEC_WEBSOCKET_KEY, wskey)) {
                                            _logger->error("Failed to read {0}", HEADER_SEC_WEBSOCKET_KEY);
                                            return false;
                                        }
                                        
                                        // TODO some alloc here
                                        std::string key = wskey + std::string(WEBSOCKET_GUID);
                                        std::string newKey;
                                        if (!ComputeKey(key, newKey)) {
                                            _logger->error("Failed to compute key.");
                                            return false;
                                        }

                                        // set write bit
                                        con->_writeBuf->Push(HEADER_SERVER, HEADER_SERVER_LEN); // HEADER_SERVER\r\n
                                        con->_writeBuf->Push(HEADER_SERVER_UPGRADE, HEADER_SERVER_UPGRADE_LEN); // Upgrade: websocket\r\n
                                        con->_writeBuf->Push(HEADER_SERVER_CONNECTION, HEADER_SERVER_CONNECTION_LEN); // Connection: Upgrade\r\n

                                        char keyHeader[1024];
                                        int r = snprintf(keyHeader, 1024, "%s: %s\r\n", HEADER_SEC_WEBSOCKET_ACCEPT, newKey.c_str());
                                        con->_writeBuf->Push(keyHeader, r); // Sec-WebSocket-Accept: key\r\n
                                        con->_writeBuf->Push(HEADER_HTTP_NEWLINE, HEADER_HTTP_NEWLINE_LEN); // \r\n

                                        con->_state = WS_CS_OPEN;
                                        _set_write(con->_fd);
                                    }
                                } else {
                                    int checkHeaderCount = 0;

                                    if (con->HasHeader("Server")) ++checkHeaderCount;
                                    if (con->HasHeader("Upgrade")) ++checkHeaderCount;
                                    if (con->HasHeader("Connection")) ++checkHeaderCount;
                                    if (con->HasHeader(HEADER_SEC_WEBSOCKET_ACCEPT)) ++checkHeaderCount;
                                    if (checkHeaderCount == 4) {
                                        if (con->_responsecode == "101" && con->_version == "HTTP/1.1") {
                                            std::string wskey;
                                            if(!con->GetHeader(HEADER_SEC_WEBSOCKET_ACCEPT, wskey)) {
                                                _logger->error("Failed to read {0}", HEADER_SEC_WEBSOCKET_ACCEPT);
                                                return false;
                                            }

                                            std::string key = std::string(reinterpret_cast<char *>(con->_key)) + std::string(WEBSOCKET_GUID);
                                            std::string checkKey;
                                            if (!ComputeKey(key, checkKey)) {
                                                _logger->error("Failed to compute key.");
                                                return false;
                                            }
                                                
                                            if (wskey != checkKey) {
                                                _logger->error("Keys do not match {0} {1}", wskey, checkKey);
                                                return false;
                                            }

                                            con->_state = WS_CS_OPEN;

                                            if (con->_onOpen) {
                                                con->_onOpen(con->_fd);
                                            }

                                            if (con->_stream) {
                                                std::function<bool(const char *, uint64_t)> copyCB = [&con] (const char *d, uint64_t len) {
                                                    return con->_stream->Push(d, len);
                                                };
                                                con->_httpBuf->PopAll(copyCB, con->_httpBuf->Available());
                                            }
                                        } else {
                                            // error
                                            return false;
                                        }
                                    } else {
                                        // wait
                                    }
                                }
                            }
                            break;

                            case WS_CS_OPEN:
                            {
                                int r = 0;
                                if (con->_stream) {
                                    r = DoOpen(con->_stream, con);
                                } else {
                                    r = DoOpen(con->_httpBuf, con);
                                }
                                if (r == 0) return false;
                                else if (con->_state != WS_CS_OPEN_DATA) return false; // wait for more data  
                                // ALLOW TO FALL THROUGH
                            }

                            case WS_CS_OPEN_DATA:
                            {
                                int r = 0;
                                if (con->_stream) {
                                    r = DoOpenData(con->_stream, con);
                                } else {
                                    r = DoOpenData(con->_httpBuf, con);
                                }
                                if (r == 0) return false;

                                // check for another msg
                                if (con->_state == WS_CS_OPEN) {
                                    if (con->_stream) {
                                        return con->_stream->Available() > 0;
                                    } else {
                                        return con->_httpBuf->Available() > 0;
                                    }
                                }
                                return false;
                            }
                            break;

                            case WS_CS_CLOSING:
                            {
                                // nop
                                con->_state = WS_CS_CLOSED;
                                return false;
                            }
                            break;

                            case WS_CS_CLOSED:
                            {
                                // nop
                                return false;
                            }
                            break;

                            default:
                            break;
                        }
                        return false;
                    }

                    int HandleHTTP (std::shared_ptr<con_type> &con) {
                        char header[MAX_HEADER_SIZE]; // max header
                        uint64_t offset = 0;

                        bool is_uri = true;
                        bool done = false;
                        while (con->_httpBuf->Find(LF, offset) && !done) {
                            if (offset == 1) {
                                con->_httpBuf->Pop(header, offset+1);
                                done = true;
                                continue;
                            }
                            if (offset >= (MAX_HEADER_SIZE-1)) {
                                return -3;
                            }
                            if (!con->_httpBuf->Pop(header, offset+1)) {
                                // TODO Log error
                                return -2;
                            }
                            header[offset] = 0;
                            if (is_uri && con->_server) {
                                std::string raw = std::string(header, offset-1);
                                is_uri = false;

                                size_t a = raw.find(' ');
                                size_t b = raw.find(' ',a+1);

                                if (a > 0 && b > 0) {
                                    con->_method = raw.substr(0, a).c_str();
                                    con->_uri = raw.substr(a+1, b-a-1).c_str();
                                    con->_version = raw.substr(b+1).c_str();
                                }
                            } else if (is_uri && !con->_server) {
                                std::string raw = std::string(header, offset-1);
                                is_uri = false;

                                size_t a = raw.find(' ');
                                size_t b = raw.find(' ',a+1);

                                if (a > 0 && b > 0) {
                                    con->_version = raw.substr(0, a).c_str();
                                    con->_responsecode = raw.substr(a+1, b-a-1).c_str();
                                    con->_text = raw.substr(b+1).c_str();
                                }
                            } else {

                                if(!AddHeader(header, offset, con)) {
                                    _logger->error("Add header failed.");
                                }
                            }
                        }

                        return 0;
                    }
            };
        }
    }
    
} // coypu

#endif
