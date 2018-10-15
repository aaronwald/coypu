#ifndef __COYPU_EVENT_HLPR_H
#define __COYPU_EVENT_HLPR_H

#include <unordered_map>
#include <functional>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/signalfd.h>

namespace  coypu
{
    namespace event 
    {
        class EPollHelper {
            public:
                static int Create (int flags = 0);
                static int Add (int efd, int fd, struct epoll_event *event);
                static int Modify (int efd, int fd, struct epoll_event *event);
                static int Delete (int efd, int fd);
            private:
                EPollHelper() = delete;
        };

        class TimerFDHelper {
            public:
                static int CreateRealtimeNonBlock (int flags = 0);
                static int CreateMonotonicNonBlock (int flags = 0);
                static int SetRelativeRepeating (int fd, time_t sec, long nsec);

            private:
                TimerFDHelper() = delete;
        };

        class EventFDHelper {
            public:
                static int CreateNonBlockEventFD (int flags);
            private:
                EventFDHelper() = delete;
        };

        class SignalFDHelper {
            public:
                static int BlockAllSignals ();
                static int CreateNonBlockSignalFD (const sigset_t *mask, int flags = 0);
            private:
                SignalFDHelper() = delete;
        };
    } // event
} //  coypu


#endif