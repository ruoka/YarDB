#pragma once

#include <map>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace fdb {

using object = xson::fson::object;
using sequence_type = std::int64_t;
using implementation_type = std::map<sequence_type,std::streamoff>;
using implementation_iterator = implementation_type::iterator;

class index_iterator
{
public:

    index_iterator(object& selector) :
        m_selector{selector},
        m_current{}
    {}

    index_iterator(object& selector, implementation_iterator itr) :
        m_selector{selector},
        m_current{itr}
    {}

    index_iterator(const index_iterator& itr) :
        m_selector{itr.m_selector},
        m_current{itr.m_current}
    {}

    auto operator * ()
    {
        return m_current->second;
    }

    auto& operator ++ () // FIXME
    {
        ++m_current;
        return *this;
    }

    auto operator != (const index_iterator& itr) const
    {
        return m_current != itr.m_current;
    }

private:

    std::reference_wrapper<object> m_selector;

    implementation_iterator m_current;

    friend class index;
};

class index_range
{
public:

    index_range(index_iterator& begin, index_iterator& end) :
        m_begin{begin},
        m_end{end}
    {}

    auto begin()
    {
        return m_begin;
    }

    auto end()
    {
        return m_end;
    }

    auto begin() const
    {
        return m_begin;
    }

    auto end() const
    {
        return m_end;
    }

private:

    index_iterator m_begin, m_end;
};

class index
{
public:

    auto sequence()
    {
        return ++m_sequence;
    }

    auto& operator [] (const sequence_type& id)
    {
        m_sequence = std::max(m_sequence, id);
        return m_implementation[id];
    }

    index_range range(object& selector)
    {
        auto begin = index_iterator{selector};
        auto end = index_iterator{selector};
        if(selector.has("_id"s)) // We have a primary key
        {
            begin.m_current = end.m_current = m_implementation.find(selector["_id"s]);
            if(end.m_current != m_implementation.end())
                ++end.m_current;
        }
        else // Full table scan
        {
            begin.m_current = m_implementation.begin();
            end.m_current = m_implementation.end();
        }
        return {begin, end};
    }

    index_iterator erase(index_iterator& itr)
    {
        return {itr.m_selector, m_implementation.erase(itr.m_current)};
    }

private:

    sequence_type  m_sequence{0};

    implementation_type m_implementation;
};

} // namespace fdb
