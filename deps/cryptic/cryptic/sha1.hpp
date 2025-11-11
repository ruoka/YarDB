// Copyright (c) 2017 Kaius Ruokonen
// Distributed under the MIT software license
// See LICENCE file or https://opensource.org/licenses/MIT

#pragma once
#include <bit>
#include <cstdint>
#include <sstream>
#include <iomanip>
#include <cryptic/base64.hpp>
#include <cryptic/details/rotate.hpp>
#include <cryptic/details/narrow_cast.hpp>

namespace cryptic {

using namespace details;

class sha1
{
public:

    using message_length_type = std::uint64_t;

    using buffer_type = std::array<std::byte,20>;

    consteval sha1() noexcept :
        m_message_length{0ull},
        m_message_digest{0x67452301u,
                         0xEFCDAB89u,
                         0x98BADCFEu,
                         0x10325476u,
                         0xC3D2E1F0u}
    {}

    sha1(auto message) noexcept : sha1()
    {
        hash(message);
    }

    void hash(auto message) noexcept
    {
        reset();

        const auto bytes = std::as_bytes(std::span{message});

        for(auto offset = 0ull; offset <= bytes.size(); offset += chunk_size)
        {
            if((offset + chunk_size) <= bytes.size_bytes())
                update(bytes.subspan(offset,chunk_size));
            else
                finalize(bytes.subspan(offset, bytes.size_bytes() - offset));
        }
    }

    void encode(std::span<std::byte,20> other) const noexcept
    {
        const auto bytes = std::as_bytes(std::span{m_message_digest});
    	for(auto i = std::uint_fast8_t{0u}; i < 20u; i += 4u)
        {
    		other[i+0] = bytes[i+3];
    		other[i+1] = bytes[i+2];
    		other[i+2] = bytes[i+1];
    		other[i+3] = bytes[i+0];
        }
    }

    std::string base64() const
    {
        auto buffer = buffer_type{};
        encode(buffer);
        return base64::encode(buffer);
    }

    static std::string base64(auto message)
    {
        const auto hash = sha1{message};
        return hash.base64();
    }

    std::string hexadecimal() const
    {
        auto ss = std::stringstream{};
        ss << std::setw(8) << std::setfill('0') << std::hex << m_message_digest[0u]
           << std::setw(8) << std::setfill('0') << std::hex << m_message_digest[1u]
           << std::setw(8) << std::setfill('0') << std::hex << m_message_digest[2u]
           << std::setw(8) << std::setfill('0') << std::hex << m_message_digest[3u]
           << std::setw(8) << std::setfill('0') << std::hex << m_message_digest[4u];
        return ss.str();
    }

    static std::string hexadecimal(auto message)
    {
        const auto hash = sha1{message};
        return hash.hexadecimal();
    }

    consteval std::size_t size() const noexcept
    {
        return 20ul;
    }

    bool operator < (const sha1& other) const noexcept
    {
    	for(auto i = std::uint_fast8_t{0u}; i < sizeof m_message_digest; ++i)
            if(m_message_digest[i] != other.m_message_digest[i])
                return m_message_digest[i] < other.m_message_digest[i];
        return false;
    }

    bool operator < (std::span<const std::byte, 20> other) const noexcept
    {
        const auto bytes = std::as_bytes(std::span{m_message_digest});
    	for(auto i = std::uint_fast8_t{0u}; i < other.size(); i += 4u)
        {
    		if(bytes[i+3] != other[i+0]) return bytes[i+3] < other[i+0];
    		if(bytes[i+2] != other[i+1]) return bytes[i+2] < other[i+1];
    		if(bytes[i+1] != other[i+2]) return bytes[i+1] < other[i+2];
    		if(bytes[i+0] != other[i+3]) return bytes[i+0] < other[i+3];
        }
        return false;
    }

private:

    static constexpr std::uint64_t chunk_size = 64u;

    constexpr void reset()
    {
        m_message_length = 0ull;
        m_message_digest[0] = 0x67452301u;
        m_message_digest[1] = 0xEFCDAB89u;
        m_message_digest[2] = 0x98BADCFEu;
        m_message_digest[3] = 0x10325476u;
        m_message_digest[4] = 0xC3D2E1F0u;
    }

    void update(std::span<const std::byte> chunk) noexcept
    {
        Expects(chunk.size() == chunk_size);
        m_message_length += 8u * chunk_size; // NOTE, bits
        transform(chunk.subspan<0,chunk_size>());
    }

    void finalize(std::span<const std::byte> chunk)
    {
        Expects(chunk.size() < chunk_size);
        m_message_length += 8u * chunk.size_bytes(); // NOTE, bits
        auto temp = std::array<std::byte,chunk_size>{};
        auto result = std::ranges::copy(chunk.begin(), chunk.end(), temp.begin());
        auto itr = result.out;
        *itr =  std::byte{0b10000000};
        ++itr;
        if(std::distance(itr, temp.end()) < 8)
        {
            std::ranges::fill(itr, temp.end(), std::byte{0b00000000});
            transform(temp);
            std::ranges::fill(temp, std::byte{0b00000000});
        }
        auto length = std::span{temp}.last<8>();
        encode(length, m_message_length);
        transform(temp);
    }

    constexpr void transform(std::span<const std::byte,chunk_size> chunk) noexcept
    {
        auto words = std::array<std::uint32_t,80>{};

        for(auto i = std::uint_fast8_t{0u}, j = std::uint_fast8_t{0u}; i < 16u; ++i, j += 4u)
            words[i] = std::to_integer<uint32_t>(chunk[j+0]) << 24 xor
                       std::to_integer<uint32_t>(chunk[j+1]) << 16 xor
                       std::to_integer<uint32_t>(chunk[j+2]) <<  8 xor
                       std::to_integer<uint32_t>(chunk[j+3]);

        for(auto i = std::uint_fast8_t{16u}; i < 32u; ++i)
            words[i] = rotl(words[i-3] xor words[i-8] xor words[i-14] xor words[i-16], 1);

        for(auto i = std::uint_fast8_t{32u}; i < 80u; ++i)
            words[i] = rotl(words[i-6] xor words[i-16] xor words[i-28] xor words[i-32], 2);

        auto a = m_message_digest[0],
             b = m_message_digest[1],
             c = m_message_digest[2],
             d = m_message_digest[3],
             e = m_message_digest[4],
             f = 0u,
             k = 0u;

        for(auto i = std::uint_fast8_t{0u}; i < 20u; ++i)
        {
            f = (b bitand c) bitor ((compl b) bitand d);
            k = 0x5A827999u;
            auto temp = rotl(a,5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotl(b,30);
            b = a;
            a = temp;
        }

        for(auto i = std::uint_fast8_t{20u}; i < 40u; ++i)
        {
            f = b xor c xor d;
            k = 0x6ED9EBA1u;
            auto temp = rotl(a,5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotl(b,30);
            b = a;
            a = temp;
        }

        for(auto i = std::uint_fast8_t{40u}; i < 60u; ++i)
        {
            f = (b bitand c) bitor (b bitand d) bitor (c bitand d);
            k = 0x8F1BBCDCu;
            auto temp = rotl(a,5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotl(b,30);
            b = a;
            a = temp;
        }

        for(auto i = std::uint_fast8_t{60u}; i < 80u; ++i)
        {
            f = b xor c xor d;
            k = 0xCA62C1D6u;
            auto temp = rotl(a,5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rotl(b,30);
            b = a;
            a = temp;
        }

        m_message_digest[0] += a;
        m_message_digest[1] += b;
        m_message_digest[2] += c;
        m_message_digest[3] += d;
        m_message_digest[4] += e;
    }

    static constexpr void encode(std::span<std::byte,8> output, const message_length_type length) noexcept
    {
    	output[7] = narrow_cast<std::byte>(length >>  0);
    	output[6] = narrow_cast<std::byte>(length >>  8);
    	output[5] = narrow_cast<std::byte>(length >> 16);
    	output[4] = narrow_cast<std::byte>(length >> 24);
    	output[3] = narrow_cast<std::byte>(length >> 32);
    	output[2] = narrow_cast<std::byte>(length >> 40);
    	output[1] = narrow_cast<std::byte>(length >> 48);
    	output[0] = narrow_cast<std::byte>(length >> 56);
    }

    message_length_type m_message_length;

    std::array<std::uint32_t,5> m_message_digest;
};

} // namespace cryptic
