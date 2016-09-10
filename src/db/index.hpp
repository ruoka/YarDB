#pragma once

#include <map>
#include <string>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace db {

using object = xson::fson::object;
using sequence_type = std::int64_t;
using position_type = std::streamoff;
using key_type = std::pair<sequence_type,std::string>;
using index_type = std::map<key_type,position_type>;
using primary_index_type = index_type;
using secondary_index_type = std::map<std::string,index_type>;

class index_iterator
{
public:

    using iterator = index_type::iterator;

    index_iterator() = default;

    index_iterator(iterator current, iterator end) :
        m_current{current},
        m_end{end}
    {}

    index_iterator(const index_iterator& itr) :
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

    operator bool () const
    {
        return m_begin != m_end;
    }

private:

    index_iterator m_begin, m_end;
};

class index
{
public:

    index() = default;

    void add(const std::string& key);

    void add(std::vector<std::string> keys);

    std::vector<std::string> keys() const;

    bool primary_key(const object& selector) const;

    bool secondary_key(const object& selector) const;

    index_range range(const object& selector);

    position_type position(const object& selector) const;

    void update(object& document);

    void insert(object& document, position_type position);

    void erase(const object& document);

private:

    sequence_type  m_sequence;

    primary_index_type m_primary_keys;

    secondary_index_type m_secondary_keys;
};

} // namespace db
