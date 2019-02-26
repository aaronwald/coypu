/*
  Copyright 2018 Aaron Wald

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
*/

#ifndef _COYPU_SSL_H
#define _COYPU_SSL_H

#include <stdio.h>
#include <assert.h>
#include <sys/uio.h>

#include <vector>
#include <memory>
#include <functional>
#include <string>

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

				
		int VerifyCTX (int preverify_ok, X509_STORE_CTX *x509_ctx);

		template <typename T>
		  int Verify (int preverify_ok, X509_STORE_CTX *ctx);
						  
		// supports non block io
		template <typename LogTrait>
		  class OpenSSLManager {
		public:
		  typedef struct SSLConnectionT {
			 int _fd;
			 SSL *_ssl;

			 SSLConnectionT (int fd, SSL *ssl) : _fd(fd),  _ssl(ssl) {
			 }
		  } SSLConnection;

		  OpenSSLManager (LogTrait &logger, std::function <int (int)> set_write, const std::string &CApath) : _logger(logger),
			 _set_write(set_write), _ctx(nullptr) {
			 _ctx = SSL_CTX_new(SSLv23_method());
			 if (!_ctx) {
				// a->error("SSL init failed.");
				assert(false);
			 }
			 SSL_CTX_set_options(_ctx, SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 |
										SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
			 SSL_CTX_set_verify(_ctx, SSL_VERIFY_PEER, VerifyCTX);
			 SSL_CTX_load_verify_locations(_ctx, nullptr, CApath.c_str());
			 SSL_CTX_set_verify_depth(_ctx, 10); // 10?
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

			 //std::function<int(int, X509_STORE_CTX *)> cb = std::bind(&OpenSSLManager<LogTrait>::VerifyCTX, this, std::placeholders::_1, std::placeholders::_2); // doesnt work
			 SSL_set_verify(ssl, SSL_VERIFY_PEER, Verify<LogTrait>);
			 SSL_set_verify_depth(ssl, 10); // 10 ?
			 SSL_set_ex_data(ssl, 1, this);
 								
			 _logger->info("SSL CTX verifyMode[{0}] verifyMode[{1}] fd[{2}]",
								SSL_CTX_get_verify_mode(_ctx),
								SSL_get_verify_mode(ssl),
								fd);

			 if (fd+1 > _fdToCon.size()) {
				_fdToCon.resize(fd+1, nullptr);
			 }

			 assert(_fdToCon[fd] == nullptr);
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
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_error_string(err, nullptr));
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_reason_error_string(err));
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
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_error_string(err, nullptr));
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_reason_error_string(err));
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
				  _logger->info("SSL want read");
				  return 0;
				} else if (ret == SSL_ERROR_WANT_WRITE) {
				  _set_write(con->_fd);
				  return 0;
				} else if (ret == SSL_ERROR_ZERO_RETURN) {
				  _logger->info("Zero return. TLS/SSL conneciton has been closed.");
				  return 0;
				} else if (ret == SSL_ERROR_NONE) {
				  _logger->info("Error none {0}", iovec[0].iov_len);
				} else if (ret == SSL_ERROR_SYSCALL) {
				  char buf[1024] = {};
				  strerror_r(errno, buf, 1024);
				  // if (err == 0) { // closed?}
				  unsigned long err = ERR_get_error();
										  
				  _logger->warn("Syscall Errno errno[{0}] ret[{1}] iov_len[{2}] err[{3}]", errno, ret, iovec[0].iov_len, err);
				  // could be closed socket from server
				  return -1;
				} else {
				  unsigned long err = ERR_get_error();
				  _logger->warn("Some other error to handle {0} {1} {2} [{3}]", xret, ret, err, ERR_error_string(err, nullptr));
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_reason_error_string(err));
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
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_error_string(err, nullptr));
				  _logger->warn("Some other error to handle {0} [{1}]", err, ERR_reason_error_string(err));
				  return -4;
				}
			 }
			 return ret;
		  }

		  template <typename T>
			 friend int Verify (int preverify_ok, X509_STORE_CTX *ctx);

		private:
		  OpenSSLManager (const OpenSSLManager &other) = delete;
		  OpenSSLManager &operator= (const OpenSSLManager &other) = delete;

		  LogTrait _logger;
		  std::function <int (int)> _set_write;
		  std::vector <std::shared_ptr<SSLConnection>> _fdToCon;
		  SSL_CTX *_ctx;
		};

		template <typename T>
		  int Verify (int preverify_ok, X509_STORE_CTX *ctx) {
		  char    buf[256];
		  X509   *err_cert;
		  int     err, depth;
		  SSL    *ssl;

		  err_cert = X509_STORE_CTX_get_current_cert(ctx);
		  err = X509_STORE_CTX_get_error(ctx);
		  depth = X509_STORE_CTX_get_error_depth(ctx);
				  
		  /*
			* Retrieve the pointer to the SSL of the connection currently treated
			* and the application specific data stored into the SSL object.
			*/
		  ssl = reinterpret_cast<SSL *>(X509_STORE_CTX_get_ex_data(ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
		  void *p = SSL_get_ex_data(ssl, 1);
		  assert(p);
		  OpenSSLManager <T> *sslmgr = reinterpret_cast<OpenSSLManager<T>*>(p);
				  
		  X509_NAME_oneline(X509_get_subject_name(err_cert), buf, 256);
				  
		  /*
			* Catch a too long certificate chain. The depth limit set using
			* SSL_CTX_set_verify_depth() is by purpose set to "limit+1" so
			* that whenever the "depth>verify_depth" condition is met, we
			* have violated the limit and want to log this error condition.
			* We must do it here, because the CHAIN_TOO_LONG error would not
			* be found explicitly; only errors introduced by cutting off the
			* additional certificates would be logged.
			*/
		  int verify_depth = 10;
		  if (depth > verify_depth) {
			 preverify_ok = 0;
			 err = X509_V_ERR_CERT_CHAIN_TOO_LONG;
			 X509_STORE_CTX_set_error(ctx, err);
		  }
		  if (!preverify_ok) {
			 sslmgr->_logger->error("verify error:num={0}:{1}:depth={2}:{3}", err,
											X509_verify_cert_error_string(err), depth, buf);
		  }
		  else if (true) {
			 sslmgr->_logger->debug("depth={0}:{1}", depth, buf);
		  }
				  
		  /*
			* At this point, err contains the last verification error. We can use
			* it for something special
			*/
		  if (!preverify_ok && (err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT)) {
			 X509_NAME_oneline(X509_get_issuer_name(err_cert), buf, 256);
			 sslmgr->_logger->error("issuer={0}", buf);
		  }
				  
		  bool always_continue = false;
		  if (always_continue) {
			 return 1;
		  } else {
			 return preverify_ok;
		  }
				  
		  return preverify_ok;
		}
	 }
  }
}

#endif
