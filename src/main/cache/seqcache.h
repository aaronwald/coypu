
#ifndef __COYPU_CACHE
#define __COYPU_CACHE

#include <stdint.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <iostream>
#include <streambuf>

namespace coypu {
    namespace cache {
		// Check bloom filter on each connection. Send if interest - compute hash once to scale with connections.
		// Would need a more advanced publish stream to target - allows fd target to fix pong
		// Record per tag - mark with offset / no seqno
		// To post doc we need a protocol that sends us the tags so we can hash them.
		// > Use google protobuf here?
		// > Post tags,tag,tag,tag, doc
		struct TagCache {
		  uint64_t _offset;
		  uint64_t _hashValue; // murmur + sha256? - check bit % maxBits - can just grow bits per tag, and just expand on connection to support max size
		  
		  int _fd;
		  char _pad[12];
		};
   	
        template <typename CacheType, int SizeCheck, typename LogStreamTrait, typename MergeTrait>
            class SequenceCache;

        template <typename CacheType, int SizeCheck, typename LogStreamTrait, typename MergeTrait>
            std::ostream& operator<<(std::ostream& os, const SequenceCache<CacheType, SizeCheck, LogStreamTrait, MergeTrait> & cache);  

        template <typename CacheType, int SizeCheck, typename LogStreamTrait, typename MergeTrait>
        class SequenceCache {
            public:
                typedef std::string key_type;

                SequenceCache (const std::shared_ptr<LogStreamTrait> &stream) :  _nextSeqNo(0), _stream(stream) {
                    static_assert(sizeof(CacheType) == SizeCheck, "CacheType Size Check");
						  static_assert(sizeof(TagCache) == 32, "TagCache Size Check");
                }

                virtual ~SequenceCache () {
                }

                int Restore (const CacheType &t) {
                    Store(t);
                    _nextSeqNo = std::max(_nextSeqNo+1, t._seqno);
                    return 0;
                }

                uint64_t NextSeq () {
                    return _nextSeqNo++;
                }

                uint64_t CheckSeq () {
                    return _nextSeqNo;
                }
                
                int Push (CacheType &t) {
                    t._seqno = NextSeq();
                    Store(t);
                    return _stream->Push(reinterpret_cast<const char *>(&t), SizeCheck);
                }

                // friend functions
                friend std::ostream& operator<< <> (std::ostream& os, const SequenceCache<CacheType, SizeCheck, LogStreamTrait, MergeTrait> & cache);  
            private:
                SequenceCache (const SequenceCache &other);
                SequenceCache &operator= (const SequenceCache &other);

                uint64_t _nextSeqNo;

                std::unordered_map <key_type, CacheType> _cacheMap;

                std::shared_ptr<LogStreamTrait> _stream;

                bool Store (const CacheType &c) {
                    key_type key(c._key);
                    auto i = _cacheMap.find(key);
                    if (i == _cacheMap.end()) {
                        return _cacheMap.insert(std::make_pair(key, c)).second;
                    } else {
                        if (c._seqno > (*i).second._seqno) {
                            (*i).second = c;
                        }
                    }

                    return true;
                }

                void Dump (std::ostream &out) const {
                    auto b = _cacheMap.begin();
                    auto e = _cacheMap.end();
                    for (;b!=e; ++b) {
                        out << (*b).first << " [" << (*b).second._seqno << "] O[" << (*b).second._origseqno << "], ";
                    }
                }
        };

        template <typename CacheType, int SizeCheck, typename LogStreamTrait, typename MergeTrait>
        std::ostream& operator<<  (std::ostream& os, const SequenceCache<CacheType, SizeCheck, LogStreamTrait, MergeTrait> & cache) {
            cache.Dump(os);
            return os;
        }
    }
}

#endif
