#pragma once

#include <iostream>
#include <sys/uio.h>
#include <string.h>
#include <functional>
#include <algorithm>
#include <vector>
#include <queue>
#include <deque>
#include <memory>
#include <streambuf>
#include <type_traits>

namespace coypu {
  namespace store {
    // Simpler to make this a rolling buffer 
    template <typename MMapProvider>
    class LogWriteBuf {
      public:
		LogWriteBuf (off64_t pageSize, off64_t offset, int fd, bool readOnly, bool anonymous) :
		_pageSize(pageSize), _offset(offset), _fd(fd), _readOnly(readOnly), _dataPage(nullptr, 0), _anonymous(anonymous)  { 
        }

        virtual ~LogWriteBuf () {
          if (!_anonymous && _dataPage.first) {
            MMapProvider::MUnmap(_dataPage.first, _pageSize);
          }
        }

        int Push (const char *data, off64_t len) {
          if (_readOnly) return -1;
          off64_t off = 0;
          while (len) {
            if (!_dataPage.first || _dataPage.second == _pageSize) {
              int r = AllocatePage();
              if (r != 0) return r;
            }

            off64_t x = std::min(len, _pageSize - _dataPage.second);

            memcpy(&(_dataPage.first[_dataPage.second]), &data[off], x); // hacky copy for testing
            _dataPage.second += x;
            off += x;
            len -= x;
          }

          return 0; // can overflow
        }		  

        int Readv (int fd, std::function <int(int, const struct iovec *, int)> &cb) {
          if (_readOnly) return -1; // could have static coypu errno

          struct iovec iov;
          if (!_dataPage.first || _dataPage.second == _pageSize) {
            int r = AllocatePage();
            if (r != 0) return r;
          }

          iov.iov_base = &(_dataPage.first[_dataPage.second]);
          iov.iov_len = _pageSize - _dataPage.second;

          int r = cb(fd, &iov, 1);
          if (r < 0) return r;

          _dataPage.second += r;
          return r; // can overflow
        }

		  template <typename CacheType>
			 void CachePage(CacheType &cache) {
			 cache.AddPage(_dataPage.first, _dataPage.second);
		  }

		  void SetAllocateCB (std::function<void(char *, off64_t)> &allocate_cb) {
			 _allocate_cb = allocate_cb;
		  }

		  // For use with google buf
		  int ZeroCopyNext (void **data, int *len) {
			 if (_readOnly) return -1;

			 if (!_dataPage.first || _dataPage.second == _pageSize) {
				int r = AllocatePage();
				if (r != 0) return r;
			 }

			 *len = _pageSize - _dataPage.second;
			 *data = &(_dataPage.first[_dataPage.second]);
			 _dataPage.second = _pageSize; // assume all used
          
          return 0;
        }

		  bool Backup (int len) {
			 if (len > _dataPage.second) return false;
			 _dataPage.second -= len; // backup
			 return true;
		  }

		  bool SetPosition (off64_t offset) {
			 if (offset % _pageSize == 0) return true;
			 return PositionPage(offset) == 0;
		  }

      private:
        LogWriteBuf (const LogWriteBuf &other);
        LogWriteBuf &operator= (const LogWriteBuf &other);

		  int PositionPage (off64_t offset) {
			 _offset = (offset / _pageSize) * _pageSize; // to nearets page
			 
			 if (!_anonymous) {
				if (MMapProvider::LSeekSet(_fd, _offset) != _offset) {
				  return -2;
				}

				if (_dataPage.first) {
				  MMapProvider::MUnmap(_dataPage.first, _pageSize);
				}
			 }

			 _dataPage.first = reinterpret_cast<char *>(MMapProvider::MMapWrite(_fd, _offset, _pageSize));

			 if (_allocate_cb) {
				_allocate_cb(_dataPage.first, _offset);
			 }
			 
          if (!_dataPage.first) {
            return -3;
          }
          _dataPage.second = offset % _pageSize;

          return 0;

		  }

        int AllocatePage () {
			 if (!_anonymous) {
				if (MMapProvider::Truncate(_fd, _offset+_pageSize)) {
				  return -1;
				}
				
				if (MMapProvider::LSeekSet(_fd, _offset) != _offset) {
				  return -2;
				}

				if (_dataPage.first) {
				  MMapProvider::MUnmap(_dataPage.first, _pageSize);
				}
			 }

			 _dataPage.first = reinterpret_cast<char *>(MMapProvider::MMapWrite(_fd, _offset, _pageSize));

			 if (_allocate_cb) {
				_allocate_cb(_dataPage.first, _offset);
			 }
			 
          if (!_dataPage.first) {
            return -3;
          }
          _dataPage.second = 0;
          _offset += _pageSize;
          return 0;
        }

        off64_t _pageSize;
        off64_t _offset;
        int _fd;
        bool _readOnly;
        std::pair<char *, off64_t> _dataPage;
		  bool _anonymous;
		  std::function<void(char *, off64_t)> _allocate_cb;
    };

	 
	 template <typename LogStreamTrait>
		class logstreambuf : public std::streambuf {
	 public:
		logstreambuf (const std::shared_ptr<LogStreamTrait> &stream) : _stream(stream) {
		}
		
	 protected:
		virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override
		{
		  return _stream->Push(s, n);
		};
		
		virtual int_type overflow (int_type c) override {
		  if (c != EOF) {
			 return _stream->Push(reinterpret_cast<char *>(&c), 1);
		  } else {
			 return 0;
		  }
		}
		
	 private:
		const std::shared_ptr<LogStreamTrait> _stream;
	 };

    // class LogReadPageBuf
    // map / remap
    template <typename MMapProvider>
    class LogReadPageBuf {
      public:
        LogReadPageBuf (off64_t pageSize) : 
          _pageSize(pageSize),
          _dataPage(nullptr, UINT64_MAX) {
        }

			 LogReadPageBuf (off64_t pageSize, char *page, uint64_t offset) : 
          _pageSize(pageSize),
				_dataPage(page, offset) {
        }


        virtual ~LogReadPageBuf () {
          MMapProvider::MUnmap(_dataPage.first, _pageSize);
        }

        int Map (int fd, uint64_t offset) {
          if (_dataPage.second != offset) {
            MMapProvider::MUnmap(_dataPage.first, _pageSize);

            _dataPage.second = offset;
				assert(fd >= 0);
				_dataPage.first = reinterpret_cast<char *>(MMapProvider::MMapRead(fd, _dataPage.second, _pageSize));

            if (!_dataPage.first) {
              return -3;
            }
          }

          return 0;
        }

        // Absolute offset
        bool Peak (uint64_t offset, char &d) const {
          if (!_dataPage.first) return false;

          if ((offset >= _dataPage.second) &&
              (offset <= (_dataPage.second + _pageSize))) {
                d = _dataPage.first[offset-_dataPage.second];
            return true;
          }
          return false;
        }

		  bool ZeroCopyNextPeak (uint64_t offset, const void **data, int *len) {
			 if (!_dataPage.first) return false;

          if ((offset >= _dataPage.second) &&
              (offset <= (_dataPage.second + _pageSize))) {
				*data = &_dataPage.first[offset-_dataPage.second];
				*len = _pageSize - (offset-_dataPage.second);
				return true;
          }
          return false;
        }
        
        // Absolute offset
        bool Find (uint64_t start, char d, uint64_t &offset) const {
          for (uint64_t i = start; i < (_dataPage.second+_pageSize); ++ i) {
            if (_dataPage.first[i-_dataPage.second] == d) {
              offset = i;
              return true;
            }
          }
          return false;
        }

        // Absolute offset - copy (memcpy)
        bool Pop (uint64_t start, char *dest, uint64_t size, uint64_t &outSize) {
          if (!_dataPage.first) {
            return false;
          }

          if (start < _dataPage.second || start > (_dataPage.second+ _pageSize)) return false;

          outSize = std::min(size, _pageSize - (start-_dataPage.second));
          ::memcpy(dest, &_dataPage.first[start-_dataPage.second], outSize);
          return true;
        }
		  
		  bool Unmask (uint64_t start, uint64_t &maskPos, const char *mask, const int maskLen, uint64_t size, uint64_t &outSize) {
          if (!_dataPage.first) {
            return false;
          }

          if (start < _dataPage.second || start > (_dataPage.second+ _pageSize)) return false;

          outSize = std::min(size, _pageSize - (start-_dataPage.second));

			 //::memcpy(dest, &_dataPage.first[start-_dataPage.second], outSize);
			 for (uint64_t i = start-_dataPage.second; i < (start-_dataPage.second)+outSize; ++i) {
				_dataPage.first[i] ^= mask[maskPos%maskLen];
				++maskPos;
			 }				
			 
          return true;
        }


        // Absolute offset
        int Writev (uint64_t start, int fd, std::function <int(int, const struct iovec *, int)> &cb, uint64_t size) {
          if (!_dataPage.first) return false;
          if (start < _dataPage.second || start > (_dataPage.second+ _pageSize)) return false;
          if ((start + size) >= (_dataPage.second +_pageSize)) return false;

          struct iovec iov;
          iov.iov_base = &_dataPage.first[start-_dataPage.second];
          iov.iov_len = std::min(size, _pageSize - (start-_dataPage.second));
          if (iov.iov_len > 0) {
            return cb(fd, &iov, 1);
          }
          return 0;
        }

        char *GetBase (uint64_t offset) {
          return &_dataPage.first[offset];
            // &page->second->_dataPage._first[page_offset]; // not-portable

        }

		  off64_t GetOffset () {
			 return _dataPage.second;
		  }

		  void Unmap() {
			 MMapProvider::MUnmap(_dataPage.first, _pageSize);
		  }

      private:
        LogReadPageBuf (const LogReadPageBuf &other);
        LogReadPageBuf &operator= (const LogReadPageBuf &other);

        off64_t _pageSize;
        std::pair<char *, off64_t> _dataPage;
    };

	 // TODO one shot cache.  Dont call Map on LogReadPageBuf.
	 // When we read , we go through the offsets, and unmap anything old
	 // problem is the writebuf might call unmap unless we disable for anonymous
	 // one shot cache needs to clean up pages
	 template<typename MMapProvider, int CachePages>
		class OneShotCache {
      public:
        typedef LogReadPageBuf <MMapProvider> store_type;
        typedef std::shared_ptr<store_type> page_type; 
        typedef std::pair<uint32_t, page_type> pair_type;
        typedef std::shared_ptr<pair_type> read_cache_type;
        typedef uint32_t page_offset_type;
        typedef uint64_t offset_type;

        OneShotCache (off64_t pageSize, off64_t maxSize, int fd) : _pageSize(pageSize) {
        }

		  virtual ~OneShotCache () {
			 while (!_pages.empty()) {
				_pages.front()->second->Unmap();
				_pages.pop_front();
			 }
		  }

		  int PeakPage (offset_type offset, read_cache_type &page) {
			 auto b = _pages.begin();
			 auto e = _pages.end();

			 for (;b != e; ++b) {
				if (offset >= (*b)->first && offset < (*b)->first + _pageSize) {
				  page = *b;
				  return 0;
				}
			 }

			 return -1;
		  }
				  
		  int FindPage (offset_type offset, read_cache_type &page) {
			 while (!_pages.empty() && offset >= (_pages.front()->first + _pageSize)) {
				_pages.front()->second->Unmap();
				_pages.pop_front();

			 }

			 if (!_pages.empty()) {
				if (offset >= _pages.front()->first &&
					 offset < _pages.front()->first + _pageSize) {
				  page= _pages.front();
				  return 0;
				}
			 }
			 
			 return -1;
		  }

		  void AddPage (char *p, off64_t o) {
			 assert((o % _pageSize) == 0);
			 if (!_pages.empty()) {
				assert(_pages.back()->second->GetOffset() < o);
			 }

			 page_type page = std::make_shared<store_type>(_pageSize, p, o);
			 read_cache_type rc = std::make_shared<pair_type>(std::make_pair(o, page));
			 
			 _pages.push_back(rc);
		  }
		  
	 private:
		  OneShotCache (const OneShotCache &other);
		  OneShotCache &operator= (const OneShotCache &other);

		  uint64_t _pageSize;
		  std::deque<read_cache_type> _pages;
	 };
		  
    template<typename MMapProvider, int CachePages>
    class LRUCache {
      public:
        typedef LogReadPageBuf <MMapProvider> store_type;
        typedef std::shared_ptr<store_type> page_type; 
        typedef std::pair<uint32_t, page_type> pair_type;
        typedef std::shared_ptr<pair_type> read_cache_type;
        typedef uint32_t page_offset_type;
        typedef uint64_t offset_type;

        LRUCache (off64_t pageSize, off64_t maxSize, int fd) : 
            _pageSize(pageSize), _maxSize(maxSize), _fd(fd) {

        }

        virtual ~LRUCache () {
        }

		  int PeakPage (offset_type offset, read_cache_type &page) {
			 return FindPage(offset, page);
		  }

        int FindPage (offset_type offset, read_cache_type &page) {
          page_offset_type pageIndex = offset / _pageSize;

          typename std::deque<read_cache_type>::iterator b = _lruReadCache.begin();
          typename std::deque<read_cache_type>::iterator e = _lruReadCache.end();

          for (;b != e; ++b) {
            if ((*b)->first == pageIndex) {
              page = *b;

              if (b != _lruReadCache.begin()) {
                _lruReadCache.erase(b);         // erase
                _lruReadCache.push_front(page); // add to front - TODO optimize with bpf
              }

              return 0;
            }
          }

          if (_lruReadCache.size() == _maxSize) {
            page = _lruReadCache.back();                       // re-use object
            page->first = pageIndex;
            _lruReadCache.erase(--e);                          // erase
          } else {
            page_type psp = std::make_shared<store_type>(_pageSize);
            page = std::make_shared<pair_type>(std::make_pair(pageIndex, psp)); // allocate new page
          }
			 
          if(page->second->Map(_fd, pageIndex * _pageSize) == 0) {
            _lruReadCache.push_front(page); // add to front
            return 0;
          }

          page = nullptr; // just in case

          return -1;
        }

		  void AddPage (char *, off64_t) {
			 // nop
		  }

      private:
        LRUCache (const LRUCache &other);
        LRUCache &operator= (const LRUCache &other);
        static constexpr int iov_size = CachePages;

        uint16_t _cachePages;
        uint64_t _pageSize;
        uint64_t _maxSize; 
        int _fd;

        // page index, page
        std::deque<read_cache_type> _lruReadCache;
    };

    template <typename MMapProvider, template <typename, int> class ReadCache, int CacheSize>
    class LogRWStream {
      public:
        typedef uint32_t page_offset_type;
        typedef uint64_t offset_type;
        typedef char value_type;

        typedef LogRWStream<MMapProvider, ReadCache, CacheSize> log_type;

		  // anonymous=true will keep writebuf from unmap the write page
        LogRWStream (off64_t pageSize, offset_type offset, int fd, bool anonymous, offset_type maxSize = UINT64_MAX) :
		  _writeBuf(pageSize, offset, fd, false, anonymous),
          _readCache(pageSize, CacheSize, fd),
          _pageSize(pageSize),
          _available(offset),
          _fd(fd),
          _maxSize(maxSize) {
				if (!anonymous) {
				  assert(_fd > 0);
				}

				std::function<void(char *, off64_t)> cb = [this] (char *page, off64_t offset) {
				  this->_readCache.AddPage(page, offset);
				};
				_writeBuf.SetAllocateCB(cb);
        }

        virtual ~LogRWStream () {
        } 

        // https://gist.github.com/jeetsukumaran/307264
        // https://stackoverflow.com/questions/12092448/code-for-a-basic-random-access-iterator-based-on-pointers
        template <typename LogType>
        class store_iterator : public std::iterator<std::random_access_iterator_tag, char> {
          public:
              typedef store_iterator<LogType> iterator_type;
              typedef typename LogType::value_type value_type;
              typedef typename LogType::value_type& reference;
              typedef typename LogType::value_type* pointer;
              // typedef int difference_type;

              using difference_type = typename std::iterator<std::random_access_iterator_tag, char>::difference_type;

              store_iterator(LogType *log, offset_type offset) : _log(log), _offset(offset) { 

              }

              reference operator*() { 
                _log->Peak(_offset, _value);
                return _value; 
              }

              pointer operator->() { 
                _log->Peak(_offset, _value);
                return &_value; 
              }

              inline iterator_type& operator++() {++_offset; return *this;}
              inline iterator_type& operator--() {--_offset; return *this;}
              inline iterator_type operator++(int) const {iterator_type tmp(_log, *this); ++_offset; return tmp;}
              inline iterator_type operator--(int) const {iterator_type tmp(_log, *this); --_offset; return tmp;}
              /* inline Iterator operator+(const Iterator& rhs) {return Iterator(_ptr+rhs.ptr);} */
              inline difference_type operator-(const iterator_type& rhs) const {return _offset-rhs._offset;}
              inline iterator_type operator+(difference_type rhs) const {return iterator_type(_log, _offset+rhs);}
              inline iterator_type operator-(difference_type rhs) const {return iterator_type(_log, _offset-rhs);}
              friend inline iterator_type operator+(difference_type lhs, const iterator_type& rhs) {return iterator_type(rhs._log, lhs+rhs._offset);}
              friend inline iterator_type operator-(difference_type lhs, const iterator_type& rhs) {return iterator_type(rhs._log, lhs-rhs._offset);}


              inline bool operator==(const iterator_type& rhs) const {return _offset == rhs._offset;}
              inline bool operator!=(const iterator_type& rhs) const {return _offset != rhs._offset;}
              inline bool operator>(const iterator_type& rhs) const {return _offset > rhs._offset;}
              inline bool operator<(const iterator_type& rhs) const {return _offset < rhs._offset;}
              inline bool operator>=(const iterator_type& rhs) const {return _offset >= rhs._offset;}
              inline bool operator<=(const iterator_type& rhs) const {return _offset <= rhs._offset;}
          private:
              LogType *_log;
              offset_type _offset;

              char _value;
        };

        typedef store_iterator<log_type> iterator;

        iterator begin(offset_type offset) {
          return store_iterator<log_type>(this, offset);
        }

        iterator end(offset_type end) {
          return store_iterator<log_type>(this, end);
        }

        offset_type Available () const {
          return _available;
        }

        bool IsEmpty () const {
          return _available == 0;
        }

        inline offset_type Free() const {
          return Capacity() - Available();
        }

        offset_type Capacity() const {
          return _maxSize;
        }

		  int ZeroCopyWriteNext (void **data, int *len) {
			 int i = _writeBuf.ZeroCopyNext(data, len);
			 if (i == 0) _available += *len;
			 return 0;
		  }

		  void ZeroCopyWriteBackup (int len) {
			 _writeBuf.Backup(len);
			 _available -= len;
		  }

        int Push (const char *data, offset_type len) {
          int r = _writeBuf.Push(data,len);
          if (r == 0) _available += len;
          return r;
        }

        int Readv (int fd, std::function <int(int, const struct iovec *, int)> &cb) {
          int r = _writeBuf.Readv(fd, cb);
			 // LRU will ignore, 
          if (r > 0) _available += r;
          return r;
        }

        bool Peak (offset_type offset, char &d) {
          typename read_cache_type::read_cache_type page;
			 if (_readCache.PeakPage(offset, page)) return false;
          if (offset >= _available) return false;

          return page ? page->second->Peak(offset, d) : false;
        }

		  bool ZeroCopyReadNext (offset_type offset, const void **data, int *len) {
			 typename read_cache_type::read_cache_type page;
			 if (_readCache.PeakPage(offset, page)) return false;
          if (offset >= _available) return false;
          return page ? page->second->ZeroCopyNextPeak(offset, data,len) : false;
        }


		  bool Unmask (offset_type start_offset, offset_type len, const char *mask, int maskLen) {
			 page_offset_type startPage = start_offset / _pageSize;
			 page_offset_type maxPage = _available / _pageSize;
          typename read_cache_type::read_cache_type page;

          uint64_t read = 0, out_size = 0;
			 uint64_t maskPos = 0;
          for (page_offset_type i = startPage; i <= maxPage; ++i) {
            offset_type pageStart = i * _pageSize;

            if (_readCache.PeakPage(pageStart, page) == 0) {
              offset_type start_pos = std::max(start_offset, pageStart); 

              if (page->second && page->second->Unmask(start_pos, maskPos, mask, maskLen, len-read, out_size)) {
                read += out_size;
                if (read == len) return true;
              } else {
                return false;
              }
            }
          } 
          return false;
		  }
        
        bool Find (offset_type start_offset, char d, offset_type &offset) {
          page_offset_type startPage = start_offset / _pageSize;
          page_offset_type maxPage = _available / _pageSize;
          typename read_cache_type::read_cache_type page;

          for (page_offset_type i = startPage; i <= maxPage; ++i) {
            offset_type pageStart = i * _pageSize;

            if (_readCache.FindPage(pageStart, page) == 0) {
              offset_type start_pos = std::max(start_offset, pageStart); 

              if (page->second->Find(start_pos, d, offset)) {
                return true;
              }
            }
          }
          offset = UINT64_MAX;
          return false;
        }

        // copy
        bool Pop (offset_type start_offset, char *dest, uint64_t size) {
          page_offset_type startPage = start_offset / _pageSize;
          page_offset_type maxPage = _available / _pageSize;
          typename read_cache_type::read_cache_type page;

          uint64_t read = 0, out_size = 0;
          // offset_type end_offset = start_offset + size - 1;
          for (page_offset_type i = startPage; i <= maxPage; ++i) {
            offset_type pageStart = i * _pageSize;
            // offset_type pageEnd = pageStart + _pageSize - 1;

            if (_readCache.FindPage(pageStart, page) == 0) {
              offset_type start_pos = std::max(start_offset, pageStart); 
              // offset_type end_pos   = std::min(end_offset, pageEnd); - not needed?
            
              // Pop will only return min(page_size, size-read)
              if (page->second && page->second->Pop (start_pos, &dest[read], size-read, out_size)) {
                read += out_size;
                if (read == size) return true;
              } else {
                return false;
              }
            }
          } 
          return false;
        }

        // direct without a copy up to n bytes starting at start_offset
        // maps up to CacheSize pages which means this could fail if n is significantly large.
        int Writev (offset_type start_offset, offset_type size,
                    int fd, std::function <int(int, const struct iovec *, int)> &cb) {
          size = std::min(size, (CacheSize * _pageSize));
          struct iovec iov[CacheSize];

          typename read_cache_type::read_cache_type page;
          page_offset_type startPage = start_offset / _pageSize;
          page_offset_type maxPage = _available / _pageSize;

          // how to make sure pages are locked
          offset_type queued = 0;
          int iov_i = 0;
          for (page_offset_type i = startPage; queued < size && i <= maxPage && iov_i < CacheSize; ++i, ++iov_i) {
            offset_type pageStart = i * _pageSize;
            offset_type page_offset = (start_offset+queued) % _pageSize;

            if (_readCache.FindPage(pageStart, page) == 0) {
              iov[iov_i].iov_len = std::min((size-queued), _pageSize - page_offset);
              iov[iov_i].iov_base = page->second->GetBase(page_offset);
              queued += iov[iov_i].iov_len;
            } else {
              return -2;
            }
          }
          return cb(fd, iov, iov_i);
        }

		  bool SetPosition (off64_t offset) {
			 return _writeBuf.SetPosition(offset);
		  }

		  bool Backup (int count) {
			 return true;
		  }

		  bool Skip (int count) {
			 // nop - should be enable_if
			 return true;
		  }
		  
      private:
        LogRWStream (const LogRWStream &other);
        LogRWStream &operator= (const LogRWStream &other);

        LogWriteBuf <MMapProvider> _writeBuf;

        typedef ReadCache<MMapProvider, CacheSize>  read_cache_type;
        read_cache_type _readCache;

        uint64_t _pageSize;
        uint64_t _available;
        int      _fd;            // file descriptor
        uint64_t _maxSize;       // max size, defaults unbound
    };


    // Keeps track of current read position in stream
    template <typename S>
    class PositionedStream {
      public:
		//		constexpr static bool is_positioned = true;
		
        PositionedStream (const std::shared_ptr<S> &stream) : _stream(stream),
          _curOffset(stream->Available()) {
        }

        virtual ~PositionedStream () {
        }

        typename S::offset_type Available () const {
          return _stream->Available() - _curOffset;
        }

        bool Peak (typename S::offset_type offset, char &d) {
          return _stream->Peak(_curOffset+offset, d);
        }

		  bool Unmask (typename S::offset_type offset, typename S::offset_type len, const char *mask, int maskLen) {
			 return _stream->Unmask(offset, len, mask, maskLen);
		  }
        
        bool Find (typename S::offset_type  start_offset, char d, typename S::offset_type  &offset) {
          return _stream->Find(_curOffset+start_offset, d, offset);
        }

        bool Pop (char *dest, uint64_t size) {
          if (_stream->Pop(_curOffset, dest, size)) {
            _curOffset += size;
            return true;
          }
          return false;
        }

        bool Pop (char *dest, typename S::offset_type offset, uint64_t size) {
          if (_stream->Pop(offset, dest, size)) {
            return true;
          }
          return false;
        }

        int Readv (int fd, std::function <int(int, const struct iovec *, int)> &cb) {
          return _stream->Readv(fd, cb);
        }

        int Push (const char *data, typename S::offset_type len) {
          return _stream->Push(data, len);
        }

        int Writev (typename S::offset_type  size,
                    int fd, std::function <int(int, const struct iovec *, int)> &cb) {
          int r = _stream->Writev(_curOffset, size, fd, cb);
          if (r > 0) {
            _curOffset += r;
          }
          return r;
        }

		  bool Backup (typename S::offset_type  size) {
			 if (size <= _curOffset) {
				_curOffset -= size;
				return true;
			 }
			 return false;
		  }

        bool Skip (typename S::offset_type  size) {
          if (size <= Available()) {
            _curOffset += size;
            return true;
          }
          return false;
        }

		  void ResetPosition () {
			 _curOffset = _stream->Available();
		  }
				
        typename S::offset_type CurrentOffset() const {
          return _curOffset;
        }

		  bool ZeroCopyReadNext (typename S::offset_type offset, const void **data, int *len) {
			 bool b =  _stream->ZeroCopyReadNext(offset, data, len);
			 if (b) {
				_curOffset += *len;
			 }
			 return b;
		  }
        
      private:
        PositionedStream (const PositionedStream &other);
        PositionedStream &operator=(const PositionedStream &other);

        std::shared_ptr<S> _stream;
        typename S::offset_type _curOffset;
    };

    template <typename S>
    class MultiPositionedStreamLog {
      public:
        typedef typename S::offset_type offset_type;

        MultiPositionedStreamLog (const std::shared_ptr<S> &stream) : _stream(stream) {
        }

        virtual ~MultiPositionedStreamLog () {
        }

        int Push (const char *data, typename S::offset_type len) {
          // DTRACE_PROBE2(coypu, "multi-push", data, len);

          return _stream->Push(data, len);
        }

		  int ZeroCopyWriteNext (void **data, int *len) {
			 return _stream->ZeroCopyWriteNext(data, len);
		  }

		  void ZeroCopyWriteBackup (int len) {
			 _stream->ZeroCopyWriteBackup(len);
		  }

        int Register (int fd, uint64_t offset) {
			 if (fd+1 > _curOffsets.size()) {
            _curOffsets.resize(fd+1, UINT64_MAX);
			 }
			 assert(_curOffsets[fd] == UINT64_MAX);
            _curOffsets[fd] = offset;
            return 0;
        }

		  bool Mark (int fd, uint64_t offset) {
			 if (fd < _curOffsets.size()) {
				_curOffsets[fd] = offset;
				return true;
			 }
			 return false;
		  }
		  
		  bool MarkEnd (int fd) {
			 if (fd < _curOffsets.size()) {
				_curOffsets[fd] = _stream->Available();
				return true;
			 }
			 return false;
		  }

        int Unregister (int fd) {
            if (fd >= _curOffsets.size()) return -3;
            _curOffsets[fd] = UINT64_MAX;
            return 0;
        }

        int Writev (typename S::offset_type  size,
                    int fd, std::function <int(int, const struct iovec *, int)> &cb) {
          if (fd >= _curOffsets.size()) return -3;
          if (_curOffsets[fd] == UINT64_MAX) return 0; // no work
          int r = _stream->Writev(_curOffsets[fd], size, fd, cb);
          if (r > 0) {
            _curOffsets[fd] += r;
          }
          return r;
        }

		  typename S::offset_type Available () const {
			 return _stream->Available();
		  }
						
        typename S::offset_type Available (int fd) const {
          if (fd >= _curOffsets.size()) return 0;
          if (_curOffsets[fd] == UINT64_MAX) return 0; //unregistered

          return _stream->Available() - _curOffsets[fd];
        }

        // copy
        bool Pop (char *dest, typename S::offset_type  offset, typename S::offset_type size) {
          if (_stream->Available() < offset) return false;
          if (_stream->Available()+size < offset) return false;
          if (_stream->Pop(offset, dest, size)) {
            return true;
          }
          return false;
        }

        bool IsEmpty (int fd) const {
          if (fd >= _curOffsets.size()) return true;
          if (_curOffsets[fd] == UINT64_MAX) return true; //unregistered

          return _stream->Available() - _curOffsets[fd] == 0;
        }

        inline typename S::offset_type Free() const {
          return _stream->Free();
        }

        typename S::iterator begin(typename S::offset_type offset) {
          return _stream->begin(offset);
        }

        typename S::iterator end(typename S::offset_type end) {
          return _stream->end(end);
        }

      private:
        MultiPositionedStreamLog (const MultiPositionedStreamLog &other);
        MultiPositionedStreamLog &operator=(const MultiPositionedStreamLog &other);

        std::shared_ptr<S> _stream;
        std::vector<typename S::offset_type> _curOffsets;
    };
  }
}

