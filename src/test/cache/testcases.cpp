#include <memory>

#include "gtest/gtest.h"
#include "cache/tagcache.h"
#include "file/file.h"

using namespace coypu::cache;
using namespace coypu::file;

TEST(CacheTest, TagTest1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_NO_THROW(FileUtil::Close(fd));

	TagStore tagCache(buf);
	ASSERT_TRUE(tagCache.IsOpen());

	TagStore::tag_id_type outId;
	ASSERT_TRUE(tagCache.Append("taga", outId));
	ASSERT_EQ(outId, 0);
	ASSERT_TRUE(tagCache.Append("tagb", outId));
	ASSERT_EQ(outId, 1);
	ASSERT_EQ(tagCache.GetTagCount(), 2);
	
	TagStore tagCache2(buf);
	ASSERT_TRUE(tagCache2.IsOpen());
	ASSERT_TRUE(tagCache2.Restore());

	ASSERT_FALSE(tagCache2.FindTag("tagc", outId));
	ASSERT_TRUE(tagCache2.FindTag("tagb", outId));	
	ASSERT_EQ(outId, 1);

	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(CacheTest, TagTest2) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_NO_THROW(FileUtil::Close(fd));

	TagOffsetStore offsetStore(buf);
	TagOffsetStore::tag_type tag;
	ASSERT_TRUE(offsetStore.Append(tag));

	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(CacheTest, TagTest3)
{
  TagSub sub;
  ASSERT_FALSE(sub.IsSet(89));
  sub.Set(89);
  ASSERT_TRUE(sub.IsSet(89));
  sub.Clear(89);
  ASSERT_FALSE(sub.IsSet(89));
}
