#pragma once

#include <map>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace fdb {

using object = xson::fson::object;

class range_iterator
{
public:

    range_iterator(fdb::object& selector, std::map<std::int64_t,std::streamoff>::iterator itr) :
        m_selector{selector},
        m_current{itr}
    {}

    std::streamoff operator * ()
    {
        return m_current->second;
    }

    range_iterator& operator ++ () // FIXME
    {
        ++m_current;
        return *this;
    }

    bool operator != (const range_iterator& itr) const
    {
        return m_current != itr.m_current;
    }

private:

    fdb::object& m_selector;

    std::map<std::int64_t,std::streamoff>::iterator m_current;

    friend class range;
};

class range
{
public:

    range(fdb::object& selector, std::map<std::int64_t,std::streamoff>& sequence) :
        m_begin{selector, sequence.begin()},
        m_end{selector, sequence.end()},
        m_sequence{sequence}
    {
        if(selector.has("_id"s)) // We have a primary key
        {
            m_begin.m_current = m_end.m_current = m_sequence.find(selector["_id"s]);
            ++m_end.m_current;
            std::clog << m_begin.m_current->first << ':' << m_begin.m_current->second << '=' << selector["_id"s].value() << std::endl;
        }
    }

    range_iterator begin()
    {
        return m_begin;
    }

    range_iterator end()
    {
        return m_end;
    }

    void destroy(range_iterator& itr)
    {
        itr.m_current = m_sequence.erase(itr.m_current);
    }

private:

    std::map<std::int64_t,std::streamoff>& m_sequence;

    range_iterator m_begin, m_end;
};

class index
{
public:

    std::streamoff& operator [] (const std::int64_t& id)
    {
        return m_sequence[id];
    }

    fdb::range range(fdb::object& selector)
    {
        return fdb::range{selector,m_sequence};
    }

    std::int64_t sequence() const
    {
        return m_sequence.size()+1;
    }

private:

    std::map<std::int64_t,std::streamoff> m_sequence;
};

} // namespace fdb
