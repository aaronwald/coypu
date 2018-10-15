
#ifndef _COYPU_APP_H
#define _COYPU_APP_H

#include <string.h>

namespace coypu {
  class CoypuApplication {
  public:
    static CoypuApplication & instance ();

    void foo ();

  private:
    CoypuApplication ();
    virtual ~CoypuApplication ();
    CoypuApplication (const CoypuApplication &other);
    CoypuApplication &operator = (const CoypuApplication &other);
  };
}

#endif
