
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <string.h>
#include <stdio.h>

#include "tcp.h"

using namespace coypu::tcp;

int TCPHelper::ConnectIPV4 (int sockFD, struct sockaddr_in *serv_addr) {
    return ::connect(sockFD, (struct sockaddr *) serv_addr, sizeof(sockaddr_in));
}

int TCPHelper::ConnectStream (const char *host, int port) {
    int fd = -1;

    struct addrinfo *result, *rp;
    struct addrinfo hints;
    ::memset (&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    char service[16] = {};
    sprintf(service, "%d", port);

    int s = ::getaddrinfo(host, service, &hints, &result);
    if (s != 0) return s;
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd == -1) continue;

        if (connect(fd, rp->ai_addr, rp->ai_addrlen) != -1) break;

        close(fd);
        fd = -1;
    }
    freeaddrinfo(result);
    return fd;
}


int TCPHelper::GetSendRecvSize (int fd, int &sendSize, int &recvSize) {
    unsigned int optlen = sizeof(unsigned int);
    int res = ::getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendSize, &optlen);

    optlen = sizeof(int);
    res += ::getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvSize, &optlen);

    return res;
}

int TCPHelper::SetSendSize (int fd, int sendSize) {
    unsigned int optlen = sizeof(unsigned int);
    return ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendSize, optlen);
}

int TCPHelper::SetRecvSize (int fd, int recvSize) {
    unsigned int optlen = sizeof(unsigned int);
    return ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &recvSize, optlen);
}

int TCPHelper::SetNoDelay (int fd) {
    int one = 1;
    return ::setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one));
}

int TCPHelper::SetReuseAddr (int fd) {
    int one = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
}

int TCPHelper::SetReusePort (int fd) {
    int one = 1;
    return ::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
}

int TCPHelper::CreateIPV4NonBlockSocket() {
    return ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
}

int TCPHelper::CreateIPV6NonBlockSocket() {
    return ::socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
}

int TCPHelper::Listen (int sockfd, int backlog) {
    return ::listen(sockfd, backlog);
}

int TCPHelper::BindIPV4 (int sockFD, struct sockaddr_in *serv_addr) {
    return ::bind(sockFD, (struct sockaddr *) serv_addr, sizeof(sockaddr_in));
}

int TCPHelper::AcceptNonBlock (int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
    return ::accept4(sockfd, addr, addrlen, SOCK_NONBLOCK);
}

int TCPHelper::GetSockNameIPV4 (int sockFD, struct sockaddr_in *serv_addr) {
    socklen_t addrlen = sizeof(struct sockaddr_in);
    return ::getsockname(sockFD, reinterpret_cast<sockaddr *>(serv_addr), &addrlen);
}

// TODO template 
int TCPHelper::GetInterfaceIPV4FromName (const char *name, size_t len, struct sockaddr_in &out) {
    int ret = 1;

	struct ifaddrs *ifaddr, *ifa;
	int family;

	if (getifaddrs(&ifaddr) == -1) {
		return -1;
	}

	for (ifa = ifaddr; ifa != NULL && ret == 1; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr == NULL) {
			continue;
        }

		family = ifa->ifa_addr->sa_family;

		if (family == AF_INET) {
			// s = getnameinfo(ifa->ifa_addr,
			// 				(family == AF_INET) ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6),
			// 				host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
			if (strncmp(name, ifa->ifa_name, len) == 0) {
                ret = 0;
                out = *(reinterpret_cast<struct sockaddr_in *>(ifa->ifa_addr));
            }
		}
	}
	freeifaddrs(ifaddr);

    return ret;
}
