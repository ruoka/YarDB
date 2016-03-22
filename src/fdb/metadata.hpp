#pragma once

#include <iosfwd>
#include "xson/fson.hpp"

namespace fdb {

struct metadata
{
    bool deleted            = false;
    std::string collection  = "fdb";
    std::int64_t index      = 0; // FIXME: Use uint64_t!
    std::streamoff position = 0;
};

inline std::ostream& operator << (std::ostream& os, const metadata& data)
{
    xson::fson::encoder e{os};
    e.encode(data.deleted);
    e.encode(data.collection);
    e.encode(data.index);
    e.encode(data.position);
    return os;
}

inline std::istream& operator >> (std::istream& is, metadata& data)
{
    xson::fson::decoder d{is};
    d.decode(data.deleted);
    d.decode(data.collection);
    d.decode(data.index);
    d.decode(data.position);
    return is;
}

} // namespace fdb
