#include <memory>

#include "gtest/gtest.h"
#include "cache/tagcache.h"
#include "file/file.h"
#include "event/event_mgr.h"

using namespace coypu::cache;
using namespace coypu::file;
using namespace coypu::event;

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
	off64_t off = 0;
	ASSERT_TRUE(tagCache2.Restore(off));

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

TEST(CacheTest, TagTest4) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_NO_THROW(FileUtil::Close(fd));

	TagStore tagCache(buf);
	ASSERT_TRUE(tagCache.IsOpen());

	TagStore::tag_id_type outId;
	off64_t off = 0;
	ASSERT_TRUE(tagCache.Restore(off));
	ASSERT_TRUE(tagCache.Append("taga", outId));
	ASSERT_EQ(outId, 0);
	ASSERT_TRUE(tagCache.Append("tagb", outId));
	ASSERT_EQ(outId, 1);
	ASSERT_EQ(tagCache.GetTagCount(), 2);

	TagStore tagCache2(buf);
	ASSERT_TRUE(tagCache2.IsOpen());
	ASSERT_TRUE(tagCache2.Restore(off));
	ASSERT_EQ(tagCache2.GetTagCount(), 2);
	ASSERT_TRUE(tagCache2.Append("tagc", outId));
	ASSERT_EQ(outId, 2);
	ASSERT_EQ(tagCache2.GetTagCount(), 3);

	TagStore tagCache3(buf);
	ASSERT_TRUE(tagCache3.IsOpen());
	ASSERT_TRUE(tagCache3.Restore(off));
	ASSERT_EQ(tagCache3.GetTagCount(), 3);
	
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

struct DummyLog {
  void perror (int, const char *) { }
  template <typename... Args> const void warn(const char *msg, Args... args) { }
};

TEST(CacheTest, TagEventTest1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_NO_THROW(FileUtil::Close(fd));

	EventManager <DummyLog *> eventMgr(nullptr);
	eventMgr.Init();

	int eventfd = EventFDHelper::CreateNonBlockEventFD(0);
	int clientfd = EventFDHelper::CreateNonBlockEventFD(0);
			
	std::function<int(int)> set_write = std::bind(&EventManager<DummyLog *>::SetWrite, std::ref(eventMgr), std::placeholders::_1);
	TagStream<Tag> tagStream(eventfd, set_write, buf);

	bool done = false;
	uint32_t tagId = 89123;
	uint32_t streamId = 9812;

	std::function <int(int)> close = std::bind(&TagStream<Tag>::Close, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> read = std::bind(&TagStream<Tag>::Read, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> write = std::bind(&TagStream<Tag>::Write, std::ref(tagStream), std::placeholders::_1);

	std::function<int(int, uint64_t, uint64_t)> streamCB = [&done] (int fd, uint64_t off, uint64_t len) {
	  std::cout << "Stream out" << std::endl;
	  done = true;
	  return 0;
	};

	tagStream.Register(clientfd);
	tagStream.RegisterStream(clientfd, streamId, streamCB);
	tagStream.Subscribe(clientfd, tagId);
	
	ASSERT_EQ(eventMgr.Register(eventfd, read, write, close), 0);
	
	Tag tag (0, 8, tagId, -1, 0);
	tagStream.Queue(tag);

	while (!done) {
	  eventMgr.Wait();
	}
	eventMgr.Close();

	ASSERT_NO_THROW(FileUtil::Remove(buf));
	::close(eventfd);
	::close(clientfd);
}
