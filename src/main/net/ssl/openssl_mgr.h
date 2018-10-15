
#ifndef _COYPU_SSL_H
#define _COYPU_SSL_H

#include <stdio.h>
#include <assert.h>
#include <sys/uio.h>

#include <vector>
#include <memory>
#include <functional>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/evp.h>
#include <sys/uio.h>

namespace coypu {
    namespace net {
        namespace ssl {
            template <typename V> class SSLProvider {
                public:
                    static ssize_t Writev (int fd, const struct iovec *iov, int iovcnt) {
                        return ::writev(fd, iov, iovcnt);
                    }

                    static ssize_t Readv (int fd, const struct iovec *iov, int iovcnt) {
                        return ::readv(fd, iov, iovcnt);
                    }
                private:
                    SSLProvider() = delete;
                };

            // supports non block io
            class OpenSSLManager {
                public:
                    typedef struct SSLConnectionT {
                        int _fd;
                        SSL *_ssl;

                        SSLConnectionT (int fd, SSL *ssl) : _fd(fd),  _ssl(ssl) {
                        }
                    } SSLConnection;

                    OpenSSLManager (std::function <int (int)> set_write) : _set_write(set_write), _ctx(nullptr) {
                        _ctx = SSL_CTX_new(SSLv23_method());
                        if (!_ctx) {
                            // a->error("SSL init failed.");
                            assert(false);
                        }
                    }

                    virtual ~OpenSSLManager () {
                        if (_ctx) {
                            SSL_CTX_free(_ctx);
                            _ctx = nullptr;
                        }
                    }

                    static void Init() {
                        SSL_library_init(); // always returns 1
                        OpenSSL_add_all_algorithms();
                        SSL_load_error_strings();
                        ERR_load_crypto_strings();                        
                    }

                    static int Base64Encode (unsigned char *dest, unsigned char *src, size_t srclen) {
                        return ::EVP_EncodeBlock(dest, src, srclen);
                    }

                    int Register (int fd, bool setConnect = true) {
                        SSL *ssl = SSL_new(_ctx);
                        if (!ssl) {
                            // a->error("SSL new");
                            assert(false);
                        }

                        _fdToCon.resize(fd+1);
                        _fdToCon[fd] = std::make_shared<SSLConnection>(fd, ssl);

                        SSL_set_fd(ssl, fd);
                        if (setConnect) {
                    		SSL_set_connect_state(ssl);
                        }
                        
                        return 0;
                    }

                    int Unregister (int fd) {
                        if (fd >= _fdToCon.size()) return -1;
                        if (!_fdToCon[fd]) return -2;

                        if (_fdToCon[fd] && _fdToCon[fd]->_ssl) {
                            SSL_free(_fdToCon[fd]->_ssl);
                        }

                        _fdToCon[fd] = nullptr;

                        return 0;
                    }

                    int ReadNonBlock (int fd, void *buf, size_t len) {
                        if (fd >= _fdToCon.size()) return -1;
                        if (!_fdToCon[fd]) return -2;
                        std::shared_ptr<SSLConnection> &con = _fdToCon[fd];
                        if (!con) return -3;
                
                        int ret = SSL_read(con->_ssl, buf, len);
                        if (ret <= 0) {
                            ret = SSL_get_error(con->_ssl, ret);
                            if (ret == SSL_ERROR_WANT_READ) {
                                return 0;
                            } else if (ret == SSL_ERROR_WANT_WRITE) {
                                _set_write(con->_fd);
                                return 0;
                            } else {
                                unsigned long err = ERR_get_error();
                                printf("Some other error to handle %ld [%s]\n", err, ERR_error_string(err, nullptr));
                                printf("Some other error to handle %ld [%s]\n", err, ERR_reason_error_string(err));
                                return -4;
                            }
                        }
                        return ret;
                    }

                    int WriteNonBlock (int fd, const void *buf, size_t len) {
                        if (fd >= _fdToCon.size()) return -1;
                        if (!_fdToCon[fd]) return -2;
                        std::shared_ptr<SSLConnection> &con = _fdToCon[fd];
                        if (!con) return -3;

                        int ret = SSL_write(con->_ssl, buf, len);
                        
                        if (ret <= 0) {
                            ret = SSL_get_error(con->_ssl, ret);
                            if (ret == SSL_ERROR_WANT_READ) {
                                return 0;
                            } else if (ret == SSL_ERROR_WANT_WRITE) {
                                return 0;
                            } else {
                                unsigned long err = ERR_get_error();
                                printf("Some other error to handle %ld [%s]\n", err, ERR_error_string(err, nullptr));
                                printf("Some other error to handle %ld [%s]\n", err, ERR_reason_error_string(err));
                                return -4;
                            }
                        }
                        return ret;
                    }


                    // Only checks index 0
                    int ReadvNonBlock (int fd, const struct iovec *iovec, int count) {
                        if (count <= 0) return -5;
                        if (fd >= _fdToCon.size()) return -1;
                        if (!_fdToCon[fd]) return -2;
                        std::shared_ptr<SSLConnection> &con = _fdToCon[fd];
                        if (!con) return -3;
                
                        int xret = SSL_read(con->_ssl, iovec[0].iov_base, iovec[0].iov_len);

                        if (xret <= 0) {
                            int ret = SSL_get_error(con->_ssl, xret);
                            if (ret == SSL_ERROR_WANT_READ) {
                                return 0;
                            } else if (ret == SSL_ERROR_WANT_WRITE) {
                                _set_write(con->_fd);
                                return 0;
                            } else if (ret == SSL_ERROR_ZERO_RETURN) {
                                printf("Zero return. TLS/SSL conneciton has been closed.\n");
                                return 0;
                            } else if (ret == SSL_ERROR_NONE) {
                                printf("Error none %zu\n", iovec[0].iov_len);
                            } else if (ret == SSL_ERROR_SYSCALL) {
                                char buf[128] = {};
                                strerror_r(errno, buf, 128);
                                unsigned long err = ERR_get_error();
                                // if (err == 0) { // closed?}

                                printf("Syscall Errno %d %lu %zu [%s]\n", errno, err,  iovec[0].iov_len, buf);
                                return -1;
                            } else {
                                unsigned long err = ERR_get_error();
                                printf("Some other error to handle %d %d %ld [%s]\n", xret, ret, err, ERR_error_string(err, nullptr));
                                printf("Some other error to handle %ld [%s]\n", err, ERR_reason_error_string(err));
                                return -4;
                            }
                        }
                        return xret;
                    }

                    // Only checks index 0
                    int WritevNonBlock (int fd, const struct iovec *iovec, int count) {
                        if (count <= 0) return -5;
                        if (fd >= _fdToCon.size()) return -1;
                        if (!_fdToCon[fd]) return -2;
                        std::shared_ptr<SSLConnection> &con = _fdToCon[fd];
                        if (!con) return -3;

                        int ret = SSL_write(con->_ssl, iovec[0].iov_base, iovec[0].iov_len);
                        
                        if (ret <= 0) {
                            ret = SSL_get_error(con->_ssl, ret);
                            if (ret == SSL_ERROR_WANT_READ) {
                                return 0;
                            } else if (ret == SSL_ERROR_WANT_WRITE) {
                                return 0;
                            } else {
                                unsigned long err = ERR_get_error();
                                printf("Some other error to handle %ld [%s]\n", err, ERR_error_string(err, nullptr));
                                printf("Some other error to handle %ld [%s]\n", err, ERR_reason_error_string(err));
                                return -4;
                            }
                        }
                        return ret;
                    }

                private:
                    OpenSSLManager (const OpenSSLManager &other) = delete;
                    OpenSSLManager &operator= (const OpenSSLManager &other) = delete;

                    std::function <int (int)> _set_write;
                    std::vector <std::shared_ptr<SSLConnection>> _fdToCon;
                    SSL_CTX *_ctx;

                    // forgot to include link to where this is from
                    void dump_cert_info(SSL *ssl, bool server) {
                        if(server) {
                            printf("Ssl server version: %s", SSL_get_version(ssl));
                        }
                        else {
                            printf("Client Version: %s", SSL_get_version(ssl));
                        }

                        /* The cipher negotiated and being used */
                        printf("Using cipher %s", SSL_get_cipher(ssl));

                        /* Get client's certificate (note: beware of dynamic allocation) - opt */
                        X509 *client_cert = SSL_get_peer_certificate(ssl);
                        if (client_cert != NULL) {
                            if(server) {
                                printf("Client certificate:\n");
                            }
                            else {
                                printf("Server certificate:\n");
                            }
                            char *str = X509_NAME_oneline(X509_get_subject_name(client_cert), 0, 0);
                            if(str == NULL) {
                                printf("warn X509 subject name is null");
                            }
                            printf("\t Subject: %s\n", str);
                            OPENSSL_free(str);

                            str = X509_NAME_oneline(X509_get_issuer_name(client_cert), 0, 0);
                            if(str == NULL) {
                                printf("warn X509 issuer name is null");
                            }
                            printf("\t Issuer: %s\n", str);
                            OPENSSL_free(str);

                            /* Deallocate certificate, free memory */
                            X509_free(client_cert);
                        } else {
                            printf("Client does not have certificate.\n");
                        }
                    }
            };
        }
    }
}

#endif
