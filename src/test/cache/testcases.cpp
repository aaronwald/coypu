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

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	int eventfd = EventFDHelper::CreateNonBlockEventFD(0);
			
	std::function<int(int)> set_write = std::bind(&EventManager<DummyLog *>::SetWrite, std::ref(eventMgr), std::placeholders::_1);
	TagStream<Tag> tagStream(eventfd, set_write, buf);

	uint16_t msgCount = 0;
	uint32_t tagId = 89123;
	uint32_t streamId = 9812;
	uint32_t streamId2 = 1232;
	
	std::function <int(int)> close = std::bind(&TagStream<Tag>::Close, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> read = std::bind(&TagStream<Tag>::Read, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> write = std::bind(&TagStream<Tag>::Write, std::ref(tagStream), std::placeholders::_1);

	std::function<int(int, uint64_t, uint64_t)> streamCB = [&msgCount] (int fd, uint64_t off, uint64_t len) {
	  //std::cout << "Stream out data " << off << "," << len << std::endl;
	  ++msgCount;
	  return len;
	};

	tagStream.Register(fds[1]);
	tagStream.RegisterStream(streamId, streamCB);
	tagStream.Subscribe(fds[1], tagId);
	ASSERT_EQ(tagStream.Start(fds[1], 0), 0);

	std::function <int(int)> clientWrite = std::bind(&TagStream<Tag>::StreamWrite, std::ref(tagStream), std::placeholders::_1);

	ASSERT_EQ(eventMgr.Register(eventfd, read, write, close), 0);
	ASSERT_EQ(eventMgr.Register(fds[1], nullptr, clientWrite, nullptr), 0);

	tagStream.Queue(Tag(0, 8, streamId, tagId, -1, 0));
	tagStream.Queue(Tag(0, 8, streamId2, tagId, -1, 0));
	tagStream.Queue(Tag(8, 16, streamId, tagId, -1, 0));

	while (msgCount < 2) {
	  eventMgr.Wait();
	}
	eventMgr.Close();
	ASSERT_EQ(msgCount, 2);

	ASSERT_NO_THROW(FileUtil::Remove(buf));
	::close(eventfd);
	::close(fds[0]);
	::close(fds[1]);
}

TEST(CacheTest, TagEventTest2) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_NO_THROW(FileUtil::Close(fd));

	EventManager <DummyLog *> eventMgr(nullptr);
	eventMgr.Init();

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	int eventfd = EventFDHelper::CreateNonBlockEventFD(0);
			
	std::function<int(int)> set_write = std::bind(&EventManager<DummyLog *>::SetWrite, std::ref(eventMgr), std::placeholders::_1);
	TagStream<Tag> tagStream(eventfd, set_write, buf);

	uint16_t msgCount = 0;
	uint32_t tagId = 89123;
	uint32_t streamId = 9812;
	uint32_t streamId2 = 1232;
	
	std::function <int(int)> close = std::bind(&TagStream<Tag>::Close, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> read = std::bind(&TagStream<Tag>::Read, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> write = std::bind(&TagStream<Tag>::Write, std::ref(tagStream), std::placeholders::_1);

	std::function<int(int, uint64_t, uint64_t)> streamCB = [&msgCount] (int fd, uint64_t off, uint64_t len) {
	  //std::cout << "Stream out data " << off << "," << len << std::endl;
	  if (off % 4 == 0)
		 ++msgCount;

	  return len > 4 ? 4 : len;
	};

	// setup stream
	ASSERT_EQ(tagStream.Register(fds[1]), 0);
	ASSERT_EQ(tagStream.Register(fds[1]), -1);
	ASSERT_EQ(tagStream.RegisterStream(streamId, streamCB), 0);
	ASSERT_EQ(tagStream.RegisterStream(streamId, streamCB), -1);
	ASSERT_EQ(tagStream.Start(fds[1], 0), 0);
	tagStream.Subscribe(fds[1], tagId);

	// event mgr
	std::function <int(int)> clientWrite = std::bind(&TagStream<Tag>::StreamWrite, std::ref(tagStream), std::placeholders::_1);
	ASSERT_EQ(eventMgr.Register(eventfd, read, write, close), 0);
	ASSERT_EQ(eventMgr.Register(fds[1], nullptr, clientWrite, nullptr), 0);

	// queue events
	tagStream.Queue(Tag(0, 8, streamId, tagId, -1, 0));
	tagStream.Queue(Tag(0, 8, streamId2, tagId, -1, 0));
	tagStream.Queue(Tag(8, 8, streamId, tagId, -1, 0));

	while (msgCount < 4) {
	  eventMgr.Wait();
	}
	eventMgr.Close();
	ASSERT_EQ(msgCount, 4);

	ASSERT_NO_THROW(FileUtil::Remove(buf));
	::close(eventfd);
	::close(fds[0]);
	::close(fds[1]);
}

TEST(CacheTest, TagEventTest3) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_NO_THROW(FileUtil::Close(fd));

	EventManager <DummyLog *> eventMgr(nullptr);
	eventMgr.Init();

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);

	int eventfd = EventFDHelper::CreateNonBlockEventFD(0);
			
	std::function<int(int)> set_write = std::bind(&EventManager<DummyLog *>::SetWrite, std::ref(eventMgr), std::placeholders::_1);
	TagStream<Tag> tagStream(eventfd, set_write, buf);

	uint16_t msgCount = 0;
	uint32_t tagId = 89123;
	uint32_t streamId = 9812;
	uint32_t streamId2 = 1232;
	
	std::function <int(int)> close = std::bind(&TagStream<Tag>::Close, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> read = std::bind(&TagStream<Tag>::Read, std::ref(tagStream), std::placeholders::_1);
	std::function <int(int)> write = std::bind(&TagStream<Tag>::Write, std::ref(tagStream), std::placeholders::_1);

	uint64_t minOffset = UINT64_MAX;
	std::function<int(int, uint64_t, uint64_t)> streamCB = [&msgCount, &minOffset] (int fd, uint64_t off, uint64_t len) {
	  minOffset = std::min(off, minOffset);
	  //	  std::cout << "Stream out data " << off << "," << len << std::endl;
	  ++msgCount;

	  return len > 4 ? 4 : len;
	};

	// setup stream
	ASSERT_EQ(tagStream.Register(fds[1]), 0);
	ASSERT_EQ(tagStream.Register(fds[1]), -1);
	ASSERT_EQ(tagStream.RegisterStream(streamId, streamCB), 0);
	ASSERT_EQ(tagStream.RegisterStream(streamId, streamCB), -1);
	ASSERT_EQ(tagStream.Start(fds[1], sizeof(Tag)), 0);
	tagStream.Subscribe(fds[1], tagId);

	// event mgr
	std::function <int(int)> clientWrite = std::bind(&TagStream<Tag>::StreamWrite, std::ref(tagStream), std::placeholders::_1);
	ASSERT_EQ(eventMgr.Register(eventfd, read, write, close), 0);
	ASSERT_EQ(eventMgr.Register(fds[1], nullptr, clientWrite, nullptr), 0);

	// queue events
	tagStream.Queue(Tag(0, 8, streamId, tagId, -1, 0));
	tagStream.Queue(Tag(0, 8, streamId2, tagId, -1, 0));
	tagStream.Queue(Tag(8, 8, streamId, tagId, -1, 0));

	while (msgCount < 2) {
	  eventMgr.Wait();
	}
	eventMgr.Close();
	ASSERT_EQ(msgCount, 2);
	ASSERT_EQ(minOffset, 8);

	ASSERT_NO_THROW(FileUtil::Remove(buf));
	::close(eventfd);
	::close(fds[0]);
	::close(fds[1]);
}
