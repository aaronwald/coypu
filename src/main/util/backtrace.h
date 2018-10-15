
#ifndef __COYPU_BACKTRACE_H
#define __COYPU_BACKTRACE_H

namespace coypu {
  namespace backtrace {
    class BackTrace {
    public: 
      BackTrace () = delete;
      ~BackTrace () = delete;
      BackTrace (const BackTrace &other) = delete;
      BackTrace &operator= (const BackTrace &other) = delete;
      BackTrace (BackTrace &&) = delete;
      BackTrace &operator= (BackTrace &&) = delete;

      static void bt (void);
    };
  }
}

#endif
