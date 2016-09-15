#pragma once

#include <map>
#include <string>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace db {

using object = xson::fson::object;

using sequence_type = std::int64_t;

using primary_key_type = sequence_type;

using secondary_index_name = std::string;

using secondary_key_type = std::string;

using position_type = std::streamoff;

using primary_index_type = std::map<primary_key_type,
                                    position_type>;

using secondary_index_type = std::map<secondary_key_type,
                                      position_type,
                                      xson::less>;

using secondary_index_map = std::map<secondary_index_name,
                                     secondary_index_type>;

class index_iterator : public std::iterator<std::forward_iterator_tag,position_type>
{
public:

    enum index_type {primary, secondary};

    using primary_iterator = primary_index_type::const_iterator;

    using secondary_iterator = secondary_index_type::const_iterator;

    index_iterator() = default;

    index_iterator(primary_iterator current) :
        m_primary_current{current},
        m_secondary_current{},
        m_index_type{primary}
    {}

    index_iterator(secondary_iterator current) :
        m_primary_current{},
        m_secondary_current{current},
        m_index_type{primary}
    {}

    index_iterator(const index_iterator& itr) :
        m_primary_current{itr.m_primary_current},
        m_secondary_current{itr.m_secondary_current},
        m_index_type{itr.m_index_type}
    {}

    auto operator * ()
    {
        if(m_index_type == primary)
            return std::get<position_type>(*m_primary_current);
        else
            return std::get<position_type>(*m_secondary_current);
    }

    auto& operator ++ ()
    {
        if(m_index_type == primary)
            ++m_primary_current;
        else
            ++m_secondary_current;
        return *this;
    }

    auto operator != (const index_iterator& itr) const
    {
        if(m_index_type == primary)
            return m_primary_current != itr.m_primary_current;
        else
            return m_secondary_current != itr.m_secondary_current;
    }

private:

    primary_iterator m_primary_current;

    secondary_iterator m_secondary_current;

    index_type m_index_type;
};

class index_range
{
public:

    index_range(index_iterator& begin, index_iterator& end) :
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

    index_range range(const object& selector) const;

    // position_type position(const object& selector) const;

    void update(object& document);

    void insert(object& document, position_type position);

    void erase(const object& document);

private:

    sequence_type  m_sequence;

    primary_index_type m_primary_keys;

    secondary_index_map m_secondary_keys;
};

} // namespace db
