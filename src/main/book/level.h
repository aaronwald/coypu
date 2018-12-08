
#ifndef __COYPU_BOOK_LEVEL_H
#define __COYPU_BOOK_LEVEL_H

#include <stdint.h>
#include <memory>
#include <vector>
#include <iostream>
#include <functional>

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
		typedef std::function<bool(T *lhs, T *rhs)> comparator_type;
		CLevelBook (comparator_type cmp) : _cmpf(cmp) {
		}

		virtual ~CLevelBook () {
		}

		bool Insert (T *t, int &index) {
		  auto lb =  std::lower_bound(v.rbegin(), v.rend(), t, _cmpf);
 		  index = lb - v.rbegin();
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

		T * Erase (uint64_t px, int &index) {
		  index = -1;
		  T t;
		  t.px = px;
		  // lower bound is strictly lower so value should be equal to or not found
		  auto lb = std::lower_bound(v.rbegin(), v.rend(), &t, _cmpf);
		  if (lb != v.rend() && px == (*lb)->px) {
			 index = (lb.base()-1) - v.begin();
			 v.erase(--lb.base());
			 return *lb;
		  }
		  return nullptr;
		}

		bool Update (uint64_t px, uint64_t qty, int &index) {
		  T t;
		  t.px = px;
		  // lower bound is strictly lower so value should be equal to or not found
		  auto lb = std::lower_bound(v.rbegin(), v.rend(), &t, _cmpf);
		  if (lb != v.rend() && px == (*lb)->px) {
			 index = lb - v.rbegin();
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
  
		comparator_type _cmpf;
	 };

	 // Fwd book we can flip ordering with the comparator for lower bound
	 template <typename T>
		class CLevelFwdBook {
	 public:
		typedef std::function<bool(T *lhs, T *rhs)> comparator_type;
		CLevelFwdBook (comparator_type cmp) : _cmpf(cmp) {
		}

		virtual ~CLevelFwdBook () {
		}

		// O(1)
		bool GetLevel (int level,  T &t) const {
		  if (level < v.size()) {
			 t = *v[level];
			 return true;
		  }
		  return false;
		}

		bool GetFront (T &t) const {
		  if (!v.empty()) {
			 t = v.front();
			 return true;
		  }
		  return false;
		}

		bool GetBack (T &t) const {
		  if (!v.empty()) {
			 t = v.back();
			 return true;
		  }
		  return false;
		}

		// O(log n) - binary search
		bool Insert (T *t, int &index) {
		  auto lb =  std::lower_bound(v.begin(), v.end(), t, _cmpf);
 		  index = lb - v.begin();
		  v.insert(lb, t);

		  return true;
		}

		// O(log n) - binary search
		T * Erase (uint64_t px, int &index) {
		  index = -1;
		  T t;
		  t.px = px;
		  // lower bound is strictly lower so value should be equal to or not found
		  auto lb = std::lower_bound(v.begin(), v.end(), &t, _cmpf);
		  if (lb != v.end() && px == (*lb)->px) {
			 index = lb - v.begin();
			 v.erase(lb);
			 return *lb;
		  }
		  return nullptr;
		}

		// O(log n) - binary search
		bool Update (uint64_t px, uint64_t qty, int &index) {
		  T t;
		  t.px = px;
		  auto lb = std::lower_bound(v.begin(), v.end(), &t, _cmpf);
		  if (lb != v.end() && px == (*lb)->px) {
			 index = lb - v.begin();
			 (*lb)->qty = qty;
			 return true;
		  }
		  return false;
		}

		void Dump () const {
		  for (auto b = v.begin(); b != v.end(); ++b) {
			 std::cout << "\t" << (*b)->qty << "\t" << (*b)->px << std::endl;
		  }
		}

		void Dump (int levels) const {
		  auto b = v.begin();
		  auto e = v.end();
		  for (int i = 0; i < levels && b != e; ++b) {
			 std::cout << "\t" << (*v)->qty << "\t" << (*v)->px << std::endl;		
		  }
		}

		void RDump (int levels) const {
		  auto b = v.rbegin();
		  auto e = v.rend();
		  for (int i = 0; i < levels && b != e; ++b) {
			 std::cout << "\t" << (*v)->qty << "\t" << (*v)->px << std::endl;		
		  }
		}

	 private:
		std::vector <T *> v;
		CLevelFwdBook (const CLevelFwdBook &other) = delete;
		CLevelFwdBook &operator= (const CLevelFwdBook &other) = delete;
  
		comparator_type _cmpf;
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
		  int oi = -1;
		  T *t = _bids.Erase(px, oi);
		  if (t) {
			 Free(t);
			 return true;
		  } 
		  return false;
		}

		bool EraseAsk (uint64_t px) {
		  int oi = -1;
		  T *t = _asks.Erase(px, oi);
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
