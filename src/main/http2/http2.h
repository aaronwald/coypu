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

#pragma once

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

#include <nghttp2/nghttp2.h>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include <iostream>
#include <functional>
#include <string>
#include <unordered_map>
#include <memory>
#include "buf/buf.h"
#include "protobuf/streams.h"

#include "proto/coincache.pb.h"

#define MAKE_NV(K, V)																	\
  {                                                                            \
    (uint8_t *)K, (uint8_t *)V, sizeof(K) - 1, sizeof(V) - 1,                  \
        NGHTTP2_NV_FLAG_NONE                                                   \
  }

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


		static constexpr const int HTTP2_INIT_SIZE       = 24;
		static constexpr const int HTTP2_FRAME_HDR_SIZE  = 9;
		static constexpr const int HTTP2_HEADER_BUF_SIZE = 8192;
		
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
		  uint8_t len[3];
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

		  void SetLength (uint32_t inlen) {
			 len[0] = 0xFF & (inlen >> 16);
			 len[1] = 0xFF & (inlen >> 8);
			 len[2] = 0xFF & inlen;

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

		// Simple GRPC + HTTP2 Support
		template <typename LogTrait, typename StreamTrait, typename PublishTrait, typename RequestTrait, typename ResponseTrait>
		  class HTTP2GRPCManager {
		public:
		  typedef std::function<int(int)> write_cb_type;
		  typedef std::function<ResponseTrait (RequestTrait &)> request_cb_type;

		  HTTP2GRPCManager (LogTrait logger, 
								  write_cb_type set_write,
								  const std::string &path ) : _logger(logger),
			 _capacity(INITIAL_SETTINGS_MAX_FRAME_SIZE*2),
			 _set_write(set_write), _inflater(nullptr), _path(path) {
			 static_assert(sizeof(H2Header) == 9, "H2 Header Size");

			 int rv = nghttp2_hd_inflate_new(&_inflater);
			 if (rv != 0) {
				_logger->error("nghttp2_hd_inflate_init failed with error: {0}", nghttp2_strerror(rv));
				assert(false);
			 }
		  }

		  virtual ~HTTP2GRPCManager () {
			 if (_inflater) {
				nghttp2_hd_inflate_del(_inflater);
			 }
		  }

		  bool RegisterConnection(int fd, 
										  bool serverCon,
										  std::function<int(int,const struct iovec *,int)> readv,
										  std::function<int(int,const struct iovec *,int)> writev,
										  std::function <void(int)> onOpen,
										  std::shared_ptr<StreamTrait> &stream,
										  std::shared_ptr<PublishTrait> &publish) {
			 auto sp = std::make_shared<con_type>(fd, _capacity, !serverCon, serverCon, readv, writev, onOpen, stream, publish);
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
				  _logger->debug("Write fd[{0}] bytes[{1}]", fd, ret);
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
				_logger->debug("Read fd[{0}] bytes[{1}] State[{2}]", fd, r, con->_state);
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

		  void SetRequestCB (request_cb_type &cb) {
			 _cb = cb;
		  }
		  
		private:
		  typedef std::shared_ptr<StreamTrait> buf_sp_type;
		  typedef coypu::protobuf::LogZeroCopyInputStream<buf_sp_type> proto_in_type;

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
			 uint32_t _windowSize;
			 bool _lastPathMatch;

			 HTTP2Connection (int fd, uint64_t capacity, bool masked, bool server,
									std::function<int(int,const struct iovec *,int)> &readv,
									std::function<int(int,const struct iovec *,int)> &writev,
									std::function <void(int)> onOpen,
									std::shared_ptr<StreamTrait> &stream,
									std::shared_ptr<PublishTrait> &publish) :
			 _fd(fd), _stream(stream), _publish(publish), _hdr({}), _frameCount(0), _readData(nullptr), _writeData(nullptr), 
				_state(H2_CS_UNKNOWN), _server(server), _readv(readv), _writev(writev),
				_onOpen(onOpen), _windowSize(INITIAL_SETTINGS_MAX_FRAME_SIZE), _lastPathMatch(false) {
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
		  nghttp2_hd_inflater * _inflater;
		  std::string _path;
		  
		  RequestTrait _request;
		  request_cb_type _cb;

		  int inflate_header_block(std::shared_ptr<con_type> &con,
											nghttp2_hd_inflater *inflater, uint8_t *in,
											size_t inlen, int final) {
			 ssize_t rv;
			 
			 for (;;) {
				nghttp2_nv nv;
				int inflate_flags = 0;
				size_t proclen;
				
				rv = nghttp2_hd_inflate_hd2(inflater, &nv, &inflate_flags, in, inlen, final);

				if (rv < 0) {
				  _logger->error("inflate failed with error code {0}", rv);
				  return -1;
				}
				
				proclen = (size_t)rv;
				
				in += proclen;
				inlen -= proclen;
				
				if (inflate_flags & NGHTTP2_HD_INFLATE_EMIT) {
				  _logger->debug("{0} = {1}", nv.name, nv.value);

				  if (::strncmp(reinterpret_cast<char*>(nv.name), ":path", std::min(nv.namelen, 5ul)) == 0) {
					 if (::strncmp(reinterpret_cast<char*>(nv.value), _path.c_str(), std::min(nv.valuelen, _path.length())) == 0) {
						_logger->info("Path {0}", nv.value);
						con->_lastPathMatch = true;
					 } else {
						_logger->error("Path mismatch {0}", nv.value);
						con->_lastPathMatch = false;
					 }

				  }
				}
				
				if (inflate_flags & NGHTTP2_HD_INFLATE_FINAL) {
				  nghttp2_hd_inflate_end_headers(inflater);
				  break;
				}
				
				if ((inflate_flags & NGHTTP2_HD_INFLATE_EMIT) == 0 && inlen == 0) {
				  break;
				}
			 }
			 
			 return 0;
		  }

		  // return < 0 error
		  // return 0 wait for more data
		  // return > 0 loop
		  int ProcessFrames (std::shared_ptr<con_type> &con) {
			 if (con->_state == H2_CS_READ_FRAME_HEADER) {
				if (con->_stream->Available() >= HTTP2_FRAME_HDR_SIZE) {
				  con->_hdr.Reset();
				  if (!con->_stream->Pop(reinterpret_cast<char *>(&con->_hdr), sizeof(H2Header))) {
					 return -1;
				  }
				  
				  con->_state = H2_CS_READ_FRAME;
				  con->_hdr.id &= ~(1UL << 31); // ignore this bit when receiving
				  //con->_hdr.id &= 0x7FFFFFFF; // ignore this bit when receiving

				  _logger->debug("Frame Type[{0}] Flags [{1}] Id[{2}]", (int)con->_hdr.type, (int)con->_hdr.flags, con->_hdr.id);
				} else {
				  return 0; // more data
				}
			 }

			 if (con->_state == H2_CS_READ_FRAME) {
				uint32_t len = 0x00FFFFFF & ((con->_hdr.len[0] << 16) | (con->_hdr.len[1] << 8) | (con->_hdr.len[2]));
				
				if (con->_stream->Available() >= len) {
				  // TODO Pop frame
				  // To where? Anon store?
				  ++con->_frameCount;
				  //			  ProcessFrame(con);

				  
				  if (con->_hdr.type == H2_FT_HEADERS) {
					 // header - use nghttp2 libs
					 char data[HTTP2_HEADER_BUF_SIZE];
					 assert(len < HTTP2_HEADER_BUF_SIZE);
					 con->_stream->Pop(data, len);


					 // :path = /coypu.msg.CoypuService/RequestData
					 // content-type = application/grpc

					 inflate_header_block(con, _inflater, reinterpret_cast<uint8_t *>(data), len, 1);
				  } else if (con->_hdr.type == H2_FT_DATA) {
					 // data
					 char data[16] = {};
					 con->_stream->Pop(data, 5);

					 // TODO If this is a continuation we have to buffer the data until all is received.
					 // Would require a continuationBuffer to do copy.
					 
					 // grpc len
					 uint32_t grpcLen = ntohl(*(reinterpret_cast<uint32_t *>(&data[1])));
					 if (con->_lastPathMatch) {
						assert(data[0] == 0); // compressed byte

						// proto buf
						proto_in_type gIn(con->_stream, con->_stream->CurrentOffset());
						google::protobuf::io::CodedInputStream gInStream(&gIn);
						
						google::protobuf::io::CodedInputStream::Limit limit =
						  gInStream.PushLimit(grpcLen);
						
						bool b = _request.MergeFromCodedStream(&gInStream);
						assert(b);
						
						assert(gInStream.ConsumedEntireMessage());
						gInStream.PopLimit(limit);
						
						assert(_cb);
						ResponseTrait response = _cb(_request); // hope for move
						
						// send begin header
						nghttp2_nv nva2[] = {MAKE_NV(":status", "200"),
													MAKE_NV("content-type", "application/grpc+proto")};
						SendHeaderFrame(con, nva2, sizeof(nva2) / sizeof(nva2[0]), H2_F_END_HEADERS);
						
						// Data Frame w/ END_STREAM flag
						std::string s;
						b = response.SerializeToString(&s);
						assert(b);
						assert(s.length()+5 < INITIAL_SETTINGS_MAX_FRAME_SIZE);
						H2Header dataFrame(H2_FT_DATA);
						dataFrame.id = con->_hdr.id; // what should this be?
						dataFrame.SetLength(5 + s.length());
						SendGRPCFrame(con, dataFrame, s.c_str(), s.length());
					 
						// send end header
						nghttp2_nv nva_end[] = {MAKE_NV("grpc-status", "0")};
						SendHeaderFrame(con, nva_end, sizeof(nva_end) / sizeof(nva_end[0]), H2_F_END_HEADERS | H2_F_END_STREAM);
					 } else {
						_logger->error("Path mismatch. Skipping proto.");
						bool b = con->_stream->Skip(grpcLen);
						assert(b);
						nghttp2_nv nva2[] = {MAKE_NV(":status", "404")};
						SendHeaderFrame(con, nva2, sizeof(nva2) / sizeof(nva2[0]), H2_F_END_HEADERS | H2_F_END_STREAM);
						// TODO: Determine right error sequence
					 }
				  } else if (con->_hdr.type == H2_FT_GOAWAY) {
					 uint32_t streamId = 0;
					 uint32_t errorCode = 0;
					 bool b = con->_stream->Pop(reinterpret_cast<char *>(&streamId), sizeof(uint32_t));
					 assert(b);
					 streamId &= ~(1UL << 31);
					 streamId = ntohl(streamId);

					 b = con->_stream->Pop(reinterpret_cast<char *>(&errorCode), sizeof(uint32_t));
					 assert(b);
					 errorCode = ntohl(errorCode);

					 char foo[1024] = {};
					 con->_stream->Pop(foo, std::min(1024u, len-8));
					 
					 _logger->info("Go away fd[{0}] errorCode[{1}] Msg[{2}]", con->_fd, errorCode, foo);

				  } else if (con->_hdr.type == H2_FT_WINDOW_UPDATE) {
					 assert(len == 4);
					 uint32_t windowUpdate = 0;
					 bool b = con->_stream->Pop(reinterpret_cast<char *>(&windowUpdate), sizeof(uint32_t));
					 assert(b);
					 windowUpdate &= ~(1UL << 31);
					 windowUpdate = ntohl(windowUpdate);
					 con->_windowSize = windowUpdate;
					 // TODO Should change the capacity
					 _logger->debug("WindowUpdate fd[{0}] size[{1}]", con->_fd, con->_windowSize);
				  } else if (con->_hdr.type == H2_FT_SETTINGS) {
					 if (con->_frameCount == 1) {
						// send empty settings always
						H2Header empty(H2_FT_SETTINGS);
						SendFrame(con, empty, nullptr, 0);
					 }
					 
					 if (len >= 0) {
						assert(len % 6 == 0); // 4,2

						for (int i = 0; i < len; i += 6) {
						  uint16_t identifier = 0;
						  uint32_t value = 0;
						  bool b= con->_stream->Pop(reinterpret_cast<char *>(&identifier), 2);
						  assert(b);
						  b = con->_stream->Pop(reinterpret_cast<char *>(&value), 4);
						  assert(b);
						  identifier = ntohs(identifier);
						  value = ntohl(value);
						  _logger->debug("Setting fd[{0}] id[{1}] value [{2}]", con->_fd, identifier, value);
						}

						H2Header empty(H2_FT_SETTINGS);
						empty.flags |= 0x1;
						SendFrame(con, empty, nullptr, 0);
					 }
				  } else {
					 con->_stream->Skip(len);
				  }

				  con->_state = H2_CS_READ_FRAME_HEADER; // back to header
				} else {
				  return 0; // more data
				}
			 }

			 if (con->_stream && con->_stream->Available() > 0) return 1;
			 
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

		  bool SendGRPCFrame (std::shared_ptr<con_type> &con, const H2Header &hdr, 
									 const char *data, size_t len) {
			 bool b = con->_writeBuf->Push(reinterpret_cast<const char *>(&hdr), sizeof(H2Header));		 
			 if (!b) return false;
			 uint32_t outSize = htonl(len);
			 char compressed = 0;
			 b = con->_writeBuf->Push(&compressed, 1);
			 if (!b) return false;
			 b = con->_writeBuf->Push(reinterpret_cast<char *>(&outSize), sizeof(uint32_t));
			 if (!b) return false;
					 
			 if (len) {
				b = con->_writeBuf->Push(data, len);
				if (!b) return false;
			 }
			 _set_write(con->_fd);
			 return true;
		  }

		  bool SendHeaderFrame (std::shared_ptr<con_type> &con, nghttp2_nv *nva, size_t nvlen, char flags) {
			 nghttp2_hd_deflater * deflater;
			 nghttp2_hd_deflate_new(&deflater, HTTP2_HEADER_BUF_SIZE);
			 
			 size_t buflen = nghttp2_hd_deflate_bound(deflater, nva, nvlen);
			 uint8_t buf[HTTP2_HEADER_BUF_SIZE];
			 ::memset(buf, 0, HTTP2_HEADER_BUF_SIZE);
			 assert(buflen <= HTTP2_HEADER_BUF_SIZE);
			 int rv = nghttp2_hd_deflate_hd(deflater, buf, buflen, nva, nvlen);
			 if (rv < 0) {
				_logger->error("nghttp2_hd_deflate_hd failed with error: {0}", nghttp2_strerror(rv));
				return false;
			 } 
			 size_t outlen = (size_t)rv;
			 H2Header headers(H2_FT_HEADERS);
			 headers.SetLength(outlen);
			 headers.flags |= flags;
			 headers.id = con->_hdr.id; // what should this be?
			 SendFrame(con, headers, reinterpret_cast<char *>(buf), outlen);
			 nghttp2_hd_deflate_del(deflater);
			 return true;
		  }

		};
  }
    
} // coypu


