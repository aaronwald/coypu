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
#include "proto/coincache.pb.h"

#include <string>
#include <sstream>
#include <iostream>

using namespace coypu::msg;

class outbuf : public std::streambuf {
protected:
  virtual std::streamsize xsputn(const char_type* s, std::streamsize n) override
  {
	 std::cout << n << std::endl;
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

