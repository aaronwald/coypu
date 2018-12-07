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

struct Level {
  uint64_t px;
  uint64_t qty;
   Level *next, *prev;

  Level (uint64_t px, uint64_t qty) : px(px), qty(qty), next(nullptr), prev(nullptr) {
  }
  
  Level () : px(0), qty(0), next(nullptr), prev(nullptr) {
  }

  bool operator()(const Level *lhs, const Level *rhs) const {
	 return lhs->px < rhs->px;
  }
} __attribute__((packed, aligned(64))) ;


TEST(BookTest, Test1) 
{
  LevelAllocator<Level, 4096> la;
  Level *l1 = la.Allocate(1,1);
  ASSERT_NE(l1, nullptr);
  ASSERT_EQ(l1->px, 1);
  ASSERT_EQ(l1->qty, 1);
  Level *l2 = la.Allocate(2,2);
  ASSERT_NE(l2, nullptr);
  ASSERT_EQ(l2->px, 2);
  ASSERT_EQ(l2->qty, 2);
  
  // should always work at bottom of vector? since cheaper to move down.
  CLevelBook<Level> b;
  b.Insert(l1);
  b.Insert(l2);

  Level out;
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 2);
  ASSERT_TRUE(b.GetLevel(1, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(2, out));
  
  ASSERT_NE(b.Erase(2), nullptr);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(1, out));
  ASSERT_FALSE(b.GetLevel(2, out));
  // free go to free pool (just linked list on the book)

}


TEST(BookTest, Test2) 
{
  LevelAllocator<Level, 4096> la;
  Level *l1 = la.Allocate(1,1);
  Level *l2 = la.Allocate(2,2);
  Level *l3 = la.Allocate(3,3);

  
  // should always work at bottom of vector? since cheaper to move down.
  CLevelBook<Level> b;
  b.Insert(l1);
  b.Insert(l2);
  b.Insert(l3);

  Level out;
  ASSERT_NE(b.Erase(2), nullptr);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 3);
  ASSERT_TRUE(b.GetLevel(1, out));
  ASSERT_EQ(out.px, 1);
  ASSERT_FALSE(b.GetLevel(2, out));

  ASSERT_NE(b.Erase(1), nullptr);
  ASSERT_TRUE(b.GetLevel(0, out));
  ASSERT_EQ(out.px, 3);
  ASSERT_FALSE(b.GetLevel(1, out));
  ASSERT_FALSE(b.GetLevel(2, out));

  // free go to free pool (just linked list on the book)

}
