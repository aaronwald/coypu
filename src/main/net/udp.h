#ifndef __COYPU_TCP_H
#define __COYPU_TCP_H

#include <sys/types.h>
#include <sys/socket.h>

namespace coypu {
  namespace udp {
	 class UDPHelper {
	 public:
		int CreateUDPNonBlock ();
  
		UDPHelper() = delete;
	 };
  }
}

#endif
