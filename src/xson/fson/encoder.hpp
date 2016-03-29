#pragma once

#include "xson/fast/encoder.hpp"
#include "xson/object.hpp"

namespace xson {
namespace fson {

class encoder : public fast::encoder
{
public:

    encoder(std::ostream& os) : fast::encoder{os}
    {}

    using fast::encoder::encode;

    template<typename T>
    std::enable_if_t<std::is_enum<T>::value,void> encode(T e)
    {
        encode(static_cast<std::uint8_t>(e));
    }

    void encode(double_type d)
    {
        union {
            double d64;
            std::uint64_t i64;
        } d2i;
        d2i.d64 = d;
        encode(d2i.i64);
    }

    void encode(boolean_type b)
    {
        if(b)
            encode(std::uint8_t{'\x01'});
        else
            encode(std::uint8_t{'\x00'});
    }

    void encode(const date_type& d)
    {
        encode(static_cast<std::uint64_t>(d.count()));
    }

    void encode(const object& ob)
    {
        switch(ob.type())
        {
            case type::object:
            case type::array:
            for(const auto& o : ob)
            {
                encode(o.second.type()); // type
                encode(o.first);         // name
                encode(o.second);        // object
            }
            encode(xson::eod);
            break;

            case type::int32:
            encode(static_cast<int32_type>(ob));
            break;

            case type::int64:
            encode(static_cast<int64_type>(ob));
            break;

            case type::number:
            encode(static_cast<double_type>(ob));
            break;

            case type::string:
            encode(static_cast<string_type>(ob));
            break;

            case type::boolean:
            encode(static_cast<boolean_type>(ob));
            break;

            case type::date:
            encode(static_cast<date_type>(ob));
            break;

            case type::null:
            break;
        }
    }

};

} // namespace fson
} // namespace xson

namespace std {

inline std::ostream& operator << (std::ostream& os, const xson::object& ob)
{
    xson::fson::encoder{os}.encode(ob);
    return os;
}

}
