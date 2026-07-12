module yar;
import std;
import xson;

namespace {

auto make_primary_key = [](const xson::primitive& v){return std::get<xson::integer_type>(v);};

auto make_secondary_key = [](const xson::primitive& v){return xson::to_string(v);};

template<typename T, typename F>
yar::db::index_view query_analysis_primary(const yar::db::object& selector, const T& keys, F make_key)
{
    auto begin = keys.cbegin(),
         end   = keys.cend();

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
        std::tie(begin, end) = keys.equal_range(key);
    }
    else if(selector.has("$head"s))
    {
        const xson::integer_type n = selector["$head"s];
        auto itr = begin;
        std::ranges::advance(itr, std::min<xson::integer_type>(n, static_cast<xson::integer_type>(std::ranges::distance(begin, end))));
        end = itr;
    }
    else if(selector.has("$tail"s))
    {
        const xson::integer_type n = selector["$tail"s];
        auto itr = end;
        std::ranges::advance(itr, -std::min<xson::integer_type>(n, static_cast<xson::integer_type>(std::ranges::distance(begin, end))));
        begin = itr;
    }
    else if(selector.has_value())
    {
        const auto key = make_key(selector);
        std::tie(begin, end) = keys.equal_range(key);
    }

    if(!selector.has("$desc"s))
        return {begin, end};

    return {
        std::make_reverse_iterator(end),
        std::make_reverse_iterator(begin)};
}

auto count_positions(
    const yar::db::secondary_position_iterator& begin,
    const yar::db::secondary_position_iterator& end)
{
    auto count = std::size_t{0};
    for(auto it = begin; it != end; ++it)
        ++count;
    return count;
}

void advance_positions(
    yar::db::secondary_position_iterator& it,
    const yar::db::secondary_position_iterator& end,
    xson::integer_type steps)
{
    while(steps > 0 && it != end)
    {
        ++it;
        --steps;
    }
}

yar::db::index_view query_analysis_secondary(
    const yar::db::object& selector,
    const yar::db::secondary_index_type& keys)
{
    auto key_begin = keys.cbegin(),
         key_end   = keys.cend();

    if(selector.has("$gt"s))
    {
        const auto key = make_secondary_key(selector["$gt"s]);
        key_begin = keys.upper_bound(key);
    }
    else if(selector.has("$gte"s))
    {
        const auto key = make_secondary_key(selector["$gte"s]);
        key_begin = keys.lower_bound(key);
    }

    if(selector.has("$lt"s))
    {
        const auto key = make_secondary_key(selector["$lt"s]);
        key_end = keys.lower_bound(key);
    }
    else if(selector.has("$lte"s))
    {
        const auto key = make_secondary_key(selector["$lte"s]);
        key_end = keys.upper_bound(key);
    }

    if(selector.has("$eq"s))
    {
        const auto key = make_secondary_key(selector["$eq"s]);
        std::tie(key_begin, key_end) = keys.equal_range(key);
    }
    else if(selector.has_value())
    {
        const auto key = make_secondary_key(selector);
        std::tie(key_begin, key_end) = keys.equal_range(key);
    }

    auto pos_begin = yar::db::secondary_position_iterator{key_begin, key_end};
    auto pos_end = yar::db::secondary_position_iterator{key_end, key_end};

    if(selector.has("$head"s))
    {
        const xson::integer_type n = selector["$head"s];
        auto limited_end = pos_begin;
        advance_positions(limited_end, pos_end, n);
        pos_end = limited_end;
    }
    else if(selector.has("$tail"s))
    {
        const xson::integer_type n = selector["$tail"s];
        const auto total = static_cast<xson::integer_type>(count_positions(pos_begin, pos_end));
        if(total > n)
            advance_positions(pos_begin, pos_end, total - n);
    }

    if(!selector.has("$desc"s))
        return {pos_begin, pos_end};

    return {
        yar::db::reverse_secondary_position_iterator{
            yar::db::secondary_index_type::const_reverse_iterator{key_end},
            yar::db::secondary_index_type::const_reverse_iterator{key_begin}},
        yar::db::reverse_secondary_position_iterator{
            yar::db::secondary_index_type::const_reverse_iterator{key_begin},
            yar::db::secondary_index_type::const_reverse_iterator{key_begin}}};
}

void remove_position(yar::db::positions_type& positions, yar::db::position_type position)
{
    const auto it = std::ranges::find(positions, position);
    if(it != positions.end())
        positions.erase(it);
}

bool is_pagination_key(const std::string& key)
{
    return key == "$top"s || key == "$skip"s || key == "$desc"s;
}

auto selector_count_keys(const yar::db::object& selector)
{
    auto keys = std::vector<std::string>{};
    if(not selector.has_objects())
        return keys;

    for(const auto& entry : selector.get<yar::db::object::map>())
    {
        if(not is_pagination_key(entry.first))
            keys.push_back(entry.first);
    }
    return keys;
}

bool index_only_field_selector(const yar::db::object& field_selector)
{
    if(field_selector.has_value())
        return true;

    if(not field_selector.has_objects())
        return false;

    for(const auto& entry : field_selector.get<yar::db::object::map>())
    {
        const auto& op = entry.first;
        if(op != "$eq"s && op != "$gt"s && op != "$gte"s && op != "$lt"s && op != "$lte"s)
            return false;
    }

    return true;
}

std::size_t count_index_view(const yar::db::index_view& view)
{
    auto count = std::size_t{0};
    for(auto it = view.begin(); it != view.end(); ++it)
        ++count;
    return count;
}

std::optional<std::size_t> try_index_only_count(
    const yar::db::index& index,
    const yar::db::object& selector)
{
    const auto keys = selector_count_keys(selector);
    if(keys.empty())
        return count_index_view(index.view(selector));

    if(keys.size() != 1)
        return std::nullopt;

    if(keys[0] == "_id"s)
    {
        if(not index_only_field_selector(selector["_id"s]))
            return std::nullopt;
        return count_index_view(index.view(selector));
    }

    if(not index.secondary_key(selector) || not index_only_field_selector(selector[keys[0]]))
        return std::nullopt;

    return count_index_view(index.view(selector));
}

std::size_t count_by_scan(
    std::fstream& storage,
    const yar::db::index& index,
    const yar::db::object& selector)
{
    using xson::fson::operator >>;

    auto count = std::size_t{0};
    for(const auto position : index.view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        storage.clear();
        storage.seekg(position, storage.beg);
        storage >> metadata >> document;
        if(document.match(selector))
            ++count;
    }
    return count;
}

} // namespace

void yar::db::index::add(const std::string& key)
{
    if(not m_secondary_keys.contains(key))
        m_secondary_keys.emplace(key, yar::db::secondary_index_type{});
}

void yar::db::index::add(std::vector<std::string> keys)
{
    for(const std::string& key : keys)
        add(key);
}

std::vector<std::string> yar::db::index::keys() const
{
    auto result = std::vector<std::string>{};
    result.reserve(m_secondary_keys.size());
    for(const auto& field_name : m_secondary_keys.keys())
        result.push_back(field_name);
    return result;
}

bool yar::db::index::primary_key(const yar::db::object& selector) const
{
    return selector.has("_id"s);
}

bool yar::db::index::secondary_key(const yar::db::object& selector) const
{
    for(const auto& field_name : m_secondary_keys.keys())
        if(selector.has(field_name))
            return true;
    return false;
}

std::size_t yar::db::index::count(std::fstream& storage, const yar::db::object& selector) const
{
    if(const auto fast = try_index_only_count(*this, selector))
        return *fast;

    return count_by_scan(storage, *this, selector);
}

yar::db::index_view yar::db::index::view(const yar::db::object& selector) const
{
    if(primary_key(selector))
        return query_analysis_primary(selector["_id"s], m_primary_keys, make_primary_key);

    if(secondary_key(selector))
        for(const auto& entry : m_secondary_keys)
            if(selector.has(entry.first))
                return query_analysis_secondary(selector[entry.first], entry.second);

    if(!selector.has("$desc"s))
        return {std::ranges::cbegin(m_primary_keys), std::ranges::cend(m_primary_keys)};

    return {
        std::ranges::crbegin(m_primary_keys),
        std::ranges::crend(m_primary_keys)};
}

void yar::db::index::update(yar::db::object& document)
{
    if(document.has("_id"s))
        m_sequence = std::max<yar::db::sequence_type>(m_sequence, document["_id"s]);
    else
        document["_id"s] = ++m_sequence;
}

void yar::db::index::insert(yar::db::object& document, yar::db::position_type position)
{
    const auto pk = make_primary_key(document["_id"s]);
    m_primary_keys[pk] = position;

    for(const auto& field_name : m_secondary_keys.keys())
    {
        if(not document.has(field_name))
            continue;

        const auto sk = make_secondary_key(document[field_name]);
        auto& positions = m_secondary_keys[field_name][sk];
        if(std::ranges::find(positions, position) == positions.end())
            positions.push_back(position);
    }
}

void yar::db::index::erase(const yar::db::object& document)
{
    const auto pk = make_primary_key(document["_id"s]);
    const auto primary_it = m_primary_keys.find(pk);
    if(primary_it == m_primary_keys.end())
        return;

    const auto position = primary_it->second;
    m_primary_keys.erase(primary_it);

    for(const auto& field_name : m_secondary_keys.keys())
    {
        if(not document.has(field_name))
            continue;

        const auto sk = make_secondary_key(document[field_name]);
        auto& secondary = m_secondary_keys[field_name];
        const auto secondary_it = secondary.find(sk);
        if(secondary_it == secondary.end())
            continue;

        auto& positions = secondary[sk];
        remove_position(positions, position);
        if(positions.empty())
            secondary.erase(sk);
    }
}