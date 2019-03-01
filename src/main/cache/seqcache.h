#pragma once

#include <stdint.h>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include <string>
#include <iostream>
#include <streambuf>
#include "rigtorp/Seqlock.h"

// use Seqlock to allow reads (spin read - write is non-block)
namespace coypu {
  namespace cache {
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
		
		bool Load (const key_type &key, CacheType &out) {
		  auto b = _cacheMap.find(key);
		  if (b != _cacheMap.end()) {
			 out = (*b).second->load();
			 return true;
		  }
		  return false;
		}

		// friend functions
		friend std::ostream& operator<< <> (std::ostream& os, const SequenceCache<CacheType, SizeCheck, LogStreamTrait, MergeTrait> & cache);  
	 private:
		SequenceCache (const SequenceCache &other);
		SequenceCache &operator= (const SequenceCache &other);

		uint64_t _nextSeqNo;

		typedef std::shared_ptr<rigtorp::Seqlock<CacheType>> store_type;
		std::unordered_map <key_type, store_type> _cacheMap;

		std::shared_ptr<LogStreamTrait> _stream;

		bool Store (const CacheType &c) {
		  key_type key(c._key);
		  auto i = _cacheMap.find(key);
		  if (i == _cacheMap.end()) {
			 store_type sp = std::make_shared<rigtorp::Seqlock<CacheType>>();
			 sp->store(c);
			 return _cacheMap.insert(std::make_pair(key,sp)).second;
		  } else {
			 (*i).second->store(c);
		  }

		  return true;
		}


		void Dump (std::ostream &out) const {
		  auto b = _cacheMap.begin();
		  auto e = _cacheMap.end();
		  for (;b!=e; ++b) {
			 CacheType z = (*b).second->load();
			 out << (*b).first << " [" << z._seqno << "] O[" << z._origseqno << "], ";
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


