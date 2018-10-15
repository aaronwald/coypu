
#include <string>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include "gtest/gtest.h"
#include "http/websocket.h"


using namespace coypu::http::websocket;


TEST(WebsocketTest, Test1) 
{
    const char *check="dGhlIHNhbXBsZSBub25jZQ==";

    std::string cat = std::string(check) + std::string(WEBSOCKET_GUID);

    // sha1 computation for websocket key check
    SHA_CTX ctx;
    ASSERT_EQ(SHA1_Init(&ctx), 1);
    ASSERT_EQ(SHA1_Update(&ctx, cat.c_str(), cat.size()), 1);
    unsigned char sha1[SHA_DIGEST_LENGTH] = {};
    ASSERT_EQ(SHA1_Final(sha1, &ctx), 1);

    constexpr int sfsize = 1+(((SHA_DIGEST_LENGTH/3)+1)*4);
    unsigned char base64[sfsize] = {};
    EVP_EncodeBlock(base64, sha1, SHA_DIGEST_LENGTH);

    const char *result = "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=";
    ASSERT_STREQ(result, reinterpret_cast<char *>(base64));
}
