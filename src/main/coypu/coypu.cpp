
#include <memory>
#include "coypu.h"
#include "spdlogger.h"
#include "event/event_mgr.h"

using namespace coypu;
using namespace coypu::event;

CoypuApplication & CoypuApplication::instance () {
  static CoypuApplication c;
  return c;
}

CoypuApplication::CoypuApplication () {

}

CoypuApplication::~CoypuApplication () {
}
