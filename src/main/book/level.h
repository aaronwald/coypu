
#ifndef __COYPU_BOOK_LEVEL_H
#define __COYPU_BOOK_LEVEL_H

#include <stdint.h>
#include <memory>
#include <vector>
#include <iostream>
#include <functional>

namespace coypu
{
namespace book
{
template <typename T, uint32_t PageSize>
class LevelAllocator
{
public:
	LevelAllocator() : _curPage(nullptr), _curOffset(0)
	{
		static_assert(PageSize % sizeof(T) == 0, "Type not page aligned");
	}

	virtual ~LevelAllocator()
	{
	}

	template <class... Args>
	T *Allocate(Args... args)
	{
		if (!_curPage || _curOffset == PageSize)
		{
			_curOffset = 0;
			_curPage = new char[PageSize];
			_pages.push_back(std::shared_ptr<char>(_curPage, [](char *p) { delete[] p; }));
		}

		_curOffset += sizeof(T);

		return new (_curPage + _curOffset - sizeof(T)) T(args...);
	}

private:
	LevelAllocator(const LevelAllocator &other) = delete;
	LevelAllocator &operator=(const LevelAllocator &other) = delete;

	char *_curPage;
	uint32_t _curOffset;

	std::vector<std::shared_ptr<char>> _pages;
};

template <typename T>
class CLevelBook
{
public:
	typedef std::function<bool(T *lhs, T *rhs)> comparator_type;
	CLevelBook(comparator_type cmp) : _cmpf(cmp)
	{
	}

	virtual ~CLevelBook()
	{
	}

	bool Insert(T *t, int &index)
	{
		auto lb = std::lower_bound(v.rbegin(), v.rend(), t, _cmpf);
		index = lb - v.rbegin();
		v.insert(lb.base(), t);

		return true;
	}

	bool GetLevel(int level, T &t)
	{
		if (level < v.size())
		{
			t = *v[level];
			return true;
		}
		return false;
	}

	T *Erase(uint64_t px, int &index)
	{
		index = -1;
		T t;
		t.px = px;
		// lower bound is strictly lower so value should be equal to or not found
		auto lb = std::lower_bound(v.rbegin(), v.rend(), &t, _cmpf);
		if (lb != v.rend() && px == (*lb)->px)
		{
			index = (lb.base() - 1) - v.begin();
			v.erase(--lb.base());
			return *lb;
		}
		return nullptr;
	}

	bool Update(uint64_t px, uint64_t qty, int &index)
	{
		T t;
		t.px = px;
		// lower bound is strictly lower so value should be equal to or not found
		auto lb = std::lower_bound(v.rbegin(), v.rend(), &t, _cmpf);
		if (lb != v.rend() && px == (*lb)->px)
		{
			index = lb - v.rbegin();
			(*lb)->qty = qty;
			return true;
		}
		return false;
	}

	void Dump()
	{
		for (auto b = v.begin(); b != v.end(); ++b)
		{
			std::cout << "\t" << (*b)->qty << "\t" << (*b)->px << std::endl;
		}
	}

	void Dump(int levels)
	{
		for (int i = 0; i < levels && i < v.size(); ++i)
		{
			std::cout << "\t" << v[i]->qty << "\t" << v[i]->px << std::endl;
		}
	}

private:
	std::vector<T *> v;
	CLevelBook(const CLevelBook &other) = delete;
	CLevelBook &operator=(const CLevelBook &other) = delete;

	comparator_type _cmpf;
};

// Fwd book we can flip ordering with the comparator for lower bound
template <typename T>
class CLevelFwdBook
{
public:
	typedef std::function<bool(T *lhs, T *rhs)> comparator_type;
	CLevelFwdBook(comparator_type cmp) : _cmpf(cmp)
	{
	}

	virtual ~CLevelFwdBook()
	{
	}

	// O(1)
	bool GetLevel(int level, T &t) const
	{
		if (level < v.size())
		{
			t = *v[level];
			return true;
		}
		return false;
	}

	bool GetFront(T &t) const
	{
		if (!v.empty())
		{
			t = v.front();
			return true;
		}
		return false;
	}

	bool GetBack(T &t) const
	{
		if (!v.empty())
		{
			t = v.back();
			return true;
		}
		return false;
	}

	// O(log n) - binary search
	bool Insert(T *t, int &index)
	{
		auto lb = std::lower_bound(v.begin(), v.end(), t, _cmpf);
		index = lb - v.begin();
		v.insert(lb, t);

		return true;
	}

	// O(log n) - binary search
	T *Erase(uint64_t px, int &index)
	{
		index = -1;
		T t;
		t.px = px;
		// lower bound is strictly lower so value should be equal to or not found
		auto lb = std::lower_bound(v.begin(), v.end(), &t, _cmpf);
		if (lb != v.end() && px == (*lb)->px)
		{
			index = lb - v.begin();
			T *t = *lb;
			v.erase(lb);

			return t;
		}
		return nullptr;
	}

	// O(log n) - binary search
	bool Update(uint64_t px, uint64_t qty, int &index)
	{
		T t;
		t.px = px;
		auto lb = std::lower_bound(v.begin(), v.end(), &t, _cmpf);
		if (lb != v.end() && px == (*lb)->px)
		{
			index = lb - v.begin();
			(*lb)->qty = qty;
			return true;
		}
		return false;
	}

	void Dump(int levels = 0) const
	{
		auto b = v.begin();
		auto e = v.end();
		for (int i = 0; (levels == 0 || i < levels) && b != e; ++b, ++i)
		{
			std::cout << "\t" << (*b)->qty << "\t" << (*b)->px << std::endl;
		}
	}

	void RDump(int levels = 0) const
	{
		auto b = v.rbegin();
		auto e = v.rend();
		for (int i = 0; (levels == 0 || i < levels) && b != e; ++b, ++i)
		{
			std::cout << "\t" << (*b)->qty << "\t" << (*b)->px << std::endl;
		}
	}

private:
	std::vector<T *> v;
	CLevelFwdBook(const CLevelFwdBook &other) = delete;
	CLevelFwdBook &operator=(const CLevelFwdBook &other) = delete;

	comparator_type _cmpf;
};

template <typename T, uint32_t PageSize>
class CBook
{
public:
	CBook() : _freeList(nullptr),
		_bids ([] (const T *lhs, const T *rhs) -> bool { return lhs->px < rhs->px; }),
		_asks ([] (const T *lhs, const T *rhs) -> bool { return lhs->px > rhs->px; }) 
		{}

	virtual ~CBook() {}

	bool InsertBid(uint64_t px, uint64_t qty, int &index) {
		T *t = Allocate(px, qty);
		assert(t);
		return _bids.Insert(t, index);
	}

	bool InsertAsk(uint64_t px, uint64_t qty, int &index) {
		T *t = Allocate(px, qty);
		assert(t);
		return _asks.Insert(t, index);
	}

	bool UpdateBid(uint64_t px, uint64_t qty, int &index) {
		return _bids.Update(px, qty, index);
	}
	
	bool UpdateAsk(uint64_t px, uint64_t qty, int &index) {
		return _asks.Update(px, qty, index);
	}

	void DumpBid (int levels = 0) {
			_bids.Dump(levels);
	}

	void DumpAsk (int levels = 0) {
			_asks.Dump(levels);
	}

	void RDumpBid (int levels = 0) {
			_bids.RDump(levels);
	}

	void RDumpAsk (int levels = 0) {
			_asks.RDump(levels);
	}

	bool EraseBid(uint64_t px, int &index)
	{
		T *t = _bids.Erase(px, index);
		if (t)
		{
			assert(t->px == px);
			Free(t);
			return true;
		}
		return false;
	}

	bool EraseAsk(uint64_t px, int &index)
	{
		T *t = _asks.Erase(px, index);
		if (t)
		{
			assert(t->px == px);
			Free(t);
			return true;
		}
		return false;
	}

private:
	CBook(const CBook &other) = delete;
	CBook &operator=(const CBook &other) = delete;

	template <class... Args>
	T *Allocate(Args... args)
	{
		if (_freeList)
		{
			T *temp = _freeList;
			_freeList = temp->next;
			temp->next = nullptr;
			temp->Set(args...);
			return temp;
		}

		return _la.Allocate(args...);
	}

	void Free(T *t)
	{
		assert(t);
		t->px = t->qty = 0;

		t->next = _freeList;
		_freeList = t;
	}

	T *_freeList;
	LevelAllocator<T, PageSize> _la;
	CLevelFwdBook<T> _bids;
	CLevelFwdBook<T> _asks;
};

} // namespace book
} // namespace coypu

#endif
