#include <algorithm>
#include "db/index.hpp"

void db::index::add(const std::string& key)
{
    if(m_secondary_keys.count(key) == 0)
        m_secondary_keys[key] = secondary_index_type{};
}

void db::index::add(std::vector<std::string> keys)
{
    for(const std::string& key : keys)
        add(key);
}

std::vector<std::string> db::index::keys() const
{
    auto result = std::vector<std::string>{};
    for (const auto& index : m_secondary_keys)
        result.push_back(index.first);
    return result;
}

bool db::index::primary_key(const object& selector) const
{
    return selector.has(u8"id"s);
}

bool db::index::secondary_key(const object& selector) const
{
    for(const auto& index : m_secondary_keys)
        if(selector.has(index.first))
            return true;
    return false;
}

db::index_range db::index::range(const object& selector)
{
    auto begin = index_iterator{m_primary_keys.begin()};
    auto end = index_iterator{m_primary_keys.end()};

    if(primary_key(selector))
    {
        if(selector[u8"id"s].has(u8"$gt"s))
        {
            const primary_key_type pk = selector[u8"id"s][u8"$gt"s];
            begin = m_primary_keys.upper_bound(pk);
        }
        else if(selector[u8"id"s].has(u8"$gte"s))
        {
            const primary_key_type pk = selector[u8"id"s][u8"$gte"s];
            begin = m_primary_keys.lower_bound(pk);
        }
        else if(selector[u8"id"s].has(u8"$lt"s))
        {
            const primary_key_type pk = selector[u8"id"s][u8"$lt"s];
            end = m_primary_keys.lower_bound(pk);
        }
        else if(selector[u8"id"s].has(u8"$lte"s))
        {
            const primary_key_type pk = selector[u8"id"s][u8"$lte"s];
            end = m_primary_keys.upper_bound(pk);
        }
        else if(selector[u8"id"s].has(u8"$eq"s))
        {
            const primary_key_type pk = selector[u8"id"s][u8"$eq"s];
            std::tie(begin,end) = m_primary_keys.equal_range(pk);
        }
        else if(selector[u8"id"s].has(u8"$head"s))
        {
            const sequence_type n = selector[u8"id"s][u8"$head"s];
            auto itr = m_primary_keys.begin();
            std::advance(itr, std::min<std::ptrdiff_t>(n, m_primary_keys.size()));
            end = itr;
        }
        else if(selector[u8"id"s].has(u8"$tail"s))
        {
            const sequence_type n = selector[u8"id"s][u8"$tail"s];
            auto itr = m_primary_keys.rbegin();
            std::advance(itr, std::min<std::ptrdiff_t>(n, m_primary_keys.size()));
            begin = itr.base();
        }
        else
        {
            const primary_key_type pk = selector[u8"id"s];
            std::tie(begin,end) = m_primary_keys.equal_range(pk);
        }
    }
    else if(secondary_key(selector)) // Use primary key
    {
        for(auto& key : m_secondary_keys)
            if(selector.has(key.first))
            {
                const auto sk = xson::to_string(selector[key.first]);
                std::tie(begin,end) = key.second.equal_range(sk);
            }
    }

    return {begin,end};
}

// db::position_type db::index::position(const object& selector) const
// {
//     const auto pk = xson::to_string(selector[u8"id"s]);
//     return m_primary_keys.at(pk);
// }

void db::index::update(object& document)
{
    if(document.has(u8"id"s))
        m_sequence = std::max<sequence_type>(m_sequence, document[u8"id"s]);
    else
        document[u8"id"s] = ++m_sequence;
}

void db::index::insert(object& document, position_type position)
{
    const primary_key_type pk = document[u8"id"s];
    m_primary_keys[pk] = position;

    for(auto& key : m_secondary_keys)
        if(document.has(key.first))
        {
            const auto sk = xson::to_string(document[key.first]);
            key.second[sk] = position;
        }
}

void db::index::erase(const object& document)
{
    const primary_key_type pk = document[u8"id"s];
    m_primary_keys.erase(pk);

    for(auto& key : m_secondary_keys)
        if(document.has(key.first))
        {
            const auto sk = xson::to_string(document[key.first]);
            key.second.erase(sk);
        }
}
