
#ifndef __COYPU_BOOK_LEVEL_H
#define __COYPU_BOOK_LEVEL_H

#include <stdint.h>
#include <memory>
#include <vector>
#include <iostream>

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
			 _pages.push_back(std::shared_ptr<char>(_curPage, [](char *p) { delete[] p; }));
		  }
		  
		  _curOffset += sizeof(T);

		  return new(_curPage+_curOffset-sizeof(T))T(args...);
		}

	 private:
		LevelAllocator (const LevelAllocator &other) = delete;
		LevelAllocator &operator= (const LevelAllocator &other) = delete;

		char *_curPage;
		uint32_t _curOffset;

		std::vector<std::shared_ptr<char>> _pages;
	 };


	 template <typename T>
		class CLevelBook {
	 public:
		CLevelBook () {
		}

		virtual ~CLevelBook () {
		}

		bool Insert (T *t) {
		  auto lb =  std::lower_bound(v.rbegin(), v.rend(), t, _cmp);
		  v.insert(lb.base(), t);
		  return true;
		}

		bool GetLevel (int level,  T &t) {
		  if (level < v.size()) {
			 t = *v[level];
			 return true;
		  }
		  return false;
		}

		T * Erase (uint64_t px) {
		  T t;
		  t.px = px;
		  // lower bound is strictly lower so value should be equal to or not found
		  auto lb = std::lower_bound(v.rbegin(), v.rend(), &t, _cmp);
		  if (lb != v.rend() && px == (*lb)->px) {
			 v.erase(--lb.base());
			 return *lb;
		  }
		  return nullptr;
		}

		bool Update (uint64_t px, uint64_t qty) {
		  T t;
		  t.px = px;
		  // lower bound is strictly lower so value should be equal to or not found
		  auto lb = std::lower_bound(v.rbegin(), v.rend(), &t, _cmp);
		  if (lb != v.rend() && px == (*lb)->px) {
			 (*lb)->qty = qty;
			 return true;
		  }
		  return false;
		}

		void Dump () {
		  for (auto b = v.begin(); b != v.end(); ++b) {
			 std::cout << "\t" << (*b)->qty << "\t" << (*b)->px << std::endl;
		  }
		}

		void Dump (int levels) {
		  for (int i = 0; i < levels && i < v.size(); ++i) {
			 std::cout << "\t" << v[i]->qty << "\t" << v[i]->px << std::endl;		
		  }
		}

	 private:
		std::vector <T *> v;
		CLevelBook (const CLevelBook &other) = delete;
		CLevelBook &operator= (const CLevelBook &other) = delete;
  
		T _cmp;
	 };

	 template <typename T, uint32_t PageSize>
		class CBook {
	 public:
		CBook () : _freeList (nullptr) {
		}

		virtual ~CBook () {
		}

		template <class ...Args>
		  T * Allocate (Args... args) {
		  if (_freeList) {
			 T * temp = _freeList;
			 _freeList = temp->next;
			 temp->next = nullptr;
			 return temp;
		  }

		  return _la.Allocate(args...);
		}

		void Free (T *t) {
		  assert(t);

		  t->next = _freeList;
		  _freeList = t;
		}

		bool EraseBid (uint64_t px) {
		  T *t = _bids.Erase(px);
		  if (t) {
			 Free(t);
			 return true;
		  } 
		  return false;
		}

		bool EraseAsk (uint64_t px) {
		  T *t = _asks.Erase(px);
		  if (t) {
			 Free(t);
			 return true;
		  } 
		  return false;
		}

		
	 private:
		CBook (const CBook &other) = delete;
		CBook &operator= (const CBook &other) = delete;

		T *_freeList;
		LevelAllocator <T, PageSize> _la;
		CLevelBook <T> _bids;
		CLevelBook <T> _asks;

	 };

  }
}

#endif
