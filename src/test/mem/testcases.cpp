
#include <string>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/evp.h>

#include "gtest/gtest.h"
#include "mem/mem.h"


using namespace coypu::mem;


TEST(MemTest, Test1) 
{
    int node = MemManager::GetMaxNumaNode();
    ASSERT_TRUE(node >= 0);
}
