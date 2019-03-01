#pragma once

#include <vector>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <fcntl.h>
#include <unistd.h>

#include "store/store.h"
#include "store/storeutil.h"
#include "mem/mem.h"
#include "file/file.h"

namespace coypu {
  namespace cache {
	 // TagStore - just 64 bytes, seq no is the tag id. quick restore

	 // TagOffsetStore <Offset Id, Tag Id, fd>
	 // - For multiple tags, we can just track offset, if we've written then we can ignore subsequent interest

	 // Sub is <fd, bitset<tags>> (could be bloom later)
	 // Need PositioendStream that understands the subs and reads TagOffetStore
	 
	 // rawPersist -> Protobuf type,size,hdr(with stags),msg -> Generate tag cache -> TagCache & PublishCache (?)

	 struct Tag {
		uint64_t _offset;
		uint64_t _len;
		uint64_t _hashValue; // murmur + sha256? - check bit % maxBits - can just grow bits per tag, and just expand on connection to support max size
		uint32_t _tagId;
		int _fd;
	 };

	 class TagSub {
	 public:
		TagSub () : _tags(64) {
		}

		virtual ~TagSub () {
		}

		bool IsSet (uint32_t tagid) {
		  if (tagid < _tags.size()) return _tags[tagid];
		  return false;
		}

		void Clear (uint32_t tagid) {
		  if (tagid < _tags.size()) {
			 _tags[tagid] = false;
		  }
		}

		void Set(uint32_t tagid) {
		  if (tagid >= _tags.size()) {
			 _tags.resize(tagid+1,0);
		  }
		  _tags[tagid] = true;
		}
	 private:
		TagSub (const TagSub &other) = delete;
		TagSub &operator = (const TagSub &other) = delete;
		
		std::vector<bool> _tags;
	 };

	 // positioned 
	 class TagOffsetStore {
	 public:
		typedef Tag tag_type;
		
		TagOffsetStore (const std::string &path) noexcept {
		  _offsetStore = coypu::store::StoreUtil::CreateSimpleStore<stream_type, buf_type>(path, 1);
		}

		virtual ~TagOffsetStore() {
		}

		bool Append(const tag_type &tag) {
		  return _offsetStore->Push(reinterpret_cast<const char *>(&tag), sizeof(tag_type)) == 0;
		}

	 private:
		typedef coypu::store::LogRWStream<coypu::file::MMapShared, coypu::store::LRUCache, 16> buf_type;
		typedef coypu::store::PositionedStream <buf_type> stream_type;
		
		TagOffsetStore(const TagOffsetStore &other) = delete;
		TagOffsetStore &operator= (const TagOffsetStore &other) = delete;

		std::shared_ptr<stream_type> _offsetStore;
	 };
   	
	 class TagStore {
	 public:
		const static uint32_t MAX_TAG_LEN = 128;
		typedef uint32_t tag_id_type;
		
		TagStore (const std::string &path) noexcept : _nextId(0) {
		  static_assert(sizeof(Tag) == 32, "TagMsg Size Check");
		  _tagBuf = coypu::store::StoreUtil::CreateSimpleBuf<buf_type>(path, 1);
		}
		
		virtual ~TagStore () noexcept {
		}

		bool Restore () {
		  if (!IsOpen()) {
			 return false;
		  }

		  char in_tag[MAX_TAG_LEN];
		  off64_t avail = _tagBuf->Available();
		  off64_t off = 0;
		  
		  for (;off < avail; ) {
			 if (avail-off < sizeof(size_t)) return false;

			 size_t len = 0;
			 if (!_tagBuf->Pop(off, reinterpret_cast<char *>(&len), sizeof(size_t))) {
				return false;
			 }
			 if (len == 0) break; 
			 off += sizeof(size_t);

			 if (len > MAX_TAG_LEN) return false;

			 if (!_tagBuf->Pop(off, in_tag, len)) {
				return false;
			 }

			 std::string restoreTag (in_tag, len);
			 _tags.push_back(restoreTag);
			 _tagToId.insert(std::make_pair(restoreTag, _nextId));
			 off += len;

			 ++_nextId;
		  }

		  return true;
		}

		std::string GetTag (tag_id_type id) {
		  if (id < _tags.size()) return _tags[id];
		  return "";
		}

		bool FindTag(const std::string &tag, tag_id_type &outid) {
		  auto b = _tagToId.find(tag);
		  if (b != _tagToId.end()) {
			 outid = (*b).second;
			 return true;
		  }
		  return false;
		}

		bool Append (const std::string &tag, tag_id_type &outid) {
		  if (tag.length() > MAX_TAG_LEN) return false;
		  
		  size_t l = tag.length();

		  if(_tagBuf->Push(reinterpret_cast<char *>(&l), sizeof(size_t))) {
			 return false;
		  }
		  
		  if (_tagBuf->Push(tag.c_str(), l)) {
			 return false;
		  }

		  outid = _nextId++;
		  _tags.push_back(tag);
		  _tagToId.insert(std::make_pair(tag, outid));
		  
		  return true;
		}
		
		bool IsOpen () {
		  return _tagBuf != nullptr;
		}

		tag_id_type GetTagCount () {
		  return _nextId;
		}

	 private:
		typedef coypu::store::LogRWStream<coypu::file::MMapShared, coypu::store::LRUCache, 1> buf_type;
		
		TagStore (const TagStore &other) = delete;
		TagStore &operator= (const TagStore &other) = delete;

		std::shared_ptr<buf_type> _tagBuf;
		tag_id_type _nextId;

		std::vector<std::string> _tags;
		std::unordered_map<std::string, tag_id_type> _tagToId;
	 };
	 
	 template <typename TagType>
		class TagStream {
	 public:
		typedef std::function<int(int)> write_cb_type;

		// fd should be eventfd()
	 TagStream(int fd, write_cb_type set_write) : _fd(fd), _set_write(set_write) {
		}

		virtual ~TagStream() {
		}


		int Read (int fd) {
		  uint64_t u = UINT64_MAX;
		  int r = ::read(_fd, &u, sizeof(uint64_t));
		  if (r > 0) {
			 assert(r == sizeof(uint64_t));
			 if (r < sizeof(uint64_t)) return -128;

			 std::cout << "tag:" << u << std::endl;
			 
 			 return 0;
		  }
		  return r;
		}

		int Write (int fd) {
		  // write queue
		  uint64_t u = 1;
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
		  return -1;
		}

		void Queue (const TagType &tag) {
		  _set_write(_fd);
		}

	 private:
		TagStream (const TagStream &other) = delete;
		TagStream &operator= (const TagStream &other) = delete;
		TagStream (const TagStream &&other) = delete;
		TagStream &operator= (const TagStream &&other) = delete;
			 
		int _fd;
		write_cb_type _set_write;
	 };
  }
}

