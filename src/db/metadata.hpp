#pragma once

#include <iosfwd>
#include "xson/fson.hpp"

namespace db
{
using namespace std::string_literals;

struct metadata
{
    metadata(bool v = true) : valid{v}
    {};
    bool valid              = true;
    std::string collection  = u8"db"s;
    std::int64_t index      = 0; // FIXME: Use uint64_t!
    std::streamoff position = 0;
    std::streamoff previous = -1;
};

static const metadata destroyed{false};

inline std::ostream& operator << (std::ostream& os, const metadata& data)
{
    auto encoder = xson::fson::encoder{os};
    encoder.encode(data.valid);
    if(data.valid)
    {
        encoder.encode(data.collection);
        encoder.encode(data.index);
        encoder.encode(data.position);
        encoder.encode(data.previous);
    }
    return os;
}

inline std::istream& operator >> (std::istream& is, metadata& data)
{
    auto decoder = xson::fson::decoder{is};
    decoder.decode(data.valid);
    decoder.decode(data.collection);
    decoder.decode(data.index);
    decoder.decode(data.position);
    decoder.decode(data.previous);
    return is;
}

} // namespace fdb
