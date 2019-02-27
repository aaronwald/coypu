#ifndef __COYPU_TAG_CACHE
#define __COYPU_TAG_CACHE

namespace coypu {
  namespace cache {
	 // Would need a more advanced publish stream to target - allows fd target to fix pong

	 // TagCache <Tag> - just 64 bytes, seq no is the tag id. quick restore

	 // Tags <Offset Id, Tag Id, fd>
	 // - For multiple tags, we can just track offset, if we've written then we can ignore subsequent interest

	 // Sub is <fd, bitset<tags>> (could be bloom later)

	 
	 // rawPersist -> Protobuf type,size,hdr(with stags),msg -> Generate tag cache -> TagCache & PublishCache (?)
	 // if tags exit then the hashValue can be stored and computed just once
	 // could also just use tagId in filter? doesnt need to be bloom if the set is not that big?
	 struct Tag {
		uint64_t _offset;
		uint64_t _hashValue; // murmur + sha256? - check bit % maxBits - can just grow bits per tag, and just expand on connection to support max size
		uint32_t _tagId;
		int _fd;

		char _pad[8];
	 };
   	
	 class TagCache {
	 public:
		TagCache () {
		  static_assert(sizeof(Tag) == 32, "TagMsg Size Check");
		}
		
		virtual ~TagCache () {
		}

	 private:
		TagCache (const TagCache &other) = delete;
		TagCache &operator= (const TagCache &other) = delete;
	 };
  }
}

#endif
