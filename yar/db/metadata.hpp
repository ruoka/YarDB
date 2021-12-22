#pragma once

#include <iosfwd>
#include "xson/fson.hpp"

namespace db
{
using namespace std::string_literals;

using xson::fson::object;

struct metadata
{
    enum action : char {created = 'C', updated = 'U', deleted = 'D'};
    metadata(action s = created) : status{s}
    {};
    metadata(const std::string& c) : metadata{created}
    {
        collection = c;
    };
    action status = created;
    std::string collection = ""s;
    std::chrono::system_clock::time_point timestamp = std::chrono::system_clock::time_point{};
    std::streamoff position = -1;
    std::streamoff previous = -1;
};

static const metadata updated{metadata::updated};

static const metadata deleted{metadata::deleted};

inline auto& operator << (std::ostream& os, metadata& data)
{
    data.timestamp = std::chrono::system_clock::now();
    data.previous = data.position;
    data.position = os.tellp();
    auto encoder = xson::fson::encoder{os};
    encoder.encode(data.status);
    encoder.encode(data.collection);
    encoder.encode(data.timestamp);
    encoder.encode(data.position);
    encoder.encode(data.previous);
    return os;
}

inline auto& operator << (std::ostream& os, const metadata& data)
{
    auto encoder = xson::fson::encoder{os};
    encoder.encode(data.status);
    return os;
}

inline auto& operator >> (std::istream& is, metadata& data)
{
    auto decoder = xson::fson::decoder{is};
    decoder.decode(data.status);
    decoder.decode(data.collection);
    decoder.decode(data.timestamp);
    decoder.decode(data.position);
    decoder.decode(data.previous);
    return is;
}

inline auto to_string(metadata::action a)
{
    if(a == metadata::created) return "created"s;
    if(a == metadata::updated) return "updated"s;
    if(a == metadata::deleted) return "deleted"s;
    std::terminate();
}

} // namespace db
