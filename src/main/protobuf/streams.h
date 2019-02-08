#ifndef __COYPU_PROTOBUF_H
#define __COYPU_PROTOBUF_H

#include <iostream>
#include <google/protobuf/io/zero_copy_stream.h>

// Bind a coypu stream to protobuf
namespace coypu {
  namespace protobuf {
	 template <typename T>
		class LogZeroCopyOutputStream : public google::protobuf::io::ZeroCopyOutputStream {
	 public:
	 LogZeroCopyOutputStream(T t) : _t(t), _byteCount(0) { }
		virtual ~LogZeroCopyOutputStream () { }

		bool Next(void ** data, int * size)  {
		  return _t->ZeroCopyWriteNext(data,size) == 0;
		}

		void BackUp(int count) {
		  _t->ZeroCopyWriteBackup(count);
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
		LogZeroCopyOutputStream (const LogZeroCopyOutputStream& other) = delete;
		LogZeroCopyOutputStream &operator= (const LogZeroCopyOutputStream& other) = delete;

		T _t;
		int64_t _byteCount;
	 };

	 template <typename T>
		class LogZeroCopyInputStream : public google::protobuf::io::ZeroCopyInputStream {
	 public:
	 LogZeroCopyInputStream(T t) : _t(t), _byteCount(0) { }
		virtual ~LogZeroCopyInputStream () { }
  
		bool Next(const void ** data, int * size)  {
		  bool b = _t->ZeroCopyReadNext(_byteCount, data, size);
		  if (b) _byteCount += *size;
		  return b;
		}

		void BackUp(int count) {
		  _byteCount -= count;
		}

		bool Skip(int count) {
		  _byteCount += count;
		  return true;
		}

		int64_t ByteCount() const {
		  return _byteCount;
		}
  
	 private:
		LogZeroCopyInputStream (const LogZeroCopyInputStream& other) = delete;
		LogZeroCopyInputStream &operator= (const LogZeroCopyInputStream& other) = delete;

		T _t;
		int64_t _byteCount;
	 };
  }
}


#endif
