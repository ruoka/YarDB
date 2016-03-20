#pragma once

#include <iosfwd>
#include "xson/fast/encoder.hpp"
#include "xson/fast/decoder.hpp"

namespace fdb {

struct metadata
{
    std::string collection  = "fdb";
    std::uint64_t id        = 0;
    std::streamoff previous = 0;
    std::streamoff current  = 0;
    std::streamoff next     = 0;
};

inline std::ostream& operator << (std::ostream& os, const metadata& data)
{
    xson::fast::encoder e{os};
    e.encode(data.collection);
    e.encode(data.id);
    e.encode(data.previous);
    e.encode(data.current);
    e.encode(data.next);
    return os;
}

inline std::istream& operator >> (std::istream& is, metadata& data)
{
    xson::fast::decoder d{is};
    d.decode(data.collection);
    d.decode(data.id);
    d.decode(data.previous);
    d.decode(data.current);
    d.decode(data.next);
    return is;
}

} // namespace fdb
