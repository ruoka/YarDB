#pragma once

#include <iosfwd>
#include "xson/fson.hpp"

namespace fdb {

struct metadata
{
    metadata(bool v = false) : valid{v}
    {};
    bool valid              = false;
    std::string collection  = "fdb";
    std::int64_t index      = 0; // FIXME: Use uint64_t!
    std::streamoff position = 0;
};

inline std::ostream& operator << (std::ostream& os, const metadata& data)
{
    xson::fson::encoder e{os};
    e.encode(data.valid);
    e.encode(data.collection);
    e.encode(data.index);
    e.encode(data.position);
    return os;
}

inline std::istream& operator >> (std::istream& is, metadata& data)
{
    xson::fson::decoder d{is};
    d.decode(data.valid);
    if(data.valid)
    {
        d.decode(data.collection);
        d.decode(data.index);
        d.decode(data.position);
    }
    return is;
}

} // namespace fdb
