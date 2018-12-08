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
#include "book/level.h"

#include <string>
#include <sys/uio.h>
#include <vector>

using namespace coypu::book;

struct BookLevel {
  uint64_t px;
  uint64_t qty;
   BookLevel *next, *prev;

  BookLevel (uint64_t px, uint64_t qty) : px(px), qty(qty), next(nullptr), prev(nullptr) {
  }
  
  BookLevel () : px(0), qty(0), next(nullptr), prev(nullptr) {
  }
} __attribute__((packed, aligned(64))) ;


TEST(BookTest, Test1) 
{
  int outindex = -1;
  LevelAllocator<BookLevel, 4096> la;
  BookLevel *l1 = la.Allocate(1,1);
  ASSERT_NE(l1, nullptr);
  ASSERT_EQ(l1->px, 1);
  ASSERT_EQ(l1->qty, 1);
  BookLevel *l2 = la.Allocate(2,2);
  ASSERT_NE(l2, nullptr);
  ASSERT_EQ(l2->px, 2);
  ASSERT_EQ(l2->qty, 2);
  
  // should always work at bottom of vector? since cheaper to move down.
  CLevelBook<BookLevel> b ([] (const BookLevel *lhs, const BookLevel *rhs) -> bool { return lhs->px < rhs->px; });
  b.Insert(l1, outindex);
  ASSERT_EQ(outindex, 0);
  b.Insert(l2, outindex);
  ASSERT_EQ(outindex, 1);

  BookLevel out;
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 2);
  ASSERT_TRUE(b.GetLevel(1, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(2, out));
  
  ASSERT_NE(b.Erase(2, outindex), nullptr);
  ASSERT_EQ(outindex, 0);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(1, out));
  ASSERT_FALSE(b.GetLevel(2, out));
  // free go to free pool (just linked list on the book)

}


TEST(BookTest, Test2) 
{
  LevelAllocator<BookLevel, 4096> la;
  BookLevel *l1 = la.Allocate(1,1);
  BookLevel *l2 = la.Allocate(2,2);
  BookLevel *l3 = la.Allocate(3,3);

  // should always work at bottom of vector? since cheaper to move down.
  int outindex = -1;
  CLevelBook<BookLevel> b ([] (const BookLevel *lhs, const BookLevel *rhs) -> bool { return lhs->px < rhs->px; });
  b.Insert(l1, outindex);
  ASSERT_EQ(outindex, 0);
  b.Insert(l2, outindex);
  ASSERT_EQ(outindex, 1);
  b.Insert(l3, outindex);
  ASSERT_EQ(outindex, 2);

  BookLevel out;
  ASSERT_NE(b.Erase(2, outindex), nullptr);
  ASSERT_EQ(outindex, 1);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 3);
  ASSERT_TRUE(b.GetLevel(1, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(2, out));

  ASSERT_NE(b.Erase(1, outindex), nullptr);
  ASSERT_EQ(outindex, 1);
  ASSERT_EQ(b.Erase(1, outindex), nullptr);
  ASSERT_EQ(outindex, -1);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 3);
  ASSERT_FALSE(b.GetLevel(1, out));
  ASSERT_FALSE(b.GetLevel(2, out));

  // free go to free pool (just linked list on the book)

}

// Bid test - Highest at bottom of array
TEST(BookTest, UpdateBidTest1) 
{
  int outindex = -1;
  LevelAllocator<BookLevel, 4096> la;
  BookLevel *l1 = la.Allocate(1,1);
  ASSERT_NE(l1, nullptr);
  ASSERT_EQ(l1->px, 1);
  ASSERT_EQ(l1->qty, 1);
  BookLevel *l2 = la.Allocate(2,2);
  ASSERT_NE(l2, nullptr);
  ASSERT_EQ(l2->px, 2);
  ASSERT_EQ(l2->qty, 2);
  
  // should always work at bottom of vector? since cheaper to move down.
  CLevelFwdBook<BookLevel> b ([] (const BookLevel *lhs, const BookLevel *rhs) -> bool { return lhs->px < rhs->px; });
  b.Insert(l1, outindex);
  ASSERT_EQ(outindex, 0);
  b.Insert(l2, outindex);
  ASSERT_EQ(outindex, 1);

  b.Update(2, 10, outindex);
  ASSERT_EQ(outindex, 1);

  BookLevel out;
  ASSERT_TRUE(b.GetLevel(1, out));
  ASSERT_EQ(out.px, 2);
  ASSERT_EQ(out.qty, 10);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(2, out));
  
  ASSERT_NE(b.Erase(2, outindex), nullptr);
  ASSERT_EQ(outindex, 1);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(1, out));
  ASSERT_FALSE(b.GetLevel(2, out));
  // free go to free pool (just linked list on the book)

}

// Bid test - Highest at top of array
TEST(BookTest, UpdateAskTest1) 
{
  int outindex = -1;
  LevelAllocator<BookLevel, 4096> la;
  BookLevel *l1 = la.Allocate(1,1);
  ASSERT_NE(l1, nullptr);
  ASSERT_EQ(l1->px, 1);
  ASSERT_EQ(l1->qty, 1);
  BookLevel *l2 = la.Allocate(2,2);
  ASSERT_NE(l2, nullptr);
  ASSERT_EQ(l2->px, 2);
  ASSERT_EQ(l2->qty, 2);
  
  // flip comparator so highest at top and lowest at bottom (so copies are faster at end)
  CLevelFwdBook<BookLevel> b ([] (const BookLevel *lhs, const BookLevel *rhs) -> bool { return lhs->px > rhs->px; });
  b.Insert(l1, outindex);
  ASSERT_EQ(outindex, 0);
  b.Insert(l2, outindex);
  ASSERT_EQ(outindex, 0);

  b.Update(2, 10, outindex);
  ASSERT_EQ(outindex, 0);

  BookLevel out;
  ASSERT_TRUE(b.GetLevel(1, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_EQ(out.qty, 1);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 2);
  ASSERT_EQ(out.qty, 10);
  ASSERT_FALSE(b.GetLevel(2, out));
  
  ASSERT_NE(b.Erase(2, outindex), nullptr);
  ASSERT_EQ(outindex, 0);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(1, out));
  ASSERT_FALSE(b.GetLevel(2, out));
  // free go to free pool (just linked list on the book)

}
