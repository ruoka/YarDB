#pragma once

#include <string>
#include "xson/fson.hpp"

namespace fdb {

using object = xson::fson::object;
using encoder = xson::fson::encoder;
using decoder = xson::fson::decoder;

struct header
{
    enum : std::int8_t {create = 'C', read = 'R', update = 'U', destroy = 'D', reply = 'R'} operaion;
    std::string collection;
};

inline std::istream& operator >> (std::istream& is, header& hdr)
{
    decoder d{is};
    d.decode(hdr.operaion);
    d.decode(hdr.collection);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const header& hdr)
{
    encoder e{os};
    e.encode(hdr.operaion);
    e.encode(hdr.collection);
    return os;
}

///////////////////////////////////////////////////////////////////////////////

struct create
{
    header hdr = {header::create, "fdb"};
    object document;
};

inline std::istream& operator >> (std::istream& is, create& msg)
{
    decoder d{is};
    d.decode(msg.document);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const create& msg)
{
    encoder e{os};
    e.encode(msg.hdr.operaion);
    e.encode(msg.hdr.collection);
    e.encode(msg.document);
    return os;
}

///////////////////////////////////////////////////////////////////////////////

struct read
{
    header hdr = {header::read, "fdb"};;
    object selector;
};

inline std::istream& operator >> (std::istream& is, read& msg)
{
    decoder d{is};
    d.decode(msg.selector);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const read& msg)
{
    encoder e{os};
    e.encode(msg.hdr.operaion);
    e.encode(msg.hdr.collection);
    e.encode(msg.selector);
    return os;
}

///////////////////////////////////////////////////////////////////////////////

struct update
{
    header hdr = {header::update, "fdb"};
    object selector;
    object document;
};

inline std::istream& operator >> (std::istream& is, update& msg)
{
    decoder d{is};
    d.decode(msg.selector);
    d.decode(msg.selector);
    return is;
}

///////////////////////////////////////////////////////////////////////////////

struct destroy
{
    header hdr = {header::destroy, "fdb"};;
    object selector;
};

inline std::istream& operator >> (std::istream& is, destroy& msg)
{
    decoder d{is};
    d.decode(msg.selector);
    return is;
}

///////////////////////////////////////////////////////////////////////////////

struct reply
{
    header hdr = {header::reply, "fdb"};;
    object document;
};

inline std::istream& operator >> (std::istream& is, reply& msg)
{
    decoder d{is};
    d.decode(msg.document);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const reply& msg)
{
    encoder e{os};
    e.encode(msg.hdr.operaion);
    e.encode(msg.hdr.collection);
    e.encode(msg.document);
    return os;
}

} // namespace fdb
