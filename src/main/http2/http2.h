/*
 * Created on Mon Feb 11 2019
 *
 *  Copyright (c) 2019 Aaron Wald
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

#ifndef __COYPU_HTTP2_H
#define __COYPU_HTTP2_H

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

#include <iostream>
#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include "buf/buf.h"

namespace coypu
{
  namespace http2
  {
		//	"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
		// 0x505249202a20485454502f322e300d0a0d0a534d0d0a0d0a
		// 24 bytes
		static constexpr const char HTTP2_HDR []=
		  {'P', 'R', 'I', ' ',
			'*', ' ', 'H', 'T',
			'T', 'P', '/', '2',
			'.', '0', '\r', '\n',
			'\r', '\n', 'S', 'M',
			'\r', '\n', '\r', '\n'};


		static constexpr const int HTTP2_INIT_SIZE = 24;
		static constexpr const int HTTP2_FRAME_HDR_SIZE = 9;
		
		// headers
		// 9 bytes
		/*
		  +-----------------------------------------------+
		  |                 Length (24)                   |
		  +---------------+---------------+---------------+
		  |   Type (8)    |   Flags (8)   |
		  +-+-------------+---------------+-------------------------------+
		  |R|                 Stream Identifier (31)                      |
		  +=+=============================================================+
		  |                   Frame Payload (0...)                      ...
		  +---------------------------------------------------------------+
		*/
		static constexpr const int INITIAL_SETTINGS_MAX_FRAME_SIZE = 16384; //2^14
		static constexpr const int MAX_SETTINGS_MAX_FRAME_SIZE = 16777215; // 2^24-1 


		   
		enum H2FrameType {
		  H2_FT_DATA          = 0x0,
		  H2_FT_HEADERS       = 0x1,
		  H2_FT_PRIORITY      = 0x2,
		  H2_FT_RST_STREAM    = 0x3,
		  H2_FT_SETTINGS      = 0x4,
		  H2_FT_PUSH_PROMISE  = 0x5,
		  H2_FT_PING          = 0x6,
		  H2_FT_GOAWAY        = 0x7,
		  H2_FT_WINDOW_UPDATE = 0x8,
		  H2_FT_CONTINUATION  = 0x9
		};

		// id=0x0 connection
		// client odd streams
		// server even streams

		struct H2Header {
		  char len[3];
		  char type;
		  char flags;
		  int32_t id;
		  H2Header (H2FrameType inType = H2_FT_DATA) : type(inType), flags(0), id(0) {
			 len[0] = len[1] = len[2] = 0;
		  }
		  
		  void Reset () {
			 len[0] = len[1] = len[2] = 0;
			 type = flags = id = 0;
		  }
		} __attribute__((packed));


		enum H2Settings {
		  H2_S_HEADER_TABLE_SIZE      = 0x1,
		  H2_S_ENABLE_PUSH            = 0x2,
		  H2_S_MAX_CONCURRENT_STREAMS = 0x3,
		  H2_S_INITIAL_WINDOW_SIZE    = 0x4,
		  H2_S_MAX_FRAME_SIZE         = 0x5,
		  H2_S_MAX_HEADER_LIST_SIZE   = 0x6
		};

		enum H2ErrorCode {
		  H2_EC_NO_ERROR            = 0x0,
		  H2_EC_PROTOCOL_ERROR      = 0x1,
		  H2_EC_INTERNAL_ERROR      = 0x2,
		  H2_EC_FLOW_CONTROL_ERROR  = 0x3,
		  H2_EC_SETTINGS_TIMEOUT    = 0x4,
		  H2_EC_STREAM_CLOSED       = 0x5,
		  H2_EC_FRAME_SIZE_ERROR    = 0x6,
		  H2_EC_REFUSED_STREAM      = 0x7,
		  H2_EC_CANCEL              = 0x8,
		  H2_EC_COMPRESSION_ERROR   = 0x9,
		  H2_EC_CONNECT_ERROR       = 0xa,
		  H2_EC_ENHANCE_YOUR_CALM   = 0xb,
		  H2_EC_INADEQUATE_SECURITY = 0xc,
		  H2_EC_HTTP_1_1_REQUIRED   = 0xd
		};
		
		enum H2Flags {
		  H2_F_END_STREAM  = 0x1, //      0000 0001
		  H2_F_END_HEADERS = 0x4, //      0000 0100
		  H2_F_PADDED      = 0x8, //      0000 1000
		  H2_F_PRIORITY    = 0x20 // 1<<5 0010 0000
		};

		// stream states
		enum H2StreamState {
		  H2_SSTATE_UNKNOWN,
		  H2_SSTATE_IDLE,
		  H2_SSTATE_OPEN,
		  H2_SSTATE_RESERVED_LOCAL,
		  H2_SSTATE_RESERVED_REMOTE,
		  H2_SSTATE_HALF_CLOSED_REMOTE,
		  H2_SSTATE_HALF_CLOSED_LOCAL,
		  H2_SSTATE_CLOSED
		};
		
		enum H2ConnectionState {
		  H2_CS_UNKNOWN,
		  H2_CS_CONNECTING,
		  H2_CS_PRE_HTTP,
		  H2_CS_READ_FRAME_HEADER,
		  H2_CS_READ_FRAME,
		  H2_CS_CLOSED
		};

		// ProviderType to read from underlying connection which could be regular socket (writev/readv) or SSL (SSL_Read/SSL_Write)
		template <typename LogTrait, typename StreamTrait, typename PublishTrait>
		  class HTTP2Manager {
		public:
		  typedef std::function<int(int)> write_cb_type;

		  HTTP2Manager (LogTrait logger, 
								  write_cb_type set_write) : _logger(logger),
			 _capacity(INITIAL_SETTINGS_MAX_FRAME_SIZE*2), _set_write(set_write)  {
			 static_assert(sizeof(H2Header) == 9, "H2 Header Size");
		  }

		  virtual ~HTTP2Manager () {
		  }

		  bool RegisterConnection(int fd, 
										  bool serverCon,
										  std::function<int(int,const struct iovec *,int)> readv,
										  std::function<int(int,const struct iovec *,int)> writev,
										  std::function <void(int)> onOpen,
										  std::function <void(uint64_t, uint64_t)> onText,
										  const std::shared_ptr<StreamTrait> stream,
										  const std::shared_ptr<PublishTrait> publish) {
			 auto sp = std::make_shared<con_type>(fd, _capacity, !serverCon, serverCon, readv, writev, onOpen, onText, stream, publish);
			 auto p = std::make_pair(fd, sp);
			 sp->_state = H2_CS_CONNECTING;

			 assert(_connections.find(fd) == _connections.end());

			 return _connections.insert(p).second;
		  }

		  int Unregister (int fd) {
			 auto x = _connections.find(fd);
			 if (x == _connections.end()) return -1;
			 std::shared_ptr<con_type> &con = (*x).second;
			 con->_state = H2_CS_CLOSED;

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

				if (con->_server) {
				  _logger->info("Write fd[{0}] bytes[{1}]", fd, ret);
				}

				if (ret < 0) return ret; // error

				// We could have EAGAIN/EWOULDBLOCK so we want to maintain write if data available
				// 0 will clear write bit
				// Can improve branching here if we just return is empty directly on the stack without another call
				return con->_writeBuf->IsEmpty() ? 0 : 1;
			 } else if (con->_publish) {
				// could limit size of write
				int ret = con->_publish->Writev(con->_publish->Available(fd), fd, con->_writev);
									 
				if (ret < 0) {
				  _logger->error("Publish error fd[{0}] err[{1}]", fd, ret);
				  return ret; // error
				}
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
			 if (con->_stream && (con->_state == H2_CS_READ_FRAME_HEADER ||
										 con->_state == H2_CS_READ_FRAME)) {
				r = con->_stream->Readv(fd, con->_readv);
			 } else {
				r = con->_httpBuf->Readv(fd, con->_readv);
			 }

			 if (con->_server) {
				_logger->info("Read fd[{0}] bytes[{1}] State[{2}]", fd, r, con->_state);
			 }


			 if (r < 0) return -3;
                        
			 if (con->_state == H2_CS_CONNECTING) {
				int r = ProcessConnecting(con);
				if (r < 0) return r;
			 }
			 
			 do {
			 } while ((r = ProcessFrames(con)) > 0);
			 return r;
		  }


		  // hack
		  void SetWriteAll () {
			 std::for_each(_connections.begin(), _connections.end(),
								[this] (const std::pair<int, std::shared_ptr<con_type>> p) { _set_write(p.first); });             
		  }
		  
		private:

		  typedef struct HTTP2Connection {
			 int _fd;
			 std::shared_ptr<coypu::buf::BipBuf <char, uint64_t>> _httpBuf;
			 std::shared_ptr<coypu::buf::BipBuf <char, uint64_t>> _writeBuf;
			 std::shared_ptr<StreamTrait> _stream;
			 std::shared_ptr<PublishTrait> _publish;
			 H2Header _hdr;
			 uint32_t _frameCount;
			 
			 char *_readData;
			 char *_writeData;
			 H2ConnectionState _state;

			 bool _server;
			 std::function<int(int,const struct iovec *,int)> _readv;
			 std::function<int(int,const struct iovec *,int)> _writev;
			 std::function <void(int)> _onOpen;
			 std::function <void(uint64_t, uint64_t)> _onText;

			 HTTP2Connection (int fd, uint64_t capacity, bool masked, bool server,
									std::function<int(int,const struct iovec *,int)> readv,
									std::function<int(int,const struct iovec *,int)> writev,
									std::function <void(int)> onOpen,
									std::function <void(uint64_t, uint64_t)> onText,
									std::shared_ptr<StreamTrait> stream,
									std::shared_ptr<PublishTrait> publish) :
			 _fd(fd), _stream(stream), _publish(publish), _hdr({}), _frameCount(0), _readData(nullptr), _writeData(nullptr), 
				_state(H2_CS_UNKNOWN), _server(server), _readv(readv), _writev(writev),
				_onOpen(onOpen), _onText(onText) {
				assert(capacity >= INITIAL_SETTINGS_MAX_FRAME_SIZE);
				_readData = new char[capacity];
				_writeData = new char[capacity];
				_httpBuf = std::make_shared<coypu::buf::BipBuf <char, uint64_t>>(_readData, capacity);
				_writeBuf = std::make_shared<coypu::buf::BipBuf <char, uint64_t>>(_writeData, capacity);
			 }

			 virtual ~HTTP2Connection () {
				if (_readData) delete [] _readData;
				if (_writeData) delete [] _writeData;
			 }

		  } con_type;

		  typedef std::unordered_map <int, std::shared_ptr<con_type>> map_type;

		  map_type _connections;

		  LogTrait _logger;
		  uint64_t _capacity;
		  write_cb_type _set_write;


		  // return < 0 error
		  // return 0 wait for more data
		  // return > 0 loop
		  int ProcessFrames (std::shared_ptr<con_type> &con) {
			 std::cout << "ProcessFrames " << con->_state << std::endl;
			 if (con->_state == H2_CS_READ_FRAME_HEADER) {
				if (con->_stream->Available() >= HTTP2_FRAME_HDR_SIZE) {
				  con->_hdr.Reset();
				  if (!con->_stream->Pop(reinterpret_cast<char *>(&con->_hdr), sizeof(H2Header))) {
					 return -1;
				  }
				  
				  con->_state = H2_CS_READ_FRAME;
				  std::cout << "Frame type " << (int)con->_hdr.type << std::endl;
				  std::cout << "Frame flags " << (int)con->_hdr.flags << std::endl;

				  con->_hdr.id &= ~(1UL << 31); // ignore this bit when receiving
				  //con->_hdr.id &= 0x7FFFFFFF; // ignore this bit when receiving
				  
				  std::cout << "Frame id " << std::hex << con->_hdr.id << std::dec << std::endl;
				  
				} else {
				  return 0; // more data
				}
			 }

			 if (con->_state == H2_CS_READ_FRAME) {
				uint32_t len = (con->_hdr.len[0] << 16) | (con->_hdr.len[1] << 8) | (con->_hdr.len[2]);
				std::cout << "Read len " << len << std::endl;
 
				
				if (len && con->_stream->Available() >= len) {
				  // TODO Pop frame
				  // To where? Anon store?
				  ++con->_frameCount;
				  //			  ProcessFrame(con);
				  con->_stream->Skip(len);

				  con->_state = H2_CS_READ_FRAME_HEADER; // back to header
				} else if (len == 0) {
				  ++con->_frameCount;
				  //ProcessFrame(con);

				  // hack
				  if (con->_frameCount == 1) {
					 H2Header empty(H2_FT_SETTINGS);
					 SendFrame(con, empty, nullptr, 0);
				  }


				  
				  con->_state = H2_CS_READ_FRAME_HEADER; // back to header
				} else {
				  return 0; // more data
				}
			 }
				  std::cout << "Rem " << con->_stream->Available() << std::endl;
			 
			 return 0;
		  }
		  
		  int ProcessConnecting (std::shared_ptr<con_type> &con) {
			 if (con->_httpBuf->Available() >= HTTP2_INIT_SIZE) {
				char priHdr[HTTP2_INIT_SIZE];
				if (!con->_httpBuf->Pop(priHdr, HTTP2_INIT_SIZE)) {
				  return -3;
				}

				for (int i = 0; i < HTTP2_INIT_SIZE; ++i) {
				  if (priHdr[i] != HTTP2_HDR[i]) {
					 _logger->error("Pri Failed fd[{0}] {1}", con->_fd, i);
					 return -2;
				  }
				}

				_logger->info("Pri Received fd[{0}]", con->_fd);
				con->_state = H2_CS_READ_FRAME_HEADER;

				// switch to streambuf
				std::function<bool(const char *, uint64_t)> copyCB = [&con] (const char *d, uint64_t len) {
				  return con->_stream->Push(d, len);
				};
				con->_httpBuf->PopAll(copyCB, con->_httpBuf->Available());

				return 0;
			 }
			 return 0;
		  }

		  bool SendFrame (std::shared_ptr<con_type> &con, const H2Header &hdr, const char *data, size_t len) {
			 bool b = con->_writeBuf->Push(reinterpret_cast<const char *>(&hdr), sizeof(H2Header));
			 if (!b) return false;
			 if (len) {
				b = con->_writeBuf->Push(data, len);
				if (!b) return false;
			 }
			 _set_write(con->_fd);
			 return true;
		  }
			 
		};
  }
    
} // coypu

#endif
