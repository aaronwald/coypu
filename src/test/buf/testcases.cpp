/*
 * Created on Mon Sep 24 2018
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
#include "buf/buf.h" 

#include <string>
#include <sys/uio.h>

using namespace coypu::buf;

TEST(BufTest, Test1) 
{
    char data[32];
    BipBuf <char, uint32_t> buf(data, 32);

    ASSERT_EQ(buf.Capacity(), 32);
    ASSERT_EQ(buf.Available(), 0);
    ASSERT_EQ(buf.Head(), 0);
    ASSERT_EQ(buf.Tail(), 0);

    ASSERT_FALSE(buf.Push(nullptr, 33));

    char bad[33];
    ASSERT_FALSE(buf.Push(bad, 33));

    char good[32];
    ASSERT_TRUE(buf.Push(good, 32));

    char x;
    ASSERT_FALSE(buf.Push(&x, 1));
    ASSERT_EQ(buf.Head(), 0);
    ASSERT_EQ(buf.Tail(), 0);
}


TEST(BufTest, Test2) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    ASSERT_EQ(buf.Capacity(), 8);
    ASSERT_EQ(buf.Available(), 0);
    ASSERT_EQ(buf.Head(), 0);
    ASSERT_EQ(buf.Tail(), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_TRUE(buf.Push(indata, 6));

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_TRUE(buf.Pop(outdata, 2));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');

    ASSERT_TRUE(buf.Push(indata, 4));

    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 8);
    uint32_t offset = 0;
    ASSERT_TRUE(buf.Find('a', offset));

    ASSERT_EQ(buf.Available(), 8);
    ASSERT_TRUE(buf.Pop(outdata, 8, true)); // peak 
    ASSERT_EQ(buf.Available(), 8);
    ASSERT_EQ(outdata[0], 'c');
    ASSERT_EQ(outdata[1], 'd');
    ASSERT_EQ(outdata[2], 'e');
    ASSERT_EQ(outdata[3], 'f');
    ASSERT_EQ(outdata[4], 'a');
    ASSERT_EQ(outdata[5], 'b');
    ASSERT_EQ(outdata[6], 'c');
    ASSERT_EQ(outdata[7], 'd');

    ::memset(outdata, 0, 8);
    ASSERT_TRUE(buf.Pop(outdata, 8));
    ASSERT_EQ(outdata[0], 'c');
    ASSERT_EQ(outdata[1], 'd');
    ASSERT_EQ(outdata[2], 'e');
    ASSERT_EQ(outdata[3], 'f');
    ASSERT_EQ(outdata[4], 'a');
    ASSERT_EQ(outdata[5], 'b');
    ASSERT_EQ(outdata[6], 'c');
    ASSERT_EQ(outdata[7], 'd');

    ASSERT_FALSE(buf.Find('a', offset));

    ASSERT_FALSE(buf.Pop(indata, 4));
}

TEST(BufTest, Test3) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    ASSERT_EQ(buf.Capacity(), 8);
    ASSERT_EQ(buf.Available(), 0);
    ASSERT_EQ(buf.Head(), 0);
    ASSERT_EQ(buf.Tail(), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_TRUE(buf.Push(indata, 6));

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_TRUE(buf.Pop(outdata, 2));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');

    indata[3] = 'R';
    ASSERT_TRUE(buf.Push(indata, 4));

    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 8);
    uint32_t offset = 0;
    ASSERT_TRUE(buf.Find('R', offset));
}


TEST(BufTest, Test4) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    ASSERT_EQ(buf.Capacity(), 8);
    ASSERT_EQ(buf.Available(), 0);
    ASSERT_EQ(buf.Head(), 0);
    ASSERT_EQ(buf.Tail(), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(buf.Push(indata[i]));
    }

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_TRUE(buf.Pop(outdata, 2));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');

    for (int i = 0; i < 4; ++i) {
        ASSERT_TRUE(buf.Push(indata[i]));
    }

    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 8);
    uint32_t offset = 0;
    ASSERT_TRUE(buf.Find('a', offset));

    ASSERT_TRUE(buf.Pop(outdata, 8));
    ASSERT_EQ(outdata[0], 'c');
    ASSERT_EQ(outdata[1], 'd');
    ASSERT_EQ(outdata[2], 'e');
    ASSERT_EQ(outdata[3], 'f');
    ASSERT_EQ(outdata[4], 'a');
    ASSERT_EQ(outdata[5], 'b');
    ASSERT_EQ(outdata[6], 'c');
    ASSERT_EQ(outdata[7], 'd');

    ASSERT_FALSE(buf.Find('a', offset));

    ASSERT_FALSE(buf.Pop(indata, 4));
}


TEST(BufTest, TestReadv1) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char peak;
    ASSERT_FALSE(buf.Peak(0, peak));

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_EQ(write(fds[1], indata, 6), 6);

    ASSERT_EQ(buf.Readv(fds[0], rcb), 6);

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_TRUE(buf.Peak(0, peak));
    ASSERT_EQ(peak, 'a');

    ASSERT_TRUE(buf.Pop(outdata, 2));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');

    peak = 0;
    ASSERT_TRUE(buf.Peak(0, peak));
    ASSERT_EQ(peak, 'c');

    ASSERT_EQ(write(fds[1], indata, 4), 4);
    ASSERT_EQ(buf.Readv(fds[0], rcb), 4);

    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 8);

    peak = 0;
    ASSERT_TRUE(buf.Peak(5, peak));
    ASSERT_EQ(peak, 'b');

    peak = 0;
    ASSERT_TRUE(buf.Peak(6, peak));
    ASSERT_EQ(peak, 'c');

    peak = 0;
    ASSERT_TRUE(buf.Peak(7, peak));
    ASSERT_EQ(peak, 'd');

    ASSERT_TRUE(buf.Pop(outdata, 8));
    ASSERT_EQ(outdata[0], 'c');
    ASSERT_EQ(outdata[1], 'd');
    ASSERT_EQ(outdata[2], 'e');
    ASSERT_EQ(outdata[3], 'f');
    ASSERT_EQ(outdata[4], 'a');
    ASSERT_EQ(outdata[5], 'b');
    ASSERT_EQ(outdata[6], 'c');
    ASSERT_EQ(outdata[7], 'd');


    ASSERT_FALSE(buf.Pop(indata, 4));
    close(fds[0]);
    close(fds[1]);
}


TEST(BufTest, TestReadv2) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_EQ(write(fds[1], indata, 6), 6);
    ASSERT_EQ(buf.Readv(fds[0], rcb), 6);

    ASSERT_EQ(write(fds[1], indata, 2), 2);
    ASSERT_EQ(buf.Readv(fds[0], rcb), 2);

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 8);
    ASSERT_TRUE(buf.Pop(outdata, 8));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');
    ASSERT_EQ(outdata[6], 'a');
    ASSERT_EQ(outdata[7], 'b');


    close(fds[0]);
    close(fds[1]);
}

TEST(BufTest, TestReadv3) 
{
    char data[8];
    BipBuf <char, uint64_t> buf(data, 8);

    std::function<int(int, const struct iovec *,int)> rcb = [] (int fd, const struct iovec *io, int count) {
        return ::readv(fd, io, count);
    };
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_EQ(write(fds[1], indata, 6), 6);
    ASSERT_EQ(buf.Readv(fds[0], rcb), 6);

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 6);
    ASSERT_TRUE(buf.Pop(outdata, 2));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');

    ASSERT_EQ(write(fds[1], indata, 2), 2);
    ASSERT_EQ(buf.Readv(fds[0], rcb), 2);

    ASSERT_EQ(write(fds[1], indata, 2), 2);
    ASSERT_EQ(buf.Readv(fds[0], rcb), 2);


    close(fds[0]);
    close(fds[1]);
}

TEST(BufTest, TestWritev1) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    std::function<int(int, const struct iovec *,int)> wcb = [] (int fd, const struct iovec *io, int count) {
        return ::writev(fd, io, count);
    };

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_TRUE(buf.Push(indata, 6));

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Writev(fds[1], wcb), 6);

    ASSERT_EQ(::read(fds[0], outdata, 6), 6);
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');

    close(fds[0]);
    close(fds[1]);
}

TEST(BufTest, TestWritev2) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    std::function<int(int, const struct iovec *,int)> wcb = [] (int fd, const struct iovec *io, int count) {
        return ::writev(fd, io, count);
    };

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char outdata[8];
    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};

    ASSERT_TRUE(buf.Push(indata, 6));
    ASSERT_EQ(buf.Writev(fds[1], wcb), 6);

    ::memset(outdata, 0, 8);
    ASSERT_EQ(::read(fds[0], outdata, 6), 6);
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');

    ASSERT_TRUE(buf.Push(indata, 6));
    ASSERT_EQ(buf.Writev(fds[1], wcb), 6);

    ::memset(outdata, 0, 8);
    ASSERT_EQ(::read(fds[0], outdata, 6), 6);
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');

    close(fds[0]);
    close(fds[1]);
}


TEST(BufTest, TestCB1) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);


    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_TRUE(buf.Push(indata, 6));

    char outdata[8];
    ::memset(outdata, 0, 8);

    std::function <int(int, void *, size_t)> cb = [] (int fd, void *buf, size_t len) {
        return ::write(fd, buf, len);
    };

    ASSERT_EQ(buf.Write(fds[1], cb), 6);

    ASSERT_EQ(::read(fds[0], outdata, 6), 6);
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');

    close(fds[0]);
    close(fds[1]);
}

TEST(BufTest, TestCB2) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_TRUE(buf.Push(indata, 6));

    char outdata[8];
    ::memset(outdata, 0, 8);

    std::function <int(int, void *, size_t)> cb = [] (int fd, void *buf, size_t len) {
        return ::write(fd, buf, len);
    };

    ASSERT_EQ(buf.Write(fds[1], cb), 6);

    ASSERT_EQ(::read(fds[0], outdata, 6), 6);
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');

    ASSERT_EQ(buf.Available(), 0);
    ASSERT_TRUE(buf.Push(indata, 6));
    // should have 2 and 4 around boundary
    ASSERT_EQ(buf.Write(fds[1], cb), 2);
    ASSERT_EQ(buf.Write(fds[1], cb), 4);
    ASSERT_EQ(buf.Available(), 0);

    ::memset(outdata, 0, 8);
    ASSERT_EQ(::read(fds[0], outdata, 6), 6);
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');
    ASSERT_EQ(outdata[2], 'c');
    ASSERT_EQ(outdata[3], 'd');
    ASSERT_EQ(outdata[4], 'e');
    ASSERT_EQ(outdata[5], 'f');


    close(fds[0]);
    close(fds[1]);
}

TEST(BufTest, PopAll) 
{
    char data[8];
    BipBuf <char, uint32_t> buf(data, 8);
    BipBuf <char, uint32_t> buf2(data, 8);

    ASSERT_EQ(buf.Capacity(), 8);
    ASSERT_EQ(buf.Available(), 0);
    ASSERT_EQ(buf.Head(), 0);
    ASSERT_EQ(buf.Tail(), 0);

    char indata[6] = {'a', 'b', 'c', 'd', 'e', 'f'};
    ASSERT_TRUE(buf.Push(indata, 6));

    char outdata[8];
    ::memset(outdata, 0, 8);
    ASSERT_TRUE(buf.Pop(outdata, 2, true));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');


    ASSERT_TRUE(buf.Pop(outdata, 2));
    ASSERT_EQ(outdata[0], 'a');
    ASSERT_EQ(outdata[1], 'b');

    ASSERT_TRUE(buf.Push(indata, 4));

    ::memset(outdata, 0, 8);
    ASSERT_EQ(buf.Available(), 8);

    std::function<bool(const char *, uint32_t)> cb = [&buf2] (const char *data, uint32_t len) {
        return buf2.Push(data, len);
    };
    ASSERT_TRUE(buf.PopAll(cb, 8));
    ASSERT_EQ(buf.Available(), 0);
    ASSERT_EQ(buf2.Available(), 8);

    ASSERT_TRUE(buf2.Pop(outdata, 8));
    ASSERT_EQ(outdata[0], 'c');
    ASSERT_EQ(outdata[1], 'd');
    ASSERT_EQ(outdata[2], 'e');
    ASSERT_EQ(outdata[3], 'f');
    ASSERT_EQ(outdata[4], 'a');
    ASSERT_EQ(outdata[5], 'b');
    ASSERT_EQ(outdata[6], 'c');
    ASSERT_EQ(outdata[7], 'd');
    ASSERT_FALSE(buf.PopAll(cb, 8));

}