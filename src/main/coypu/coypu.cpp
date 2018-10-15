
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

void CoypuApplication::foo () {
  // foo
  // typedef EventManager <std::shared_ptr<SPDLogger>> event_mgr_type;

}