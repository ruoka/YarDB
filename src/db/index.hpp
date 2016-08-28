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

    index_iterator(const object& selector, iterator current, iterator end) :
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

    auto& operator ++ ()
    {
        if(m_current != m_end)
            ++m_current;
        return *this;
    }

    auto operator != (const index_iterator& itr) const
    {
        return m_current != itr.m_current;
    }

private:

    std::reference_wrapper<const object> m_selector;

    iterator m_current, m_end;

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

    const auto primary_key(const object& selector)
    {
        return selector.has(u8"_id"s);
    }

    const auto seek(const object& selector)
    {
        return primary_key(selector); // FIXME Not yet implemented
    }

    const auto scan(const object& selector)
    {
        return !primary_key(selector);
    }

    index_range range(const object& selector)
    {
        if(primary_key(selector)) // We have a primary key
        {
            auto begin = index_iterator{selector, m_impl.find(selector[u8"_id"s]), m_impl.end()};
            auto end = begin;
            return {begin, ++end};
        }
        else // Fall back to full table scan / seek
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
