#ifndef __COYPU_TCP_H
#define __COYPU_TCP_H
#include <netinet/in.h>
#include <sys/uio.h>

namespace coypu {
    namespace tcp {
        class TCPHelper {
            public:
                template <typename V> class MultiProvider {
                public:
                    static ssize_t Writev (int fd, const struct iovec *iov, int iovcnt) {
                        return ::writev(fd, iov, iovcnt);
                    }

                    static ssize_t Readv (int fd, const struct iovec *iov, int iovcnt) {
                        return ::readv(fd, iov, iovcnt);
                    }
                private:
                    MultiProvider() = delete;
                };

                static int CreateIPV4NonBlockSocket ();
                static int CreateIPV6NonBlockSocket ();
                static int SetNoDelay (int fd);
                static int SetReuseAddr (int fd);
                static int SetReusePort(int fd);

                static int ConnectStream (const char *host, int port);
                static int ConnectIPV4 (int sockFD, struct sockaddr_in *serv_addr);
                static int GetSockNameIPV4 (int sockFD, struct sockaddr_in *serv_addr);

                static int GetSendRecvSize (int fd, int &sendSize, int &recvSize);
					 static int GetTCPFastOpen (int fd, int &fastopen);
                static int SetSendSize (int fd, int sendSize);
                static int SetRecvSize (int fd, int recvSize);

                static int Listen (int sockfd, int listen);
                static int AcceptNonBlock (int sockfd, struct sockaddr *addr, socklen_t *addrlen);
                
                static int BindIPV4 (int sockFD, struct sockaddr_in *serv_addr);

                static int GetInterfaceIPV4FromName (const char *name, size_t len, struct sockaddr_in &out);
                static int GetInterfaceIPV6FromName (const char *name, size_t len, struct sockaddr_in6 &out);


                TCPHelper() = delete;
        };
    }
}

#endif
