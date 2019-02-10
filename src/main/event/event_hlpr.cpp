
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <string.h>
#include <iostream>

#include "event_hlpr.h"

using namespace coypu::event;

int EPollHelper::Create (int flags) {
    // size is deprecated
    return ::epoll_create1(flags);
}

int EPollHelper::Add (int efd, int fd, struct epoll_event *event) {
  //  std::cout << "Add " << fd << std::endl;
    return ::epoll_ctl(efd, EPOLL_CTL_ADD, fd, event);
}

int EPollHelper::Modify (int efd, int fd, struct epoll_event *event) {
    return ::epoll_ctl(efd, EPOLL_CTL_MOD, fd, event);
}

int EPollHelper::Delete (int efd, int fd) {
  //  std::cout << "Delete " << fd  << std::endl;
    return ::epoll_ctl(efd, EPOLL_CTL_DEL, fd, nullptr);
}

int EventFDHelper::CreateNonBlockEventFD (int flags) {
    return ::eventfd(0, flags | EFD_NONBLOCK);
}

int TimerFDHelper::CreateRealtimeNonBlock (int flags) {
    return ::timerfd_create(CLOCK_REALTIME, TFD_NONBLOCK | flags);
}

int TimerFDHelper::CreateMonotonicNonBlock (int flags) {
    return ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | flags);
}

int TimerFDHelper::SetRelativeRepeating (int fd, time_t sec, long nsec) {
    struct itimerspec ts;
    ::memset(&ts, 0, sizeof(struct itimerspec));
    ts.it_value.tv_sec = sec;
    ts.it_value.tv_nsec = nsec;
    ts.it_interval.tv_sec = sec;
    ts.it_interval.tv_nsec = nsec;
    return ::timerfd_settime(fd, 0, &ts, NULL);  
}

int SignalFDHelper::CreateNonBlockSignalFD (const sigset_t *mask, int flags) {
    return ::signalfd(-1, mask, flags | SFD_NONBLOCK);
}

int SignalFDHelper::BlockAllSignals () {
    sigset_t mask;
	::sigfillset(&mask);
	return ::sigprocmask(SIG_SETMASK, &mask, NULL); // set mask blocks these signals
}
