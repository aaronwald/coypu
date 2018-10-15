
#include "gtest/gtest.h"
#include "pipeline/pipeline.h" 

#include <string>

using namespace coypu::pipeline;

struct Event {
  int _i; 

  Event (int i) : _i(i) {}
};


template <typename T> struct A;

template <>
struct A <Event> {
    Event &operator () (Event &i) {
      ++i._i;
      return i;
    }
};

template <typename T> struct B;

template <>
struct B <Event> {
    Event &operator () (Event &i) {
      i._i = 0;
      return i;
    }
};

TEST(PipelineTest, Test1) 
{
    Event e(1);
    PipelineHandler <Event, A<Event>, B<Event> > ph;

    ph(e);
    ASSERT_EQ(e._i, 0);
}

TEST(PipelineTest, Test2) 
{
    Event e(1);
    PipelineHandler <Event, B<Event>, A<Event> > ph;

    ph(e);
    ASSERT_EQ(e._i, 1);
}
