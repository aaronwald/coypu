
#include "gtest/gtest.h"
#include "file/file.h" 

#include <string>

using namespace coypu::file;

TEST(FileTest, Test1) 
{
  char buf[1024];
  int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
  ASSERT_TRUE(fd > 0);
  ASSERT_NO_THROW(FileUtil::Remove(buf));
  ASSERT_NO_THROW(FileUtil::Close(fd));
}
