
#include "gtest/gtest.h"
#include "config/config.h"
#include "file/file.h"

#include <string>

using namespace coypu::file;
using namespace coypu::config;

TEST(ConfigTest, Test1)
{
    char buf[1024];
    int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
    ASSERT_TRUE(fd > 0);
    write(fd, "---\n\n", 5);
    write(fd, "foo\n", 4);
    write(fd, "?: x\n", 5);
    ASSERT_NO_THROW(FileUtil::Close(fd));

    // Parse
    std::shared_ptr<CoypuConfig> config = CoypuConfig::Parse(buf);
    ASSERT_TRUE(config == nullptr);

    ASSERT_NO_THROW(FileUtil::Remove(buf));
}
using namespace coypu::config;

TEST(ConfigTest, Test2)
{
    char buf[1024];
    int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
    ASSERT_TRUE(fd > 0);
    write(fd, "---\n\n", 5);
    write(fd, "y: x\n", 5);
    write(fd, "z:\n", 3);
    write(fd, " - x\n", 5);
    ASSERT_NO_THROW(FileUtil::Close(fd));

    // Parse
    std::shared_ptr<CoypuConfig> config = CoypuConfig::Parse(buf);
    ASSERT_TRUE(config != nullptr);

    ASSERT_NO_THROW(FileUtil::Remove(buf));
}
