#ifndef __COYPU_PROTOMGR_H
#define __COYPU_PROTOMGR_H

#include <functional>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <google/protobuf/io/zero_copy_stream.h>
#include <google/protobuf/io/coded_stream.h>

#include "buf/buf.h"
#include "util/string-util.h"

namespace coypu {
  namespace protobuf {
	 template <typename T>
		class BufZeroCopyOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
	 public:
	 BufZeroCopyOutputStream(T t) : _t(t), _byteCount(0) { }
		virtual ~BufZeroCopyOutputStream () { }

		bool Next(void ** data, int * size)  {
		  bool b=  _t->PushDirect(data, size);
		  if (b) _byteCount += *size;
		  return b;
		}

		void BackUp(int count) {
		  if (_t->BackupDirect(count)) {
			 _byteCount -= count;
		  }		  
		}

		int64_t ByteCount() const {
		  return _byteCount;
		}

		bool WriteAliasedRaw(const void * data, int size) {
		  return false;
		}

		bool AllowsAliasing() const {
		  return false;
		}
	 private:
		BufZeroCopyOutputStream (const BufZeroCopyOutputStream& other) = delete;
		BufZeroCopyOutputStream &operator= (const BufZeroCopyOutputStream& other) = delete;

		T _t;
		int64_t _byteCount;
	 };

	 template <typename T>
		class BufZeroCopyInputStream : public google::protobuf::io::ZeroCopyInputStream {
	 public:
	 BufZeroCopyInputStream(T t) : _t(t), _byteCount(0) { }
		virtual ~BufZeroCopyInputStream () { }
  
		bool Next(const void ** data, int * size)  {
		  bool b = _t->Direct(data, size);
		  if (b) _byteCount += *size;
		  return b;
		}

		void BackUp(int count) {
		  _t->Backup(count);
		  _byteCount -= count;
		}

		bool Skip(int count) {
		  bool b = _t->Skip(count);
		  if (b) _byteCount += count;
		  return b;
		}

		int64_t ByteCount() const {
		  return _byteCount;
		}
  
	 private:
		BufZeroCopyInputStream (const BufZeroCopyInputStream& other) = delete;
		BufZeroCopyInputStream &operator= (const BufZeroCopyInputStream& other) = delete;

		T _t;
		int64_t _byteCount;
	 };


	 template <typename LogTrait, typename RequestTrait>
		class ProtoManager {
	 public:
		typedef std::function<void(int, const RequestTrait &)> callback_type;
		
		typedef std::function<int(int)> write_cb_type;

		ProtoManager (LogTrait logger, 
						  write_cb_type set_write) noexcept : _logger(logger),
		  _capacity(64*1024), _set_write(set_write)  {
		}

		virtual ~ProtoManager () {
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
		
		int Read (int fd) {
		  auto x = _connections.find(fd);
		  if (x == _connections.end()) return -1;
		  std::shared_ptr<con_type> &con = (*x).second;
		  if (!con) return -2;

		  int r = con->_readBuf->Readv(fd, con->_readv);
		  if (con->_readBuf->Available() >= 4) {

			 if (con->_gSize == 0) {
				con->_readBuf->Pop(reinterpret_cast<char *>(&con->_gSize), 4);
				con->_gSize = ntohl(con->_gSize);
			 }

			 if (con->_gSize > 0 && con->_readBuf->Available() >= con->_gSize) {

				proto_in_type gIn(con->_readBuf);
				google::protobuf::io::CodedInputStream gInStream(&gIn);
				
				google::protobuf::io::CodedInputStream::Limit limit =
				  gInStream.PushLimit(con->_gSize);

				bool b = _request.MergeFromCodedStream(&gInStream);
				assert(b);
				assert(gInStream.ConsumedEntireMessage());

				if (_cb) {
				  _cb(con->_fd, _request);
				}
				
				gInStream.PopLimit(limit);
				con->_gSize = 0;
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

		void SetCallback (callback_type &cb) {
		  _cb = cb;
		}
                

	 private:
		ProtoManager (const ProtoManager &other);
		ProtoManager &operator= (const ProtoManager &other);

		typedef coypu::buf::BipBuf <char, int> buf_type;
		typedef std::shared_ptr<buf_type> buf_sp_type;
		typedef BufZeroCopyInputStream<buf_sp_type> proto_in_type;
		
		typedef struct ClientConnection {
		  int _fd;

		  buf_sp_type _readBuf;
		  buf_sp_type _writeBuf;

		  std::function<int(int,const struct iovec *,int)> _readv;
		  std::function<int(int,const struct iovec *,int)> _writev;
		  char * _readData;
		  char * _writeData;
		  uint32_t _gSize;

		  ClientConnection (int fd, 
									  int capacity,
									  std::function<int(int,const struct iovec *,int)> readv,
									  std::function<int(int,const struct iovec *,int)> writev ) :
		  _fd(fd), _readv(readv), _writev(writev), _gSize(0) { 
			 _readData = new char[capacity];
			 _writeData = new char[capacity];
			 _readBuf = std::make_shared<buf_type>(_readData, capacity);
			 _writeBuf = std::make_shared<buf_type>(_writeData, capacity);
		  }

		  virtual ~ClientConnection () {
			 if (_readData) delete [] _readData;
			 if (_writeData) delete [] _writeData;
		  }
		} con_type;

		typedef std::unordered_map<int, std::shared_ptr<con_type>> con_map_type;

		LogTrait _logger;
		uint64_t _capacity;
		write_cb_type _set_write;
		con_map_type _connections;

		RequestTrait _request;
		callback_type _cb;
	 };
  }
}

#endif
