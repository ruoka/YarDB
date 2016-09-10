#include <algorithm>
#include "db/index.hpp"

void db::index::add(const std::string& key)
{
    if(m_secondary_keys.count(key) == 0)
        m_secondary_keys[key] = index_type{};
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
    return selector.has(u8"_id"s);
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
    index_iterator begin, end;

    if(primary_key(selector))
    {
        if(selector[u8"_id"s].has(u8"gt"s))
        {
            const auto pk = xson::to_string(selector[u8"_id"s][u8"gt"s]);
            begin = index_iterator{m_primary_keys.upper_bound(pk), m_primary_keys.end()};
            end = index_iterator{m_primary_keys.end(), m_primary_keys.end()};
        }
        else if(selector[u8"_id"s].has(u8"gte"s))
        {
            const auto pk = xson::to_string(selector[u8"_id"s][u8"gte"s]);
            begin = index_iterator{m_primary_keys.lower_bound(pk), m_primary_keys.end()};
            end = index_iterator{m_primary_keys.end(), m_primary_keys.end()};
        }
        else if(selector[u8"_id"s].has(u8"lt"s))
        {
            const auto pk = xson::to_string(selector[u8"_id"s][u8"lt"s]);
            begin = index_iterator{m_primary_keys.begin(), m_primary_keys.end()};
            end = index_iterator{m_primary_keys.lower_bound(pk), m_primary_keys.end()};
        }
        else if(selector[u8"_id"s].has(u8"lte"s))
        {
            const auto pk = xson::to_string(selector[u8"_id"s][u8"lte"s]);
            begin = index_iterator{m_primary_keys.begin(), m_primary_keys.end()};
            end = index_iterator{m_primary_keys.upper_bound(pk), m_primary_keys.end()};
        }
        else if(selector[u8"_id"s].has(u8"eq"s))
        {
            const auto pk = xson::to_string(selector[u8"_id"s][u8"eq"s]);
            begin = end = index_iterator{m_primary_keys.find(pk), m_primary_keys.end()};
            ++end;
        }
        else if(selector[u8"_id"s].has(u8"tail"s))
        {
            const std::int64_t n = selector[u8"_id"s][u8"tail"s];
            if(n > 0)
            {
                auto itr = m_primary_keys.rbegin();
                std::advance(itr, std::min<std::size_t>(n-1,m_primary_keys.size()-1));
                const auto pk = itr->first;
                begin = index_iterator{m_primary_keys.find(pk), m_primary_keys.end()};
                end = index_iterator{m_primary_keys.end(), m_primary_keys.end()};
            }
        }
        else if(selector[u8"_id"s].has(u8"eq"s))
        {
            const auto pk = xson::to_string(selector[u8"_id"s][u8"eq"s]);
            begin = end = index_iterator{m_primary_keys.find(pk), m_primary_keys.end()};
            ++end;
        }
        else
        {
            const auto pk = xson::to_string(selector[u8"_id"s]);
            begin = end = index_iterator{m_primary_keys.lower_bound(pk), m_primary_keys.end()};
            ++end;
        }
    }
    else if(secondary_key(selector)) // Use primary key
    {
        for(auto& key : m_secondary_keys)
            if(selector.has(key.first))
            {
                const auto sk = xson::to_string(selector[key.first]);
                begin = end = index_iterator{key.second.find(sk), key.second.end()};
            }
        ++end;
    }
    else // Fllback to full scan
    {
        begin = index_iterator{m_primary_keys.begin(), m_primary_keys.end()};
        end = index_iterator{m_primary_keys.end(), m_primary_keys.end()};
    }
    return {begin, end};
}

// db::position_type db::index::position(const object& selector) const
// {
//     const auto pk = xson::to_string(selector[u8"_id"s]);
//     return m_primary_keys.at(pk);
// }

void db::index::update(object& document)
{
    if(document.has(u8"_id"s))
        m_sequence = std::max<sequence_type>(m_sequence, document[u8"_id"s]);
    else
        document[u8"_id"s] = ++m_sequence;
}

void db::index::insert(object& document, position_type position)
{
    const auto pk = xson::to_string(document[u8"_id"s]);
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
    const auto pk = xson::to_string(document[u8"_id"s]);
    m_primary_keys.erase(pk);

    for(auto& key : m_secondary_keys)
        if(document.has(key.first))
        {
            const auto sk = xson::to_string(document[key.first]);
            key.second.erase(sk);
        }
}
