#include <memory>

#include "gtest/gtest.h"
#include "store/store.h"
#include "file/file.h"
#include "mem/mem.h"

using namespace coypu::store;
using namespace coypu::file;
using namespace coypu::mem;


TEST(StoreTest, Test1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(),  0, fd, false, false);

	EXPECT_EQ(store.Push("foo", 3), 0) << buf;

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, Test2) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

	for (int x = 0; x < 10000; ++x) {
		ASSERT_EQ(store.Push("foo", 3), 0) << "Push [" << x << "] [" << buf << "]";
	}

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(StoreTest, StreamBufTest1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	typedef LogWriteBuf<MMapShared> buf_type;
	std::shared_ptr<buf_type> sp = std::make_shared<buf_type>(MemManager::GetPageSize(), 0, fd, false, false);

	logstreambuf<buf_type> csb(sp);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(StoreTest, TestReadv1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_EQ(write(fds[1], indata, 6), 6);
    ASSERT_EQ(store.Readv(fds[0], rcb), 6);


    close(fds[0]);
    close(fds[1]);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}


TEST(StoreTest, TestReadv2) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogWriteBuf<MMapShared> store(MemManager::GetPageSize(), 0, fd, false, false);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);


	for (int x = 0; x < 10000; ++x) {
		char indata[20];
		int r = snprintf(indata, 20, "data-%d,", x);
		ASSERT_EQ(write(fds[1], indata, r), r);
		r = store.Readv(fds[0], rcb);
		ASSERT_TRUE(r > 0)  << "Readv [" << x << "] [" << buf << "][" << r << "]";
	}

    close(fds[0]);
    close(fds[1]);

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRW1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	EXPECT_EQ(rwBuf.Push("abcdef", 6), 0) << buf;
	EXPECT_EQ(rwBuf.Available(), 6);
	EXPECT_EQ(rwBuf.IsEmpty(), false);
	EXPECT_EQ(rwBuf.Free(), UINT64_MAX - 6);
	EXPECT_EQ(rwBuf.Capacity(), UINT64_MAX);

	char d = 0;
	EXPECT_EQ(rwBuf.Peak (5, d), true);
	EXPECT_EQ(d, 'f');
	EXPECT_EQ(rwBuf.Peak (6, d), false);

	uint64_t offset = 0;
	EXPECT_EQ(rwBuf.Find(0, 'a', offset), true);
	EXPECT_EQ(offset, 0);
	EXPECT_EQ(rwBuf.Find(0, 'd', offset), true);
	EXPECT_EQ(offset, 3);
	EXPECT_EQ(rwBuf.Find(1, 'a', offset), false);

	char dest[6] = {};
	EXPECT_EQ(rwBuf.Pop(0, dest, 6), true);
	ASSERT_EQ(dest[0], 'a');
	ASSERT_EQ(dest[1], 'b');
	ASSERT_EQ(dest[2], 'c');
	ASSERT_EQ(dest[3], 'd');
	ASSERT_EQ(dest[4], 'e');
	ASSERT_EQ(dest[5], 'f');

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRW2) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	int count = 1200;
	for (int i = 0; i < count; ++i) {
		char dest[6] = {};

		ASSERT_EQ(rwBuf.Push("abcdef", 6), 0) << buf;
		ASSERT_EQ(rwBuf.Available(), 6*(i+1)) << i;
		ASSERT_EQ(rwBuf.IsEmpty(), false);
		ASSERT_EQ(rwBuf.Free(), UINT64_MAX - (6*(i+1)));
		ASSERT_EQ(rwBuf.Capacity(), UINT64_MAX);

		ASSERT_EQ(rwBuf.Pop(6*i, dest, 6), true) << "Iteration:" << i;
		ASSERT_EQ(dest[0], 'a') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[1], 'b') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[2], 'c') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[3], 'd') << "Iteration:" << i << ":" << buf; 
		ASSERT_EQ(dest[4], 'e') << "Iteration:" << i << ":" << buf;
		ASSERT_EQ(dest[5], 'f') << "Iteration:" << i << ":" << buf;
	}
	

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRW3) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	int count = 1200;
	char outstr[128];
	char dest[128];

	uint64_t offset = 0;
	for (int i = 0; i < count; ++i) {
		snprintf(outstr, 128, "count:%d", i);

		ASSERT_EQ(rwBuf.Push(outstr, strlen(outstr)), 0) << buf;

		ASSERT_EQ(rwBuf.Pop(offset, dest, strlen(outstr)), true) << "Iteration:" << i;
		offset += strlen(outstr);

		ASSERT_EQ(strncmp(outstr, dest, strlen(outstr)), 0) << "Iteration:" << i;
	}
	

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));
}

TEST(StoreTest, TestRWv1) 
{
	char buf[1024];
	int fd = FileUtil::MakeTemp("coypu", buf, sizeof(buf));
	ASSERT_TRUE(fd > 0);

	int fds[2];
	ASSERT_EQ(pipe(fds), 0);
	 
	std::function<int(int, const struct iovec *,int)> wcb = [] (int fd, const struct iovec *io, int count) {
	  return ::writev(fd, io, count);
	};
	
	LogRWStream<MMapShared, LRUCache, 16> rwBuf(MemManager::GetPageSize(), 0, fd, false);
	int count = 1200;
	char outstr[128];
	char dest[128];

	uint64_t offset = 0;
	for (int i = 0; i < count; ++i) {
		snprintf(outstr, 128, "count:%d", i);

		ssize_t to_write = strlen(outstr);

		ASSERT_EQ(rwBuf.Push(outstr, to_write), 0) << buf;

		ssize_t written = 0;
		while (to_write > 0) {
			int r = rwBuf.Writev(offset+written, to_write, fds[1], wcb);
			ASSERT_GT(r, 0) << "Iteration:" << i;
			written += r; 
			to_write -= written;
		}

		to_write = strlen(outstr);
		ssize_t r = read(fds[0], dest, to_write); // blocking read
		ASSERT_EQ(r, to_write) << "Iteration:" << i << ":" << buf;
		dest[to_write] = 0;

		offset += to_write;

		ASSERT_EQ(strncmp(outstr, dest, to_write), 0) << "Iteration:" << outstr << ":" << dest;
	}

	auto b = rwBuf.begin(0);
	auto e = rwBuf.end(100);
	int i = 0;
	for (;b != e; ++b) {
		++i;
	}
	ASSERT_EQ(i, 100); // 0..99 inclusive

	ASSERT_NO_THROW(FileUtil::Close(fd));
	ASSERT_NO_THROW(FileUtil::Remove(buf));

	FileUtil::Close(fds[0]);
	FileUtil::Close(fds[1]);
}

TEST (StoreTest, OneShotTest1) {
  typedef OneShotCache <MMapAnon, 32> cache_type;
  cache_type oneShot(MemManager::GetPageSize(), 0, -1);

  cache_type::read_cache_type page;
  
  ASSERT_EQ(oneShot.FindPage(0, page), -1);

  char *a,*b,*c,*d;
  off64_t zero = 0;
  off64_t one = MemManager::GetPageSize();
  off64_t two = MemManager::GetPageSize() * 2;
  off64_t three = MemManager::GetPageSize() * 3;

  a = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(a, nullptr);

  b = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(b, nullptr);

  c = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(c, nullptr);

  d = reinterpret_cast<char*>(MMapAnon::MMapWrite(-1, 0, MemManager::GetPageSize()));
  ASSERT_NE(d, nullptr);

  oneShot.AddPage(a, zero);
  oneShot.AddPage(b, one);
  oneShot.AddPage(c, two);
  oneShot.AddPage(d, three);


  ASSERT_EQ(oneShot.FindPage(zero, page), 0);
  ASSERT_EQ(oneShot.FindPage(zero, page), 0);
  ASSERT_EQ(oneShot.FindPage(two, page), 0);
  ASSERT_EQ(oneShot.FindPage(one, page), -1);
  ASSERT_EQ(oneShot.FindPage(two, page), 0);
  ASSERT_EQ(oneShot.FindPage(three, page), 0);
  ASSERT_EQ(oneShot.FindPage(two, page), -1);
}

TEST (StoreTest, OneShotTest2)
{
  LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);
  EXPECT_EQ(rwBuf.Push("abcdef", 6), 0);
  EXPECT_EQ(rwBuf.Available(), 6);
  EXPECT_EQ(rwBuf.IsEmpty(), false);
  EXPECT_EQ(rwBuf.Free(), UINT64_MAX - 6);
  EXPECT_EQ(rwBuf.Capacity(), UINT64_MAX);
  
  char d = 0;
  EXPECT_EQ(rwBuf.Peak (5, d), true);
  EXPECT_EQ(d, 'f');
  EXPECT_EQ(rwBuf.Peak (6, d), false);

  uint64_t offset = 0;
  EXPECT_EQ(rwBuf.Find(0, 'a', offset), true);
  EXPECT_EQ(offset, 0);
  EXPECT_EQ(rwBuf.Find(0, 'd', offset), true);
  EXPECT_EQ(offset, 3);
  EXPECT_EQ(rwBuf.Find(1, 'a', offset), false);
  
  char dest[6] = {};
  EXPECT_EQ(rwBuf.Pop(0, dest, 6), true);
  ASSERT_EQ(dest[0], 'a');
  ASSERT_EQ(dest[1], 'b');
  ASSERT_EQ(dest[2], 'c');
  ASSERT_EQ(dest[3], 'd');
  ASSERT_EQ(dest[4], 'e');
  ASSERT_EQ(dest[5], 'f');
}

// TODO test restart 


TEST(StoreTest, MaskTest1)
{
	LogRWStream<MMapAnon, OneShotCache, 16> rwBuf(MemManager::GetPageSize(), 0, -1, true);

	for (int i = 0; i < 2*MemManager::GetPageSize(); ++i) {
	  rwBuf.Push("a", 1);
	}

	char mask [] = {'b'};
	ASSERT_TRUE(rwBuf.Unmask(MemManager::GetPageSize()/2, MemManager::GetPageSize(), mask, 1));

	int i = 0;
	for (; i < MemManager::GetPageSize()/2; ++i) {
	  char a = 0;
	  ASSERT_TRUE(rwBuf.Pop(i, &a, 1)) << i;
	  ASSERT_EQ(a, 'a') << "I:" << i;
	}

	for (; i < MemManager::GetPageSize()/2 + MemManager::GetPageSize(); ++i) {
	  char a = 0;
	  ASSERT_TRUE(rwBuf.Pop(i, &a, 1)) << i;
	  ASSERT_EQ(a, 'a' ^ mask[0]) << i;
	}
		
	for (; i < 2*MemManager::GetPageSize(); ++i) {
	  char a = 0;
	  ASSERT_TRUE(rwBuf.Pop(i, &a, 1)) << i;
	  ASSERT_EQ(a, 'a') << i;
	}
}
