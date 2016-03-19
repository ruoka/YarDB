#pragma once

#include <string>
#include "xson/fson.hpp"

namespace fdb {

using xson::fson::object;
using xson::fson::encoder;
using xson::fson::decoder;

struct header
{
    enum : std::int8_t {create = 'C', update = 'U', destroy = 'D', query = 'Q', reply = 'R'};
    std::uint8_t operaion;
    std::string collection;
};

inline std::istream& operator >> (std::istream& is, header& hdr)
{
    decoder d{is};
    d.decode(hdr.operaion);
    d.decode(hdr.collection);
    return is;
}

///////////////////////////////////////////////////////////////////////////////

struct create
{
    header hdr;
    object document;
};

inline std::istream& operator >> (std::istream& is, create& crt)
{
    decoder d{is};
    d.decode(crt.document);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const create& crt)
{
    encoder e{os};
    e.encode(crt.hdr.operaion);
    e.encode(crt.hdr.collection);
    e.encode(crt.document);
    return os;
}

///////////////////////////////////////////////////////////////////////////////

struct update
{
    header hdr;
    object selector;
    object document;
};

///////////////////////////////////////////////////////////////////////////////

struct destroy
{
    header hdr;
    object selector;
};

///////////////////////////////////////////////////////////////////////////////

struct query
{
    header hdr;
    object selector;
};

inline std::istream& operator >> (std::istream& is, query& qry)
{
    decoder d{is};
    d.decode(qry.selector);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const query& qry)
{
    encoder e{os};
    e.encode(qry.hdr.operaion);
    e.encode(qry.hdr.collection);
    e.encode(qry.selector);
    return os;
}

///////////////////////////////////////////////////////////////////////////////

struct reply
{
    header hdr;
    object documents;
};

inline std::istream& operator >> (std::istream& is, reply& rply)
{
    decoder d{is};
    d.decode(rply.documents);
    return is;
}

inline std::ostream& operator << (std::ostream& os, const reply& rply)
{
    encoder e{os};
    e.encode(rply.hdr.operaion);
    e.encode(rply.hdr.collection);
    e.encode(rply.documents);
    return os;
}

} // namespace fdb
