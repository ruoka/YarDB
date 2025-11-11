// Copyright (c) 2017 Kaius Ruokonen
// Distributed under the MIT software license
// See LICENCE file or https://opensource.org/licenses/MIT

#include <iostream>
#include <fstream>
#include "cryptic/sha1.hpp"
#include "cryptic/sha2.hpp"

using namespace std::string_literals;

int main()
{
    std::cout << cryptic::sha1::base64("The quick brown fox jumps over the lazy dog"s) << std::endl;

    auto hash = cryptic::sha1{"The quick brown fox jumps over the lazy cog"s};
    std::cout << hash.hexadecimal() << std::endl;
    std::cout << hash.base64() << std::endl;

    auto file = std::ifstream{"./src/example.cpp"s};
    auto test2 = ""s;
    std::getline(file,test2,static_cast<char>(std::char_traits<char>::eof()));
    std::cout << cryptic::sha1::base64(test2) << std::endl;

    auto const test3 = "omQGMC65WBEzzZAX7H8l+g==258EAFA5-E914-47DA-95CA-C5AB0DC85B11"s;
    std::cout << cryptic::sha1::base64(test3) << std::endl;

    auto const test4 = "omQGMC65WBEzzZAX7H8l+g==258EAFA5-E914-47DA-95CA-C5AB0DC85B11_XXXXXX"s;
    std::cout << cryptic::sha1::base64(test4) << std::endl;

    auto const test5 = cryptic::base64::encode("Man"s);
    std::cout << test5 << std::endl;

    auto const test6 = cryptic::base64::decode(test5);
    std::cout << test6 << std::endl;

    auto const test7 = cryptic::base64::decode("TWFu"s);
    std::cout << test7 << std::endl;

    auto const test8 = cryptic::sha224::hexadecimal("The quick brown fox jumps over the lazy dog"s);
    std::cout << test8 << std::endl;

    auto const test9 = cryptic::sha256::base64("The quick brown fox jumps over the lazy dog"s);
    std::cout << test9 << std::endl;

    return 0;
}
