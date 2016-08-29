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

    index() = default;

    void add(const std::string& key)
    {
        m_secondary_keys[key] = primary_index_type{};
    }

    const auto primary_key(const object& selector) const
    {
        for(auto& key : m_secondary_keys)
            if(selector.has(key.first))
                return true;
        return false;
    }

    index_range range(const object& selector)
    {
        index_iterator begin, end;

        if(primary_key(selector)) // Use primary key
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

    void insert(const std::string& key, position_type position, const object& document)
    {
        m_primary_keys[key] = position;

        for(auto& key : m_secondary_keys)
            if(document.has(key.first))
                key.second[document[key.first]] = position;

    }

    void erase(const std::string& key, const object& document)
    {
        m_primary_keys.erase(key);

        for(auto& key : m_secondary_keys)
            if(document.has(key.first))
                key.second.erase(document[key.first]);
    }

private:

    primary_index_type m_primary_keys;

    secondary_index_type m_secondary_keys;
};

} // namespace db
