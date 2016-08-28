#pragma once

#include <map>
#include "xson/fson.hpp"

using namespace std::string_literals;

namespace db {

using object = xson::fson::object;
using sequence_type = std::int64_t;
using position_type = std::streamoff;
using primary_index_type = std::map<std::string,position_type>;
using secondary_index_type = std::map<std::string,std::map<std::string,position_type>>;

class index_iterator
{
public:

    using iterator = primary_index_type::iterator;

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

    const auto primary_key(const object& selector) const
    {
        return selector.has(u8"_id"s);
    }

    const auto secondary_key(const object& selector) const
    {
        for(auto& key : m_secondary_keys)
            if(selector.has(key.first))
                return true;
        return false;
    }

    const auto seek(const object& selector) const
    {
        return primary_key(selector) || secondary_key(selector);
    }

    const auto scan(const object& selector) const
    {
        return !seek(selector);
    }

    index_range range(const object& selector)
    {
        index_iterator begin, end;

        if(primary_key(selector)) // Use the primary key
        {
            begin = end = index_iterator{m_primary_keys.find(selector[u8"_id"s]), m_primary_keys.end()};
            ++end;
        }
        else if(secondary_key(selector)) // Use a secondary key
        {
            for(auto& key : m_secondary_keys)
                if(selector.has(key.first))
                    begin = end = index_iterator{key.second.find(selector[key.first]), key.second.end()};
            ++end;
        }
        else // Fllback to full scan
        {
            begin = index_iterator{m_primary_keys.begin(), m_primary_keys.end()};
            end = index_iterator{m_primary_keys.end(), m_primary_keys.end()};
        }
        return {begin, end};
    }

    void create(const std::string& key)
    {
        m_secondary_keys[key] = primary_index_type{};
    }

    void insert(sequence_type id, position_type position, const object& document)
    {
        m_sequence = std::max(m_sequence, id);
        m_primary_keys[std::to_string(id)] = position;

        for(auto& key : m_secondary_keys)
            if(document.has(key.first))
                key.second.insert(std::pair<std::string, position_type>{document[key.first], position});

    }

    void erase(sequence_type id, position_type position, const object& document)
    {
        m_primary_keys.erase(std::to_string(id));

        for(auto& key : m_secondary_keys)
            if(document.has(key.first))
                key.second.erase(document[key.first]);
    }

private:

    sequence_type  m_sequence{0};

    primary_index_type m_primary_keys;

    secondary_index_type m_secondary_keys;
};

} // namespace db
