#pragma once

#include <string>
#include <experimental/string_view>

// scheme:[//[username:password@]host[:port]][/]path[?query][#fragment]

namespace net
{

using string_view = std::experimental::string_view;

using namespace std::string_literals;

struct uri
{

explicit uri(string_view string)
{
    auto position = string.npos;

    position = string.find_first_of(":@[]/?#");         // gen-delims

    if(position > 0 && string.at(position) == ':')      // scheme name
    {
        absolute = true;
        scheme.assign(string.data(), position);
        string.remove_prefix(position+1);               // scheme:
    }

    position = string.find_first_not_of("/");

    if(position == 2)                                   // authority component
    {
        string.remove_prefix(2);                        // //

        position = string.find_first_of("/?#");
        if(position == string.npos)
            position = string.length();
        auto authority = string.substr(0, position);
        string.remove_prefix(position);                 // authority

        position = authority.find_first_of('@');
        if(position != string.npos)
        {
            userinfo.assign(authority.data(), position);
            authority.remove_prefix(position + 1);      // userinfo@
        }

        position = authority.find_last_of(':');
        if(position != string.npos)
        {
            auto size = authority.length() - position;
            port.assign(authority.data() + position + 1, size - 1);
            authority.remove_suffix(size);              // :port
        }

        position = authority.length();
        host.assign(authority.data(), position);
        authority.remove_prefix(position);              // host
    }

    position = string.find_first_of("?#");              // path component
    if(position == string.npos)
        position = string.length();
    path.assign(string.data(), position);
    string.remove_prefix(position);                     // path

    if(string.front() == '?')                           // query component
    {
        string.remove_prefix(1);                        // ?
        position = string.find_first_of("#");
        if(position == string.npos)
            position = string.length();
        query.assign(string.data(), position);
        string.remove_prefix(position);                 // query
    }

    if(string.front() == '#')                           // fragment component
    {
        string.remove_prefix(1);                        // #
        position = string.length();
        fragment.assign(string.data(), position);
        string.remove_prefix(position);                 // fragment
    }
}

struct property
{
    operator string_view () const
    {
        return m_data;
    }

    auto operator [] (std::size_t idx) const
    {
        auto tmp = m_data;
        auto pos = tmp.find_first_of('/');
        while(idx && pos != tmp.npos && tmp.at(pos) == '/')
        {
            tmp.remove_prefix(pos + 1);
            pos = tmp.find_first_of("/?#");
            --idx;
        }
        if(pos == tmp.npos) pos = tmp.length();
        if(!idx)
            return tmp.substr(0, pos);
        else
            return string_view{};
    }

private:

    friend class uri;

    void assign(const char* s, std::size_t sz)
    {
        m_data = {s,sz};
    }

    string_view m_data;
};

bool absolute = false;

property scheme;

property userinfo;

property host;

property port;

property path;

property query;

property fragment;

};

inline auto& operator << (std::ostream& os, const uri::property& p)
{
    os << static_cast<const string_view>(p);
    return os;
}

} // namespace net
