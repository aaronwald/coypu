#pragma once

#include <vector>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <fcntl.h>
#include <unistd.h>
#include <set>

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
		uint32_t _tagId;

		int _fd;             // one shot how. set to -2 when done?
		char _flags;
		char _pad[7];

		Tag () : _offset(0), _len(0), _tagId(0), _fd(-1), _flags(0) {
		}

		Tag (uint64_t offset,
			  uint64_t len,
			  uint32_t tagId,
			  int fd,
			  char flags) : _offset(offset), _len(len), _tagId(tagId), _fd(fd), _flags(flags) {
		}
	 };

	 enum TagFlags {
		TF_PERSISTENT = 0x1
	 };

	 // TODO BLOOM: murmur + sha256? - check bit % maxBits - can just grow bits per tag, and just expand on connection to support max size

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

		void ClearAll () {
		  _tags.clear();
		}

		void Set(uint32_t tagid) {
		  while (tagid >= _tags.size()) {
			 _tags.resize(tagid+16,0);
		  }
		  _tags[tagid] = true;
		}

		size_t GetMaxTag () {
		  return _tags.size();
		}
	 private:
		TagSub (const TagSub &other) = delete;
		TagSub &operator = (const TagSub &other) = delete;
		
		std::vector<bool> _tags;
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

		bool Restore (off64_t &off) {
		  if (!IsOpen()) {
			 return false;
		  }

		  char in_tag[MAX_TAG_LEN];
		  off64_t avail = _tagBuf->Available();
		  off = 0;
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
		  
		  return _tagBuf->SetPosition(off);
		}

		std::string GetTag (tag_id_type id) {
		  if (id < _tags.size()) return _tags[id];
		  return "";
		}

		bool GetOrCreateTag (const std::string &tag, tag_id_type &outid) {
		  if (FindTag(tag, outid)) return true;
		  return Append(tag, outid);
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

		bool ReadNext (tag_type &out) {
		  return _offsetStore->Pop(reinterpret_cast<char *>(&out), sizeof(tag_type));
		}

		inline bool IsEmpty() const {
		  return _offsetStore->Available() == 0;
		}

		inline uint64_t TotalAvailable() const {
		  return _offsetStore->TotalAvailable();
		}

	 private:
		typedef coypu::store::LogRWStream<coypu::file::MMapShared, coypu::store::LRUCache, 16> buf_type;
		typedef coypu::store::PositionedStream <buf_type> stream_type;
		
		TagOffsetStore(const TagOffsetStore &other) = delete;
		TagOffsetStore &operator= (const TagOffsetStore &other) = delete;

		std::shared_ptr<stream_type> _offsetStore;
	 };

	 // Filter tags to streams. Allows for mixing streams (persistent and ephemeral)
	 template <typename TagType>
		class TagStream {
	 public:
		typedef std::function<int(int)> write_cb_type;
		typedef std::function<int(int, uint64_t, uint64_t)> stream_cb_type;

	 TagStream(int eventfd, write_cb_type set_write,
				  const std::string &storePath) : _fd(eventfd), _set_write(set_write),
		  _offsetStore(storePath), _emptyPosition({}), _maxRecordCount(128), _maxFDCount(16) {
		}

		virtual ~TagStream() {
		}

		// this fd will be ready for write when a tag exists which matches
		int StreamWrite(int fd) {
		  assert(fd < _fds.size());
		  FDData &data = _fds[fd];
		  
		  // TODO Stream Write

		  // only restore non ephemeral on restart
		  // snap start pos
		  
		  //uint16_t fdCount = 0;
		  // max records per fd
		  // check cur pos for fd
		  // stream through tags
		  // check if fd is sub or this is directed
		  // lookup stream
		  // call writecb for stream fd
		  // fd(offset,len)
		  // += position for len.
		  // if not done, then we keep writing. return 1 to keep writing
		  // offset is relative to the stream and the fd
		  // fd->stream->offset
		  
		  return 0;
		}

		int Register (int fd) {
		  while (_fds.size() < fd+1) {
			 _fds.resize(_fds.size()+16, nullptr);
		  }

		  _fds[fd] = std::make_shared<FDData>();
		  _fds[fd]->_registered = true;

		  return 0; 
		}

		int RegisterStream (int fd, uint64_t streamId, std::function<int(int, uint64_t, uint64_t)> writecb) {
		  assert(fd < _fds.size());

		  while (_fds[fd]->_streams.size() < streamId+1) {
			 _fds[fd]->_streams.resize(_fds[fd]->_streams.size()+16, _emptyPosition);
		  }

		  _fds[fd]->_streams[streamId].cb = writecb;
		  _fds[fd]->_streams[streamId].startOffset = _offsetStore.TotalAvailable(); // before this not restored if TF_PERSISTENT not set

		  // TODO
		  _fds[fd]->_streams[streamId].curOffset = 0;
		  _fds[fd]->_streams[streamId].length = 0;
		  _fds[fd]->_streams[streamId].written = 0;

		  return 0;
		}

		int Unregister (int fd) {
		  if (fd < _fds.size()) {
			 _fds[fd] = nullptr;

			 // Very slow
			 for (size_t tagId : _fds[fd]->_subs.GetMaxTag()) {
				_tag2fd[tagId].erase(tagId);
			 }
			 
			 _fds[fd]->_subs.ClearAll();
		  }
		  return 0;
		}

		void Subscribe (int fd, uint32_t tag) {
		  while (_tag2fd.size() < tag+1) {
			 _tag2fd.resize(_tag2fd.size()+16, std::set<int>());
		  }
		  assert(tag < _tag2fd.size());
		  _tag2fd[tag].insert(fd);

		  assert(fd < _fds.size());
		  _fds[fd]->_subs.Set(tag);
		}

		int Read (int fd) {
		  uint64_t u = UINT64_MAX;
		  int r = ::read(_fd, &u, sizeof(uint64_t));
		  if (r > 0) {
			 assert(r == sizeof(uint64_t));
			 if (r < sizeof(uint64_t)) return -128;

			 TagType tag;
			 uint32_t recordCount = 0;
			 while (!_offsetStore.IsEmpty() && recordCount < _maxRecordCount) {
				if (!_offsetStore.ReadNext(tag)) {
				  assert(false);
				}

				if (tag._fd > 0) {
				  _set_write(fd);
				} else {
				  assert(tag._tagId < _tag2fd.size());

				  for (int fd : _tag2fd[tag._tagId]) {
					 if (_fds[fd]->_registered) {
						_set_write(fd);
					 }
				  }
				}

				++recordCount;
			 }

			 if (!_offsetStore.IsEmpty()) {
				// could be less sycalls if we didnt force write.
				_set_write(fd);
			 }
			 
 			 return 0;
		  }
		  return r;
		}

		int Write (int fd) {
		  // write queue
		  uint64_t u = _offsetStore.TotalAvailable();
		  int r = ::write(_fd, &u, sizeof(uint64_t));
								
		  if (r > 0) {
			 assert(r == sizeof(uint64_t));
			 if (r < sizeof(uint64_t)) return -128;
			 return 0;
		  }
		  assert(false);
				
		  return -1;
		}

		void Queue (const TagType &tag) {
		  while (_tag2fd.size() < tag._tagId+1) {
			 _tag2fd.resize(_tag2fd.size()+16, std::set<int>());
		  }

		  _offsetStore.Append(tag);
		  _set_write(_fd);
		}

		int Close (int fd) {
		  return -1;
		}


	 private:
		TagStream (const TagStream &other) = delete;
		TagStream &operator= (const TagStream &other) = delete;
		TagStream (const TagStream &&other) = delete;
		TagStream &operator= (const TagStream &&other) = delete;
			 
		int _fd;
		write_cb_type _set_write;
		TagOffsetStore _offsetStore;

		typedef struct StreamPositionS {
		  stream_cb_type cb;
		  uint64_t startOffset;
		  
		  uint64_t curOffset; // 
		  uint64_t length;    //
		  uint64_t written;   // handle partial
		} StreamPosition;
		StreamPosition _emptyPosition;

		typedef struct FDDataS {
		  bool _registered;
		  TagSub _subs;
		  std::vector<StreamPosition> _streams;
		} FDData;
		std::vector <std::shared_ptr<FDData>> _fds;
		FDData _emptyFD;

		std::vector <std::set<int>> _tag2fd;
		
		uint32_t _maxRecordCount;
		uint16_t _maxFDCount;
	 };
  }
}
