/*
 * Created on Mon Dec 21 2018
 * 
 * Copyright (c) 2018 Aaron Wald
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <unistd.h>
#include "gtest/gtest.h"
#include "store/store.h"
#include "file/file.h"
#include "mem/mem.h"
#include "proto/coincache.pb.h"
#include "protobuf/streams.h"
#include "protobuf/protomgr.h"
#include "buf/buf.h"

#include <string>
#include <sstream>
#include <iostream>

using namespace coypu::store;
using namespace coypu::file;
using namespace coypu::mem;
using namespace coypu::msg;
using namespace coypu::protobuf;
using namespace coypu::buf;

class outbuf : public std::streambuf {
protected:
  virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override
  {
	 //	 std::cout << n << std::endl;
  //	 return callback_(s, n, user_data_); // returns the number of characters successfully written.
		return n;
    };
  
  virtual int_type overflow (int_type c) override {
	 return c;
  }
};

TEST(ProtoTest, Test1) 
{
  coypu::msg::CoinCache gCC;
  gCC.set_high24(100.00);
  gCC.set_low24(90.000);

  std::stringstream buf;
  ASSERT_TRUE(gCC.SerializeToOstream(&buf));

  outbuf ob;
  std::ostream os(&ob);
  ASSERT_TRUE(gCC.SerializeToOstream(&os));
  os << "foo";
}

TEST(ProtoTest, Test2)
{
  char buf[1024];
  int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
  ASSERT_TRUE(fd > 0);

  typedef LogRWStream<MMapShared, LRUCache, 16> stream_type;
  stream_type rwBuf(MemManager::GetPageSize(), 0, fd, false);
  
  coypu::msg::CoinCache gCC;
  gCC.set_origseqno(2);
  gCC.set_seconds(3);
  gCC.set_milliseconds(4);
  gCC.set_high24(100.00);
  gCC.set_low24(90.000);
  gCC.set_vol24(120.00);
  gCC.set_open(99.00);
  gCC.set_last(101.00);

  //ASSERT_TRUE(gCC.SerializeToZeroCopyStream(&zOutput));

  char key[32] = {};
  LogZeroCopyOutputStream<stream_type> zOutput(&rwBuf);
  google::protobuf::io::CodedOutputStream coded_output(&zOutput);
  int msgCount = 1024;
  for (int i = 0; i < msgCount; ++i) {
	 ::snprintf(key, 32, "foo_%d", i);
	 gCC.set_key(key);
	 gCC.set_seqno(i);
	 ASSERT_TRUE(gCC.IsInitialized());

	 const int size = gCC.ByteSize();
	 coded_output.WriteVarint32(size);
					 
	 ASSERT_TRUE(gCC.SerializeToCodedStream(&coded_output));
  }

  LogZeroCopyInputStream<stream_type> zInput(&rwBuf);
  google::protobuf::io::CodedInputStream coded_input(&zInput);
  for (int i = 0; i < msgCount; ++i) {
	 coypu::msg::CoinCache gCC2;
	 uint32_t size;
	 ASSERT_TRUE(coded_input.ReadVarint32(&size));

	 google::protobuf::io::CodedInputStream::Limit limit =
		coded_input.PushLimit(size);
	 
	 ASSERT_TRUE(gCC2.MergeFromCodedStream(&coded_input));
		 
	 ASSERT_TRUE(coded_input.ConsumedEntireMessage());
	 coded_input.PopLimit(limit);

	 ::snprintf(key, 32, "foo_%d", i);

	 ASSERT_EQ(key, gCC2.key());
	 ASSERT_EQ(i, gCC2.seqno());
	 ASSERT_EQ(gCC.origseqno(), gCC2.origseqno());
	 ASSERT_EQ(gCC.seconds(), gCC2.seconds());
	 ASSERT_EQ(gCC.milliseconds(), gCC2.milliseconds());
	 ASSERT_EQ(gCC.high24(), gCC2.high24());
	 ASSERT_EQ(gCC.low24(), gCC2.low24());
	 ASSERT_EQ(gCC.vol24(), gCC2.vol24());
	 ASSERT_EQ(gCC.open(), gCC2.open());
	 ASSERT_EQ(gCC.last(), gCC2.last());
  }
  
  ASSERT_NO_THROW(FileUtil::Close(fd));
  ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(ProtoTest, BufTest1) {
  coypu::msg::CoinCache gCC;
  gCC.set_origseqno(2);
  gCC.set_seconds(3);
  gCC.set_milliseconds(4);
  gCC.set_high24(100.00);
  gCC.set_low24(90.000);
  gCC.set_vol24(120.00);
  gCC.set_open(99.00);
  gCC.set_last(101.00);
  std::string s;
  ASSERT_TRUE(gCC.SerializeToString(&s));

  char data[512] = {};
  typedef BipBuf <char, int> buf_type;
  buf_type buf2(data, 512);
  ASSERT_TRUE(buf2.Push(s.c_str(), s.length()));
  
  BufZeroCopyInputStream<buf_type *> bis(&buf2);
  google::protobuf::io::CodedInputStream coded_input(&bis);  
  coypu::msg::CoinCache gCC2;
  ASSERT_TRUE(gCC2.MergeFromCodedStream(&coded_input));
  ASSERT_EQ(gCC.origseqno(), gCC2.origseqno());
  ASSERT_EQ(gCC.seconds(), gCC2.seconds());
  ASSERT_EQ(gCC.milliseconds(), gCC2.milliseconds());
  ASSERT_EQ(gCC.high24(), gCC2.high24());
  ASSERT_EQ(gCC.low24(), gCC2.low24());
  ASSERT_EQ(gCC.vol24(), gCC2.vol24());
  ASSERT_EQ(gCC.open(), gCC2.open());
  ASSERT_EQ(gCC.last(), gCC2.last());
}

TEST(ProtoTest, BufTest2) {
  coypu::msg::CoinCache gCC;
  gCC.set_origseqno(2);
  gCC.set_seconds(3);
  gCC.set_milliseconds(4);
  gCC.set_high24(100.00);
  gCC.set_low24(90.000);
  gCC.set_vol24(120.00);
  gCC.set_open(99.00);
  gCC.set_last(101.00);

  char data[512] = {};
  typedef BipBuf <char, int> buf_type;
  buf_type buf2(data, 512);
  BufZeroCopyOutputStream<buf_type *> bos(&buf2);
  google::protobuf::io::CodedOutputStream coded_output(&bos);
  ASSERT_TRUE(gCC.SerializeToCodedStream(&coded_output));

  BufZeroCopyInputStream<buf_type *> bis(&buf2);
  google::protobuf::io::CodedInputStream coded_input(&bis);  
  coypu::msg::CoinCache gCC2;
  ASSERT_TRUE(gCC2.MergeFromCodedStream(&coded_input));
  
  ASSERT_EQ(gCC.origseqno(), gCC2.origseqno());
  ASSERT_EQ(gCC.seconds(), gCC2.seconds());
  ASSERT_EQ(gCC.milliseconds(), gCC2.milliseconds());
  ASSERT_EQ(gCC.high24(), gCC2.high24());
  ASSERT_EQ(gCC.low24(), gCC2.low24());
  ASSERT_EQ(gCC.vol24(), gCC2.vol24());
  ASSERT_EQ(gCC.open(), gCC2.open());
  ASSERT_EQ(gCC.last(), gCC2.last());
}

TEST(ProtoTest, BufTest3) {
  char data[512] = {};
  typedef BipBuf <char, int> buf_type;
  buf_type buf2(data, 512);
  BufZeroCopyOutputStream<buf_type *> bos(&buf2);
  google::protobuf::io::CodedOutputStream coded_output(&bos);
  BufZeroCopyInputStream<buf_type *> bis(&buf2);
  google::protobuf::io::CodedInputStream coded_input(&bis);
  
  coypu::msg::CoinCache gCC, gCC2;

  char key[32] = {};
  int msgCount = 128;
  for (int i = 0; i < msgCount; ++i) {
	 ::snprintf(key, 32, "foo_%d", i);
	 gCC.set_key(key);
	 gCC.set_seqno(i);
	 ASSERT_TRUE(gCC.IsInitialized());
	 
	 gCC.set_origseqno(2);
	 gCC.set_seconds(3);
	 gCC.set_milliseconds(4);
	 gCC.set_high24(100.00);
	 gCC.set_low24(90.000);
	 gCC.set_vol24(120.00);
	 gCC.set_open(99.00);
	 gCC.set_last(101.00);
	
	 const int out_size = gCC.ByteSize();
	 coded_output.WriteVarint32(out_size);
	 ASSERT_TRUE(gCC.SerializeToCodedStream(&coded_output));

	 uint32_t size = 0;
	 ASSERT_TRUE(coded_input.ReadVarint32(&size));

	 google::protobuf::io::CodedInputStream::Limit limit =
		coded_input.PushLimit(size);
	 
	 ASSERT_TRUE(gCC2.MergeFromCodedStream(&coded_input));
		 
	 ASSERT_TRUE(coded_input.ConsumedEntireMessage());
	 coded_input.PopLimit(limit);
	 
	 ASSERT_EQ(gCC.origseqno(), gCC2.origseqno());
	 ASSERT_EQ(gCC.seconds(), gCC2.seconds());
	 ASSERT_EQ(gCC.milliseconds(), gCC2.milliseconds());
	 ASSERT_EQ(gCC.high24(), gCC2.high24());
	 ASSERT_EQ(gCC.low24(), gCC2.low24());
	 ASSERT_EQ(gCC.vol24(), gCC2.vol24());
	 ASSERT_EQ(gCC.open(), gCC2.open());
	 ASSERT_EQ(gCC.last(), gCC2.last());
  }
}



