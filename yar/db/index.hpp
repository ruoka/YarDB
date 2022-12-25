#pragma once

#include <map>
#include <string>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace db {

using object = xson::object;

using sequence_type = xson::integer_type;

using primary_key_type = sequence_type;

using secondary_index_name = std::string;

using secondary_key_type = std::string;

using position_type = std::streamoff;

using primary_index_type = std::map<primary_key_type,
                                    position_type>;

using secondary_index_type = std::map<secondary_key_type,
                                      position_type>;

using secondary_index_map = std::map<secondary_index_name,
                                     secondary_index_type>;

class index_iterator
{
public:

    enum index_type {primary, secondary, reverse_primary, reverse_secondary};

    using primary_iterator = primary_index_type::const_iterator;

    using secondary_iterator = secondary_index_type::const_iterator;

    using reverse_primary_iterator = std::reverse_iterator<primary_iterator>;

    using reverse_secondary_iterator = std::reverse_iterator<secondary_iterator>;

    index_iterator() = delete;

    index_iterator(const index_iterator&) = default;

    index_iterator& operator = (const index_iterator&) = delete;

    index_iterator(primary_iterator current) :
        m_primary_current{current},
        m_secondary_current{},
        m_reverse_primary_current{},
        m_reverse_secondary_current{},
        c_index_type{primary}
    {}

    index_iterator(secondary_iterator current) :
        m_primary_current{},
        m_secondary_current{current},
        m_reverse_primary_current{},
        m_reverse_secondary_current{},
        c_index_type{secondary}
    {}

    index_iterator(reverse_primary_iterator current) :
        m_primary_current{},
        m_secondary_current{},
        m_reverse_primary_current{current},
        m_reverse_secondary_current{},
        c_index_type{reverse_primary}
    {}

    index_iterator(reverse_secondary_iterator current) :
        m_primary_current{},
        m_secondary_current{},
        m_reverse_primary_current{},
        m_reverse_secondary_current{current},
        c_index_type{reverse_secondary}
    {}

    auto operator * ()
    {
        switch(c_index_type)
        {
        case primary:
            return std::get<position_type>(*m_primary_current);
        case secondary:
            return std::get<position_type>(*m_secondary_current);
        case reverse_primary:
            return std::get<position_type>(*m_reverse_primary_current);
        case reverse_secondary:
            return std::get<position_type>(*m_reverse_secondary_current);
        }
    }

    auto& operator ++ ()
    {
        switch(c_index_type)
        {
        case primary:
            ++m_primary_current;
            break;
        case secondary:
            ++m_secondary_current;
            break;
        case reverse_primary:
            ++m_reverse_primary_current;
            break;
        case reverse_secondary:
            ++m_reverse_secondary_current;
            break;
        }
        return *this;
    }

    auto operator != (const index_iterator& itr) const
    {
        switch(c_index_type)
        {
        case primary:
            return m_primary_current != itr.m_primary_current;
        case secondary:
            return m_secondary_current != itr.m_secondary_current;
        case reverse_primary:
            return m_reverse_primary_current != itr.m_reverse_primary_current;
        case reverse_secondary:
            return m_reverse_secondary_current != itr.m_reverse_secondary_current;
        }
    }

private:

    primary_iterator m_primary_current;

    secondary_iterator m_secondary_current;

    reverse_primary_iterator m_reverse_primary_current;

    reverse_secondary_iterator m_reverse_secondary_current;

    const index_type c_index_type;
};

class index_range
{
public:

    template<typename T>
    index_range(T begin, T end) :
        m_begin{begin},
        m_end{end}
    {}

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

    index() = default;

    void add(const std::string& key);

    void add(std::vector<std::string> keys);

    std::vector<std::string> keys() const;

    bool primary_key(const object& selector) const;

    bool secondary_key(const object& selector) const;

    index_range range(const object& selector) const;

    void update(object& document);

    void insert(object& document, position_type position);

    void erase(const object& document);

private:

    sequence_type  m_sequence;

    primary_index_type m_primary_keys;

    secondary_index_map m_secondary_keys;
};

} // namespace db
