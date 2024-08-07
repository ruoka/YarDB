module yar;
import std;
import xson;

namespace {

auto make_primary_key   = [](const xson::object::value& v){return std::get<xson::integer_type>(v);};

auto make_secondary_key = [](const xson::object::value& v){return xson::to_string(v);};

template <typename T, typename F>
db::index_view query_analysis(const db::object& selector, const T& keys, F make_key)
{
    auto begin = std::ranges::cbegin(keys),
         end   = std::ranges::cend(keys);

    if(selector.has("$gt"s))
    {
        const auto key = make_key(selector["$gt"s]);
        begin = keys.upper_bound(key);
    }
    else if(selector.has("$gte"s))
    {
        const auto key = make_key(selector["$gte"s]);
        begin = keys.lower_bound(key);
    }

    if(selector.has("$lt"s))
    {
        const auto key = make_key(selector["$lt"s]);
        end = keys.lower_bound(key);
    }
    else if(selector.has("$lte"s))
    {
        const auto key = make_key(selector["$lte"s]);
        end = keys.upper_bound(key);
    }

    if(selector.has("$eq"s))
    {
        const auto key = make_key(selector["$eq"s]);
        std::tie(begin,end) = keys.equal_range(key);
    }
    else if(selector.has("$head"s))
    {
        const xson::integer_type n = selector["$head"s];
        auto itr = std::ranges::begin(keys);
        std::ranges::advance(itr, std::min<xson::integer_type>(n, keys.size()));
        end = itr;
    }
    else if(selector.has("$tail"s))
    {
        const xson::integer_type n = selector["$tail"s];
        auto itr = std::ranges::rbegin(keys);
        std::ranges::advance(itr, std::min<xson::integer_type>(n, keys.size()));
        begin = itr.base();
    }
    else // if(std::holds_alternative<typename T::mapped_type>((db::object::value)selector)) // FIXME
    {
        const auto key = make_key(selector);
        std::tie(begin,end) = keys.equal_range(key);
    }

    if(!selector.has("$desc"s))
        return {begin, end};
    else
        return {std::make_reverse_iterator(end), std::make_reverse_iterator(begin)};
}

} // namespace

void db::index::add(const std::string& key)
{
    if(not m_secondary_keys.contains(key))
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
    for (const auto& [name,key] : m_secondary_keys)
        result.push_back(name);
    return result;
}

bool db::index::primary_key(const object& selector) const
{
    return selector.has("_id"s);
}

bool db::index::secondary_key(const object& selector) const
{
    for(const auto& [name,key] : m_secondary_keys)
        if(selector.has(name))
            return true;
    return false;
}

db::index_view db::index::view(const object& selector) const
{
    if(primary_key(selector))
        return query_analysis(selector["_id"s], m_primary_keys, make_primary_key);

    else if(secondary_key(selector))
        for(const auto& [name,key] : m_secondary_keys)
            if(selector.has(name))
                return query_analysis(selector[name], key, make_secondary_key);

    // else

    if(!selector.has("$desc"s))
        return {std::ranges::cbegin(m_primary_keys),std::ranges::cend(m_primary_keys)};
    else
        return {std::ranges::crbegin(m_primary_keys),std::ranges::crend(m_primary_keys)};
}

void db::index::update(object& document)
{
    if(document.has("_id"s))
        m_sequence = std::max<sequence_type>(m_sequence, document["_id"s]);
    else
        document["_id"s] = ++m_sequence;
}

void db::index::insert(object& document, position_type position)
{
    const auto pk = make_primary_key(document["_id"s]);
    m_primary_keys[pk] = position;

    for(auto& [name,key] : m_secondary_keys)
        if(document.has(name))
        {
            const auto sk = make_secondary_key(document[name]);
            key[sk] = position;
        }
}

void db::index::erase(const object& document)
{
    const auto pk = make_primary_key(document["_id"s]);
    m_primary_keys.erase(pk);

    for(auto& [name,key] : m_secondary_keys)
        if(document.has(name))
        {
            const auto sk = make_secondary_key(document[name]);
            key.erase(sk);
        }
}
