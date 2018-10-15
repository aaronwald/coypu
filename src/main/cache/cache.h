
#ifndef __COYPU_CACHE
#define __COYPU_CACHE

#include <stdint.h>

namespace coypu {
    namespace cache {
        class Cache {
            public:
                typedef uint32_t cache_id;

                Cache () {
                }

                virtual ~Cache () {
                }



                // support push partial and stream partial

                // swap seq no when a new doc is complete

                // stream out seqno. can only trim when no longer in use. 
            private:
                Cache (const Cache &other);
                Cache &operator= (const Cache &other);
        };
    }
}

#endif