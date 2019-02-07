#ifndef __COYPU_PROTOMGR_H
#define __COYPU_PROTOMGR_H

#include <functional>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <google/protobuf/io/zero_copy_stream.h>

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

	 template <typename LogTrait>
		class ProtoManager {
	 public:
		typedef std::function<void(const std::vector<std::string> &cmd)> callback_type;
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
		  std::cout << "foooo" << std::endl;

		  int r = con->_readBuf->Readv(fd, con->_readv);
		  uint64_t offset = 0;
		  if (con->_readBuf->Find('\n', offset)) {
			 char buf[1024*32];
			 if (offset+1 > sizeof(buf)) return -1;
			 if (con->_readBuf->Pop(buf, offset+1)) {
				buf[offset] = 0;
				std::string s(buf);
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
		ProtoManager (const ProtoManager &other);
		ProtoManager &operator= (const ProtoManager &other);

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
