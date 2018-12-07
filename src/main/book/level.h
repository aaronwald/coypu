
#ifndef __COYPU_BOOK_LEVEL_H
#define __COYPU_BOOK_LEVEL_H

#include <stdint.h>

namespace coypu {
  namespace book {

	 template <typename T, uint32_t PageSize>
		class LevelAllocator {
	 public:
		LevelAllocator () : _curPage(nullptr), _curOffset(0) {
		  static_assert(PageSize%sizeof(T) == 0, "Type not page aligned");
		}

		virtual ~LevelAllocator () {
		}

		template <class ...Args>
		T * Allocate (Args... args) {
		  if (!_curPage || _curOffset == PageSize) {
			 _curOffset = 0;
			 _curPage = new char[PageSize];
		  }
		  
		  _curOffset += sizeof(T);

		  return new(_curPage+_curOffset-sizeof(T))T(args...);
		}

	 private:
		LevelAllocator (const LevelAllocator &other) = delete;
		LevelAllocator &operator= (const LevelAllocator &other) = delete;

		char *_curPage;
		uint32_t _curOffset;
	 };
  }
}

#endif
