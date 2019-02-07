/*
Copyright 2018 Aaron Wald

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#ifndef __COYPU_BUF_H
#define __COYPU_BUF_H

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <sys/uio.h>
#include <iostream>

#include <algorithm>
#include <functional>

// https://www.codeproject.com/Articles/3479/The-Bip-Buffer-The-Circular-Buffer-with-a-Twist
namespace coypu {
    namespace buf {
        template <typename DataType, typename CapacityType>
        class BipBuf {
            public:
                // Data is passed in to avoid exception in constructor and allow for inplace bipbuf
                BipBuf (DataType * data, CapacityType capacity) : 
                _head(0), _tail(0), _data(data), _capacity(capacity), _full(false) {
                }

                virtual ~BipBuf () {
                }

                inline CapacityType Head() const {
                    return _head;
                }

                inline CapacityType Tail() const {
                    return _tail;
                }

                inline CapacityType Available() const {
                    if (_full) return _capacity;
                    if (_head >= _tail) return _head - _tail;
                    else return _capacity - (_tail - _head);
                }

                inline CapacityType Capacity() const {
                    return _capacity;
                }

                inline CapacityType Free() const {
                    return Capacity() - Available();
                }

                inline bool IsEmpty () const {
                    if (_head == _tail && !_full) return true;
                    return false;
                }

                bool Peak (CapacityType offset, DataType &d) const {
                    if (offset >= Available()) return false;

                    if (_head > _tail) {
                        d =  _data[_tail + offset];
                    } else if (offset < (_capacity - _tail)) {
                        d  = _data[_tail + offset];
                    } else {
                        d  = _data[offset - (_capacity - _tail)];
                    }

                    return true;
                }

					 bool Unmask (CapacityType offset, CapacityType len, const char *mask, int maskLen) {
						assert(false);
						return false;
					 }

                bool Find (DataType d, CapacityType &offset) const {
                    offset = 0;
                    if (IsEmpty()) return false;

                    if (_head > _tail) {
                        for (CapacityType x = _tail; x < _head; ++x, ++offset) {
                            if (_data[x] == d) return true;
                        }
                    } else {
                        for (CapacityType x = _tail; x < _capacity; ++x, ++offset) {
                            if (_data[x] == d) return true;
                        }

                        for (CapacityType x = 0; x < _head; ++x, ++offset) {
                            if (_data[x] == d) return true;
                        }
                    }

                    offset = 0;
                    return false;
                }

                bool Push (const DataType indata) {
                    if (_full) return false;

                    _data[_head++] = indata;
                    if (_head == _capacity) _head = 0;
                    _full = _head == _tail;

                    return true;
                }

                bool Push (const DataType * indata, CapacityType size) {
                    if (!indata || size > (Capacity() - Available())) {
                        return false;
                    }
                    if (size == 0 || _full) return false;

                    CapacityType remaining = size;
                    CapacityType offset = 0;

                    if (_head >= _tail && _head < _capacity) {
                        CapacityType x = std::min(size, (_capacity - _head));
                        ::memcpy(&_data[_head], indata, sizeof(DataType) * x);
                        _head += x;
                        if (_head == _capacity) _head = 0;

                        offset += x;
                        remaining -= x;
                    }

                    if (remaining) {
                        ::memcpy(&_data[_head], &indata[offset], sizeof(DataType) * remaining);
                        _head += remaining;
                    }

                    _full = _head == _tail;

                    return true;
                }
					 
					 bool PushDirect (void ** indata, CapacityType *size) {
                    if (size == 0 || _full) return false;

                    if (_head >= _tail) {
							 *size = _capacity-_head;
							 *indata = &_data[_head];
							 
							 _head += *size;
							 if (_head == _capacity) _head = 0;
                    } else {
							 *size = _tail - _head;
							 *indata = &_data[_head];
							 _head += *size;
                    }

                    _full = _head == _tail;

                    return true;
                }

					 bool BackupDirect (CapacityType count) {
						if (count == 0) return true;
						if (count > Available()) return false;
						if (IsEmpty()) return false;

						
						if (_head > _tail) {
						  _head -= count;
						} else {
						  CapacityType r = std::min(_head, count);
						  _head -= r;
						  count -= r;
						  if (count) {
							 _head = _capacity - count;
						  }
						}

						_full = false;

						return true;
					 }



                bool Pop (DataType *dest, CapacityType size, bool peak=false) {
                    if (size > Available()) return false;
                    if (size == 0) return false;

                    CapacityType remaining = size;
                    CapacityType offset = 0;

                    if (_tail > _head || _full) {
                        CapacityType x = std::min(size, (_capacity - _tail));
                        ::memcpy(&dest[offset], &_data[_tail], sizeof(DataType) * x);

                        if (!peak) {
                            _tail += x;
                            if (_tail == _capacity) _tail = 0;
                        }

                        offset += x;
                        remaining -= x;
                    }

                    if (!peak) {
                        _full = false;
                    }

                    if (!peak && remaining) {
                        ::memcpy(&dest[offset], &_data[_tail], sizeof(DataType) * remaining);
                        _tail += remaining;
                    } else if (peak && remaining) {
                        ::memcpy(&dest[offset], &_data[0], sizeof(DataType) * remaining); // would be at head _tail=0
                    }

                    return true;
                }

                bool PopAll (const std::function<bool(const DataType*, CapacityType)> &cb, CapacityType size) {
                    if (size > Available()) return false;
                    if (size == 0) return false;

                    CapacityType remaining = size;
                    CapacityType offset = 0;

                    if (_tail > _head || _full) {
                        CapacityType x = std::min(size, (_capacity - _tail));
                        if (!cb(&_data[_tail], x)) return false;
                        _tail += x;
                        if (_tail == _capacity) _tail = 0;

                        offset += x;
                        remaining -= x;
                    }

                    _full = false;

                    if (remaining) {
                        if (!cb(&_data[_tail], remaining)) return false;

                        _tail += remaining;
                    }

                    return true;
                }

                int Read (int fd, const std::function <int(int, void *, size_t)> &cb) {
                    if (_full) return -2;
                    int ret = 0;

                    if (_tail > _head) {
                        ret = cb(fd, &_data[_head], _tail - _head);
                        if (ret > 0) _head += ret;
                    } else {
                        ret = cb(fd, &_data[_head], _capacity - _head);
                        if (ret > 0) _head += ret;
                    }

                    if (ret > 0) {
                        _full = _head == _tail;
                    }

                    return ret;
                }

                int Write (int fd, const std::function <int(int, void *, size_t)> &cb) {
                    if (Available() == 0) return -2;

                    int ret = 0;

                    if (_head > _tail) {
                        ret = cb(fd, &_data[_tail], _head - _tail);
                        if (ret > 0) _tail += ret;
                    } else {
                        ret = cb(fd, &_data[_tail], _capacity - _tail);
                        if (ret > 0) _tail += ret;
                    }

                    if (_tail == _capacity) _tail = 0;

                    if (ret > 0) {
                        _full = false;
                    }

                    return ret;
                }

                int Writev (int fd, const std::function <int(int, const struct iovec *, int)> &cb) {
                    if (Available() == 0) return -2;

                    int ret = 0;
                    struct iovec v[2];

                    if (_head > _tail) {
                        v[0].iov_base = &_data[_tail];
                        v[0].iov_len = _head - _tail;
                        ret = cb(fd, v, 1);

                        if (ret > 0) _tail += ret;
                    } else {
                        if (_head > 0) {
                            int x = _capacity - _tail;
                            v[0].iov_base = &_data[_tail];
                            v[0].iov_len = x;
                            v[1].iov_base = &_data[0];
                            v[1].iov_len = _head;

                            ret = cb(fd, v, 2);

                            if (ret > 0) {
                                if (ret > x) {
                                    _tail = ret - x;
                                } else if (ret > 0) {
                                    _tail += ret;
                                }
                            }
                        } else {
                            v[0].iov_base = &_data[_tail];
                            v[0].iov_len = _tail - _capacity;
                            ret = cb(fd, v, 1);

                            if (ret > 0) {
                                _tail += ret;
                            }
                        }
                    }

                    if (_tail == _capacity) _tail = 0;

                    if (ret > 0) {
                        _full = false;
                    }

                    return ret;
                }


                int Readv (int fd, const std::function <int(int, const struct iovec *, int)> &cb) {
                    if (_full) return -2;

                    int ret = 0;
                    struct iovec v[2];

                    if (_tail > _head) {
                        v[0].iov_base = &_data[_head];
                        v[0].iov_len = _tail - _head;
                        ret = cb(fd, v, 1);
                        if (ret > 0) _head += ret;
                    } else {
                        // empty
                        if (_head == 0 && _tail == 0) {
                            v[0].iov_base = &_data[_head];
                            v[0].iov_len = _capacity;

                            ret= cb(fd, v, 1);
                            if (ret > 0) _head += ret;
                        } else {
                            int x = _capacity - _head;
                            v[0].iov_base = &_data[_head];
                            v[0].iov_len = x;
                            v[1].iov_base = &_data[0];
                            v[1].iov_len = _tail;

                            ret = cb(fd, v, 2);
                            if (ret > 0) {
                                if (ret < x)  {
                                    _head += ret;
                                } else {
                                    _head = (ret - x);
                                }
                            }
                        }
                    }

                    if (ret > 0) {
                        _full = _head == _tail;
                    }
                    return ret;
                }

                CapacityType CurrentOffset() const {
                    return _head;
                }

					 bool Backup (CapacityType count) {
						if (count > Free()) return false;
						if (_full && _tail == _head) return false; // full or empty

						if (_head < _tail) {
						  if (count > _tail - _head) return false;
						  _tail -= count;
						  _full = _tail == _head;
						} else {
						  if (count > (_tail + (_capacity - _head))) return false;

						  
						  CapacityType temp = std::min(_tail, count);
						  _tail -= temp;
						  count -= temp;

						  if (count > 0) {
							 _tail = _capacity - count;
							 _full = _tail == _head;
						  }
						}
						
						return true;
					 }

					 
					 bool Direct (const void **out, CapacityType *len) {
						if (IsEmpty()) return false;

						if (_head > _tail) {
						  *len = _head - _tail;
						  *out = &_data[_tail];
						  _tail = _head;
						} else {
						  *len = _head - _tail;
						  *out = &_data[_tail];
						  _tail = 0;
						} 
						_full = false;
												
						return true;
					 }
					 

                 bool Skip (CapacityType size) {
                    if (size > Available()) return false;
                    if (size == 0) return false;

                    CapacityType remaining = size;
                    CapacityType offset = 0;

                    if (_tail > _head || _full) {
                        CapacityType x = std::min(size, (_capacity - _tail));

                        _tail += x;
                        if (_tail == _capacity) _tail = 0;

                        offset += x;
                        remaining -= x;
                    }

                    _full = false;
                    _tail += remaining;

                    return true;
                }
            private:
                BipBuf (const BipBuf &other);
                BipBuf &operator = (const BipBuf &other);

                CapacityType _head;
                CapacityType _tail;

                DataType * _data;
                CapacityType _capacity;
                bool _full;

        };    
    }
}
#endif
