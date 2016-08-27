#pragma once

#include <map>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace db {

using object = xson::fson::object;
using sequence_type = std::int64_t;
using index_implementation_type = std::map<sequence_type,std::streamoff>;

class index_iterator
{
public:

    using iterator = index_implementation_type::iterator;

    index_iterator(object& selector, iterator current, iterator end) :
        m_selector{selector},
        m_current{current},
        m_end{end}
    {}

    index_iterator(const index_iterator& itr) :
        m_selector{itr.m_selector},
        m_current{itr.m_current},
        m_end{itr.m_end}
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

    iterator m_current;

    iterator m_end;

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
        return m_impl[id];
    }

    index_range range(object& selector)
    {
        if(selector.has(u8"_id"s)) // We have a primary key
        {
            auto begin = index_iterator{selector, m_impl.find(selector[u8"_id"s]), m_impl.end()};
            auto end = index_iterator{selector, m_impl.end(), m_impl.end()};
            return {begin, end};
        }
        else // Fall back to full table scan
        {
            auto begin = index_iterator{selector, m_impl.begin(), m_impl.end()};
            auto end = index_iterator{selector, m_impl.end(), m_impl.end()};
            return {begin, end};
        }
    }

    index_iterator erase(index_iterator& itr)
    {
        return {itr.m_selector, m_impl.erase(itr.m_current), m_impl.end()};
    }

private:

    sequence_type  m_sequence{0};

    index_implementation_type m_impl;
};

} // namespace db
