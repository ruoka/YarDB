#pragma once

#include <iosfwd>
#include "xson/fson.hpp"

namespace db
{
using namespace std::string_literals;

struct metadata
{
    metadata(bool v = false) : valid{v}
    {};
    bool valid              = false;
    std::string collection  = u8"db"s;
    std::int64_t index      = 0; // FIXME: Use uint64_t!
    std::streamoff position = 0;
};

inline std::ostream& operator << (std::ostream& os, const metadata& data)
{
    auto e = xson::fson::encoder{os};
    e.encode(data.valid);
    if(data.valid)
    {
        e.encode(data.collection);
        e.encode(data.index);
        e.encode(data.position);
    }
    return os;
}

inline std::istream& operator >> (std::istream& is, metadata& data)
{
    auto d = xson::fson::decoder{is};
    d.decode(data.valid);
    d.decode(data.collection);
    d.decode(data.index);
    d.decode(data.position);
    return is;
}

} // namespace fdb
