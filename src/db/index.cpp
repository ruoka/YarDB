#include "db/index.hpp"

namespace {

auto make_primary_key   = [](const xson::value& v){return xson::get<xson::int64_type>(v);};

auto make_secondary_key = [](const xson::value& v){return xson::to_string(v);};

template <typename T, typename F>
db::index_range query_analysis(const db::object& selector, const T& keys, F make_key)
{
    auto begin = keys.cbegin();
    auto end = keys.cend();

    if(selector.has(u8"$gt"s))
    {
        auto key = make_key(selector[u8"$gt"s]);
        begin = keys.upper_bound(key);
    }
    else if(selector.has(u8"$gte"s))
    {
        auto key = make_key(selector[u8"$gte"s]);
        begin = keys.lower_bound(key);
    }
    else if(selector.has(u8"$lt"s))
    {
        auto key = make_key(selector[u8"$lt"s]);
        end = keys.lower_bound(key);
    }
    else if(selector.has(u8"$lte"s))
    {
        auto key = make_key(selector[u8"$lte"s]);
        end = keys.upper_bound(key);
    }
    else if(selector.has(u8"$eq"s))
    {
        auto key = make_key(selector[u8"$eq"s]);
        std::tie(begin,end) = keys.equal_range(key);
    }
    else if(selector.has(u8"$head"s))
    {
        xson::int64_type n = selector[u8"$head"s];
        auto itr = keys.begin();
        std::advance(itr, std::min<xson::int64_type>(n, keys.size()));
        end = itr;
    }
    else if(selector.has(u8"$tail"s))
    {
        xson::int64_type n = selector[u8"$tail"s];
        auto itr = keys.rbegin();
        std::advance(itr, std::min<xson::int64_type>(n, keys.size()));
        begin = itr.base();
    }
    else if(xson::holds_alternative<typename T::mapped_type>(selector.value())) // FIXME
    {
        auto key = make_key(selector);
        std::tie(begin,end) = keys.equal_range(key);
    }

    if(!selector.has(u8"$desc"s))
        return {begin, end};
    else
        return {std::make_reverse_iterator(end), std::make_reverse_iterator(begin)};
}

} // namespace

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
    return selector.has(u8"_id"s);
}

bool db::index::secondary_key(const object& selector) const
{
    for(const auto& index : m_secondary_keys)
        if(selector.has(index.first))
            return true;
    return false;
}

db::index_range db::index::range(const object& selector) const
{
    if(primary_key(selector))
        return query_analysis(selector[u8"_id"s], m_primary_keys, make_primary_key);

    else if(secondary_key(selector))
        for(const auto& key : m_secondary_keys)
            if(selector.has(key.first))
                return query_analysis(selector[key.first], key.second, make_secondary_key);

    auto begin = index_iterator{m_primary_keys.begin()},
         end   = index_iterator{m_primary_keys.end()};
    return {begin,end};
}

void db::index::update(object& document)
{
    if(document.has(u8"_id"s))
        m_sequence = std::max<sequence_type>(m_sequence, document[u8"_id"s]);
    else
        document[u8"_id"s] = ++m_sequence;
}

void db::index::insert(object& document, position_type position)
{
    const auto pk = make_primary_key(document[u8"_id"s]);
    m_primary_keys[pk] = position;

    for(auto& key : m_secondary_keys)
        if(document.has(key.first))
        {
            const auto sk = make_secondary_key(document[key.first]);
            key.second[sk] = position;
        }
}

void db::index::erase(const object& document)
{
    const auto pk = make_primary_key(document[u8"_id"s]);
    m_primary_keys.erase(pk);

    for(auto& key : m_secondary_keys)
        if(document.has(key.first))
        {
            const auto sk = make_secondary_key(document[key.first]);
            key.second.erase(sk);
        }
}
