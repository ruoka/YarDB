
// Copyright (c) 2017 Kaius Ruokonen
// Distributed under the MIT software license
// See LICENCE file or https://opensource.org/licenses/MIT

#include <chrono>
#include <iostream>
#include "cryptic/sha1.hpp"
#include "cryptic/sha2.hpp"

#include <sys/types.h>
#include <openssl/sha.h>

using namespace std::literals;

//constexpr auto loops = 5'000'000ul;

constexpr auto loops = 100'000ul;

static auto test_case()
{
return R"(XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

SHA1 benchmark against openssl crypto

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

XXXXXXXXXXXXXXXXXXXXXXXXXX

XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY
YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY

XXXXXXXXXXXXXXXXXXXXXXXXXX

XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX

)"sv;
}

static auto cryptic_hsa1_test()
{
    const auto test = test_case();
    const auto t1 = std::chrono::high_resolution_clock::now();
    for(auto i = loops; i; --i)
    {
        auto sha1 = cryptic::sha1{};
        auto hash = std::array<std::byte,20>{};
        sha1.hash(test);
        sha1.encode(hash);
 
        if(i == 1)
        {
            std::clog << cryptic::sha1::hexadecimal(test) << '\n';
        }
    }
    const auto t2 = std::chrono::high_resolution_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);

    return ms.count();
}

static auto cryptic_hsa256_test()
{
    const auto test = test_case();
    const auto t1 = std::chrono::high_resolution_clock::now();
    for(auto i = loops; i; --i)
    {
        auto sha256 = cryptic::sha256{};
        auto hash = std::array<std::byte,32>{};
        sha256.hash(test);
        sha256.encode(hash);

        if(i == 1)
        {
           std::clog << cryptic::sha256::hexadecimal(test) << '\n';
        }
    }
    const auto t2 = std::chrono::high_resolution_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);

    return ms.count();
}

static auto crypto_hsa1_test()
{
    const auto test = test_case();
    const auto t1 = std::chrono::high_resolution_clock::now();
    for(auto i = loops; i; --i)
    {
        SHA_CTX ctx;
        unsigned char digest[SHA_DIGEST_LENGTH];
        SHA1_Init(&ctx);
        SHA1_Update(&ctx, static_cast<const char*>(test.data()), test.size());
        SHA1_Final(digest, &ctx);
 
        if(i == 1)
        {
            for(auto i = 0u; i < SHA_DIGEST_LENGTH; ++i)
                std::clog << std::setw(2) << std::setfill('0') << std::hex << static_cast<unsigned>(digest[i]);
            std::clog << std::dec << '\n';
        }
    }
    const auto t2 = std::chrono::high_resolution_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);

    return ms.count();
}

static auto crypto_hsa256_test()
{
    const auto test = test_case();
    const auto t1 = std::chrono::high_resolution_clock::now();
    for(auto i = loops; i; --i)
    {
        SHA256_CTX ctx;
        unsigned char digest[SHA256_DIGEST_LENGTH];
        SHA256_Init(&ctx);
        SHA256_Update(&ctx, static_cast<const char*>(test.data()), test.size());
        SHA256_Final(digest, &ctx);
 
         if(i == 1)
        {
            for(auto i = 0u; i < SHA256_DIGEST_LENGTH; ++i)
                std::clog << std::setw(2) << std::setfill('0') << std::hex << static_cast<unsigned>(digest[i]);
            std::clog << std::dec << '\n';
        }

    }
    const auto t2 = std::chrono::high_resolution_clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1);

    return ms.count();
}

int main()
{
    std::clog << "SHA1 & SHA256 benchmark against openssl crypto - "
              << "looping " << loops << " times:\n";
    auto const t1 = cryptic_hsa1_test();
    std::clog << "cryptic SHA1: " << t1 << " ms\n";
    auto const t2 = crypto_hsa1_test();
    std::clog << "openssl crypto SHA1: "<< t2 << " ms\n";
    std::clog << "openssl SHA1 was " << static_cast<float>(t1)/t2 << " times faster\n";
    auto const t3 = cryptic_hsa256_test();
    std::clog << "cryptic SHA256: "<< t3 << " ms\n";
    auto const t4 = crypto_hsa256_test();
    std::clog << "openssl crypto SHA256: " << t4 << " ms\n";
    std::clog << "openssl SHA256 was " << static_cast<float>(t3)/t4 << " times faster\n";
    return 0;
}
