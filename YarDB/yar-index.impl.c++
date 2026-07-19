module yar;
import std;
import xson;

namespace {

auto make_primary_key = [](const xson::primitive& v){return std::get<xson::integer_type>(v);};

auto make_secondary_key = [](const xson::primitive& v){return v;};

// Shared $gt/$gte/$lt/$lte/$eq (and bare equality) bound resolution for primary
// and secondary flat_maps. Callers apply $head/$tail and wrap iterators.
template<typename Map, typename MakeKey>
auto analyze_key_bounds(const yar::db::object& selector, const Map& keys, MakeKey make_key)
{
    auto begin = keys.cbegin();
    auto end = keys.cend();

    if(selector.has("$gt"s))
        begin = keys.upper_bound(make_key(selector["$gt"s]));
    else if(selector.has("$gte"s))
        begin = keys.lower_bound(make_key(selector["$gte"s]));

    if(selector.has("$lt"s))
        end = keys.lower_bound(make_key(selector["$lt"s]));
    else if(selector.has("$lte"s))
        end = keys.upper_bound(make_key(selector["$lte"s]));

    if(selector.has("$eq"s))
        std::tie(begin, end) = keys.equal_range(make_key(selector["$eq"s]));
    else if(selector.has_value())
        std::tie(begin, end) = keys.equal_range(make_key(selector));

    // AND-merged OData ranges can be empty/inverted (age gt 10 and age lt 5).
    // flat_map iterators are random-access; walking an inverted [begin, end)
    // via distance/advance is undefined behavior. Secondary position iterators
    // similarly must not walk key_begin past key_end.
    if(begin > end)
        begin = end;

    return std::pair{begin, end};
}

template<typename Map, typename MakeKey>
yar::db::index_view query_analysis_primary(
    const yar::db::object& selector,
    const Map& keys,
    MakeKey make_key)
{
    auto [begin, end] = analyze_key_bounds(selector, keys, make_key);

    // Primary applies $head/$tail on key iterators, and only when equality did
    // not already replace the range (same exclusive chain as before).
    if(not selector.has("$eq"s) and not selector.has_value())
    {
        if(selector.has("$head"s))
        {
            const xson::integer_type n = selector["$head"s];
            const xson::integer_type len = std::ranges::distance(begin, end);
            auto itr = begin;
            std::ranges::advance(itr, std::min(n, len));
            end = itr;
        }
        else if(selector.has("$tail"s))
        {
            const xson::integer_type n = selector["$tail"s];
            const xson::integer_type len = std::ranges::distance(begin, end);
            auto itr = end;
            std::ranges::advance(itr, -std::min(n, len));
            begin = itr;
        }
    }

    const std::size_t known_size = std::ranges::distance(begin, end);

    if(not selector.has("$desc"s))
        return {begin, end, known_size};

    return {
        std::make_reverse_iterator(end),
        std::make_reverse_iterator(begin),
        known_size};
}

yar::db::index_view query_analysis_secondary(
    const yar::db::object& selector,
    const yar::db::secondary_index_type& keys)
{
    auto [key_begin, key_end] = analyze_key_bounds(selector, keys, make_secondary_key);

    auto pos_begin = yar::db::secondary_position_iterator{key_begin, key_end};
    auto pos_end = yar::db::secondary_position_iterator{key_end, key_end};
    auto known_size = std::optional<std::size_t>{};

    if(not selector.has("$head"s) and not selector.has("$tail"s))
        known_size = std::ranges::fold_left(
            key_begin, key_end, std::size_t{0},
            [](std::size_t total, const auto& entry) { return total + entry.second.size(); });

    // Secondary $head/$tail limit positions (one doc can share a key), not keys.
    if(selector.has("$head"s))
    {
        const xson::integer_type n = selector["$head"s];
        auto limited_end = pos_begin;
        std::ranges::advance(limited_end, n, pos_end);
        pos_end = limited_end;
    }
    else if(selector.has("$tail"s))
    {
        const xson::integer_type n = selector["$tail"s];
        const xson::integer_type total = std::ranges::distance(pos_begin, pos_end);
        if(total > n)
            std::ranges::advance(pos_begin, total - n, pos_end);
    }

    if(not selector.has("$desc"s))
        return {pos_begin, pos_end, known_size};

    return {
        yar::db::reverse_secondary_position_iterator{
            yar::db::secondary_index_type::const_reverse_iterator{key_end},
            yar::db::secondary_index_type::const_reverse_iterator{key_begin}},
        yar::db::reverse_secondary_position_iterator{
            yar::db::secondary_index_type::const_reverse_iterator{key_begin},
            yar::db::secondary_index_type::const_reverse_iterator{key_begin}},
        known_size};
}

void remove_position(yar::db::positions_type& positions, yar::db::position_type position)
{
    const auto it = std::ranges::find(positions, position);
    if(it != positions.end())
        positions.erase(it);
}

bool is_pagination_key(const std::string& key)
{
    return key == "$top"s or key == "$skip"s or key == "$desc"s or key == "$orderby"s;
}

auto selector_count_keys(const yar::db::object& selector)
{
    if(not selector.has_objects())
        return std::vector<std::string>{};

    return selector.get<yar::db::object::map>()
        | std::views::transform([](const auto& entry) { return entry.first; })
        | std::views::filter([](const std::string& key) { return not is_pagination_key(key); })
        | std::ranges::to<std::vector<std::string>>();
}

// Secondary indexes store primitives only (see insert()). Nested document
// selectors such as Customer/Country → {Customer:{Country:...}} use ordinary
// field names under the indexed parent; those cannot be probed as secondary
// keys and must fall back to a primary scan + match().
bool secondary_field_selector_usable(const yar::db::object& field_selector)
{
    if(field_selector.has_value())
        return true;

    if(not field_selector.has_objects())
        return false;

    // Operator maps ($eq/$gt/$in/...) are leaf constraints the secondary
    // analysis understands (or safely over-scans then match()-filters).
    // Any non-$ key is a nested document path.
    return std::ranges::all_of(
        field_selector.get<yar::db::object::map>(),
        [](const auto& entry)
        {
            return not entry.first.empty() and entry.first.front() == '$';
        });
}

// True when index.view()'s range analysis can represent the field selector
// exactly (view size == match count). Multi-op maps from AND-merged $filter
// are not always safe: query_analysis_* lets $eq overwrite prior bounds, and
// $gt/$gte (or $lt/$lte) are if/else-if so only one bound per side is applied.
bool index_only_field_selector(const yar::db::object& field_selector)
{
    if(field_selector.has_value())
        return true;

    if(not field_selector.has_objects())
        return false;

    auto has_eq = false;
    auto has_gt = false;
    auto has_gte = false;
    auto has_lt = false;
    auto has_lte = false;

    for(const auto& [op, _] : field_selector.get<yar::db::object::map>())
    {
        if(op == "$eq"s)
            has_eq = true;
        else if(op == "$gt"s)
            has_gt = true;
        else if(op == "$gte"s)
            has_gte = true;
        else if(op == "$lt"s)
            has_lt = true;
        else if(op == "$lte"s)
            has_lte = true;
        else
            return false;
    }

    // $eq replaces the whole range in query_analysis_*; only safe alone.
    if(has_eq)
        return not (has_gt or has_gte or has_lt or has_lte);

    // One exclusive and one inclusive bound on the same side cannot both apply.
    if(has_gt and has_gte)
        return false;
    if(has_lt and has_lte)
        return false;

    return has_gt or has_gte or has_lt or has_lte;
}

std::optional<std::size_t> try_index_only_count(
    const yar::db::index& index,
    const yar::db::object& selector)
{
    const auto keys = selector_count_keys(selector);
    if(keys.empty())
        return std::ranges::distance(index.view(selector));

    if(keys.size() != 1)
        return std::nullopt;

    if(keys[0] == "_id"s)
    {
        if(not index_only_field_selector(selector["_id"s]))
            return std::nullopt;
        return std::ranges::distance(index.view(selector));
    }

    if(not index.secondary_key(selector) or not index_only_field_selector(selector[keys[0]]))
        return std::nullopt;

    return std::ranges::distance(index.view(selector));
}

std::size_t count_by_scan(
    std::istream& storage,
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

void yar::db::index::add(std::span<const std::string> keys)
{
    std::ranges::for_each(keys, [this](const std::string& key) { add(key); });
}

std::span<const std::string> yar::db::index::keys() const
{
    return m_secondary_keys.keys();
}

bool yar::db::index::primary_key(const yar::db::object& selector) const
{
    return selector.has("_id"s);
}

bool yar::db::index::secondary_key(const yar::db::object& selector) const
{
    return std::ranges::any_of(
        m_secondary_keys.keys(),
        [&](const std::string& field_name) { return selector.has(field_name); });
}

std::size_t yar::db::index::count(std::istream& storage, const yar::db::object& selector) const
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
            {
                const auto& field_selector = selector[entry.first];
                if(secondary_field_selector_usable(field_selector))
                    return query_analysis_secondary(field_selector, entry.second);
                break;
            }

    if(not selector.has("$desc"s))
        return {std::ranges::cbegin(m_primary_keys), std::ranges::cend(m_primary_keys)};

    return {
        std::ranges::crbegin(m_primary_keys),
        std::ranges::crend(m_primary_keys)};
}

bool yar::db::index::update(yar::db::object& document)
{
    if(document.has("_id"s))
    {
        m_sequence = std::max<yar::db::sequence_type>(m_sequence, document["_id"s]);
        return true;
    }

    // Client-/import-supplied _id of INT64_MAX raises m_sequence to the limit.
    // A further auto-id create must not execute ++m_sequence (signed overflow).
    if(m_sequence == std::numeric_limits<yar::db::sequence_type>::max())
        return false;

    document["_id"s] = ++m_sequence;
    return true;
}

bool yar::db::index::contains_id(yar::db::primary_key_type id) const
{
    return m_primary_keys.contains(id);
}

std::optional<yar::db::position_type> yar::db::index::position(yar::db::primary_key_type id) const
{
    const auto it = m_primary_keys.find(id);
    if(it == m_primary_keys.end())
        return std::nullopt;
    return it->second;
}

void yar::db::index::insert(yar::db::object& document, yar::db::position_type position)
{
    const auto pk = make_primary_key(document["_id"s]);
    m_primary_keys[pk] = position;

    for(const auto& field_name : m_secondary_keys.keys())
    {
        if(not document.has(field_name))
            continue;

        // Secondary keys are primitives only. Object/array fields are skipped so
        // create/reindex/restart cannot throw bad_variant_access via make_secondary_key.
        const auto& value = document[field_name];
        if(not value.has_value())
            continue;

        const auto sk = make_secondary_key(value);
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

        const auto& value = document[field_name];
        if(not value.has_value())
            continue;

        const auto sk = make_secondary_key(value);
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