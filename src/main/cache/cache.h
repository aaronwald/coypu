
#ifndef __COYPU_CACHE
#define __COYPU_CACHE

#include <stdint.h>
#include <vector>

namespace coypu {
    namespace cache {
        template <typename MergeTrait, typename StreamTrait>
        class Cache {
            public:
                typedef uint32_t cache_id;
                typedef uint64_t seq_no;

                Cache () : _nextId(0), _step(32) {
                    static_assert(sizeof(CacheValue) == 32, "Cache Page Size Check");
                }

                virtual ~Cache () {
                }

                cache_id Register () {
                    cache_id nextId = _nextId++;
                    if (nextId >= _cache.size()) {
                        _cache.resize(_cache.size() + _step);
                    }

                    return nextId;
                }

                bool Update (cache_id id, uint64_t offset, uint64_t len) {
                    if (id >= _cache.size()) return false;
                    _cache[id]._offset = offset;
                    _cache[id]._len = len;
                    return true;
                }

                struct CacheValue {
                    cache_id  _id;      // 4
                    char      _pad[4];  // 8
                    uint64_t  _offset;  // 16
                    uint64_t  _len;     // 24
                    char      _pad2[8]; // 32
                } __attribute__ ((packed, aligned(32)));

                // Cache is sequenced, Log is not. 
                // Log (PStore) - Merge (Cache)
                // swap seq no when a new doc is complete
                // stream out seqno. can only trim when no longer in use (can compress old values)
                // cacheid -> (seqno) (1 cache line) or cache_id -> (offset, len) (2 cache lines)
                // effectively key->value

                // page compress thread - read and write out new page on disk that is sorted (LSM)
                // sort and compress pages to get latest per page
                // group pages for more
                // bloom filter for lots of keys to find pages

                // How would tags be stored? 
                // <Tag>=<Value> 
                // tag can be translated to id

                // latest in memory - cap
                // if not found then search through 
            private:
                Cache (const Cache &other);
                Cache &operator= (const Cache &other);

                cache_id _nextId;
                uint16_t _step;

                std::vector <CacheValue> _cache;
        };
    }
}

#endif