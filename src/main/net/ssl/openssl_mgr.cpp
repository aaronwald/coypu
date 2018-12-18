#include "openssl_mgr.h"

int coypu::net::ssl::VerifyCTX (int preverify_ok, X509_STORE_CTX *x509_ctx) {
  printf("Verify ctx\n");
  return preverify_ok;
}

