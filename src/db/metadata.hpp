#pragma once

#include <iosfwd>
#include "xson/fson.hpp"

namespace db
{
using namespace std::string_literals;

struct metadata
{
    enum action : char {created = 'C', updated = 'U', deleted = 'D'};
    metadata(action s = created) : status{s}
    {};
    metadata(const std::string& c) : metadata{created}
    {
        collection = c;
    };
    action status           = created;
    std::string collection  = u8"db"s;
    std::streamoff position = -1;
    std::streamoff previous = -1;
    bool valid() const {return status == created;}
};

static const metadata updated{metadata::updated};

static const metadata deleted{metadata::deleted};

inline std::ostream& operator << (std::ostream& os, metadata& data)
{
    data.previous = data.position;
    data.position = os.tellp();
    auto encoder = xson::fson::encoder{os};
    encoder.encode(data.status);
    if(data.valid())
    {
        encoder.encode(data.collection);
        encoder.encode(data.position);
        encoder.encode(data.previous);
    }
    return os;
}

inline std::ostream& operator << (std::ostream& os, const metadata& data)
{
    auto encoder = xson::fson::encoder{os};
    encoder.encode(data.status);
    return os;
}

inline std::istream& operator >> (std::istream& is, metadata& data)
{
    auto decoder = xson::fson::decoder{is};
    decoder.decode(data.status);
    decoder.decode(data.collection);
    decoder.decode(data.position);
    decoder.decode(data.previous);
    return is;
}

} // namespace fdb
