
#ifndef __COYPU_CACHE
#define __COYPU_CACHE

#include <stdint.h>
#include <vector>
#include <memory>
#include <functional>

namespace coypu {
    namespace cache {
        template <typename CacheType, int SizeCheck, typename LogStreamTrait, typename MergeTrait>
        class Cache {
            public:
                typedef uint32_t cache_id;

                // restore cache from stream. 0 success, otherwise error
                // output next seq
                typedef std::function<int(std::shared_ptr<LogStreamTrait> &, uint64_t &)> restore_cb_type;

                Cache (const std::shared_ptr<LogStreamTrait> &stream) : _nextId(0), _step(32), _nextSeqNo(0), _stream(stream) {
                    static_assert(sizeof(CacheType) == SizeCheck, "CacheType Size Check");
                }

                virtual ~Cache () {
                }

                int Restore () {
                    if (_restore) {
                        return _restore(_stream, _nextSeqNo);
                    }
                    return -1;
                }

                cache_id Register () {
                    cache_id nextId = _nextId++;
                    if (nextId >= _cache.size()) {
                        _cache.resize(_cache.size() + _step);
                    }

                    return nextId;
                }

                uint64_t NextSeq () {
                    return _nextSeqNo++;
                }

                int Push (const char *data, typename LogStreamTrait::offset_type len) {
                    return _stream->Push(data, len);
                }

                // Cache is sequenced, Log is not. 
                // Log (PStore) - Merge (Cache)
                // swap seq no when a new doc is complete
                // stream out seqno. can only trim when no longer in use (can compress old values)
                // cacheid -> (seqno) (1 cache line) or cache_id -> (offset, len) (2 cache lines)
                // effectively key->value

                // https://github.com/nlohmann/json.git  ( v3.3.0 )

                // need boost
                // https://github.com/apache/arrow.git ( apache-arrow-0.11.1 )
                // https://github.com/apache/parquet-cpp.git ( apache-parquet-cpp-1.5.0 )

                // TODO 0. Parse json (nlohmann). For doc we receive. write a simpler record , security / value
                // TODO 1. Restore Page Function - Need read record function
                // TODO 2. Sort Page
                // TODO 3. Merge Page Function to disk
                // TODO 4. Compress page
                // TODO 5. Search on disk

                // page compress thread - read and write out new page on disk that is sorted (LSM)
                // sort and compress pages to get latest per page
                // group pages for more
                // bloom filter for lots of keys to find pages

                // latest in memory - cap
                // if not found then search through 
            private:
                Cache (const Cache &other);
                Cache &operator= (const Cache &other);

                cache_id _nextId;
                uint16_t _step;
                uint64_t _nextSeqNo;

                std::vector <CacheType> _cache;
                std::shared_ptr<LogStreamTrait> _stream;
                restore_cb_type _restore;
        };
    }
}

#endif