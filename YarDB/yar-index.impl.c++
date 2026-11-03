// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

module yar;
import std;
import xson;

namespace {

auto make_primary_key = [](const xson::primitive& v){return std::get<xson::integer_type>(v);};

auto make_secondary_key = [](const xson::primitive& v){return v;};

// True when [begin, end) is inverted. Map iterators are bidirectional, so this
// compares keys instead of walking begin toward end or past it.
template<typename Map>
bool bounds_inverted(
    const Map& keys,
    typename Map::const_iterator begin,
    typename Map::const_iterator end)
{
    if(begin == end or end == keys.cend())
        return false;
    if(begin == keys.cend())
        return true;
    return keys.key_comp()(end->first, begin->first);
}

// Shared $gt/$gte/$lt/$lte/$eq (and bare equality) bound resolution for primary
// and secondary maps. Callers apply $head/$tail and wrap iterators.
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
    // Do not walk key_begin past key_end: secondary position iterators increment
    // until the end iterator, which is undefined if the range is inverted.
    if(bounds_inverted(keys, begin, end))
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

    // The whole primary map is keys.size(). Distance walks only a real subrange
    // ($head, $tail, or a bound that is not the full map).
    const auto span_count = [&](auto first, auto last)
    {
        if(first == keys.cbegin() and last == keys.cend())
            return static_cast<xson::integer_type>(keys.size());
        return static_cast<xson::integer_type>(std::ranges::distance(first, last));
    };

    // Primary applies $head/$tail on key iterators, and only when equality did
    // not already replace the range (same exclusive chain as before).
    if(not selector.has("$eq"s) and not selector.has_value())
    {
        if(selector.has("$head"s))
        {
            const xson::integer_type n = selector["$head"s];
            const xson::integer_type len = span_count(begin, end);
            auto itr = begin;
            std::ranges::advance(itr, std::min(n, len));
            end = itr;
        }
        else if(selector.has("$tail"s))
        {
            const xson::integer_type n = selector["$tail"s];
            const xson::integer_type len = span_count(begin, end);
            auto itr = end;
            std::ranges::advance(itr, -std::min(n, len));
            begin = itr;
        }
    }

    const std::size_t known_size = static_cast<std::size_t>(span_count(begin, end));

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

// A key name "service+type+status" is the compound of those fields. It is not a
// document field. The posting list is one entry per distinct tuple, so a count
// of equality on exactly those fields is the vector's size.
std::optional<std::vector<std::string>> compound_parts(std::string_view name)
{
    if(not name.contains('+'))
        return std::nullopt;

    auto parts = std::vector<std::string>{};
    auto start = std::size_t{0};
    while(start <= name.size())
    {
        const auto plus = name.find('+', start);
        const auto end = plus == std::string_view::npos ? name.size() : plus;
        if(end == start)
            return std::nullopt;
        parts.emplace_back(name.substr(start, end - start));
        if(plus == std::string_view::npos)
            break;
        start = plus + 1;
    }
    if(parts.size() < 2)
        return std::nullopt;
    return parts;
}

// Integers and whole doubles share one encoding, matching primitive_equal on a
// single-field key. Each piece is self-delimiting so the concatenation is one key.
std::string encode_primitive(const xson::primitive& value)
{
    if(std::holds_alternative<std::monostate>(value))
        return "n";
    if(std::holds_alternative<xson::boolean_type>(value))
        return std::get<xson::boolean_type>(value) ? "b1" : "b0";
    if(std::holds_alternative<xson::string_type>(value))
    {
        const auto& text = std::get<xson::string_type>(value);
        return std::format("s{}:{}", text.size(), text);
    }
    if(std::holds_alternative<xson::timestamp_type>(value))
    {
        const auto ticks = std::get<xson::timestamp_type>(value).time_since_epoch().count();
        return std::format("t{}", ticks);
    }
    if(xson::is_numeric_primitive(value))
    {
        const auto number = xson::numeric_primitive_value(value);
        const auto lowest = static_cast<xson::number_type>(std::numeric_limits<xson::integer_type>::min());
        const auto highest = static_cast<xson::number_type>(std::numeric_limits<xson::integer_type>::max());
        if(std::isfinite(number) and number >= lowest and number <= highest and number == std::trunc(number))
            return std::format("i{}", static_cast<xson::integer_type>(number));
        return std::format("f{:.17g}", number);
    }
    return "x";
}

std::optional<std::string> encode_compound(
    const yar::db::object& document,
    std::span<const std::string> parts)
{
    auto encoded = std::string{};
    for(const auto& part : parts)
    {
        if(not document.has(part))
            return std::nullopt;
        const auto& value = document[part];
        if(not value.has_value())
            return std::nullopt;
        encoded += encode_primitive(make_secondary_key(value));
    }
    return encoded;
}

// A bare value or a selector that is only {$eq: value}. A range is not equality.
std::optional<xson::primitive> equality_primitive(const yar::db::object& field)
{
    if(field.has_value())
        return make_secondary_key(field);
    if(not field.has_objects())
        return std::nullopt;
    if(field.get<yar::db::object::map>().size() != 1 or not field.has("$eq"s))
        return std::nullopt;
    const auto& eq = field["$eq"s];
    if(not eq.has_value())
        return std::nullopt;
    return make_secondary_key(eq);
}

std::optional<xson::primitive> indexed_value(const yar::db::object& document, const std::string& field_name)
{
    if(const auto parts = compound_parts(field_name))
    {
        const auto encoded = encode_compound(document, *parts);
        if(not encoded)
            return std::nullopt;
        return xson::primitive{*encoded};
    }

    if(not document.has(field_name))
        return std::nullopt;
    const auto& value = document[field_name];
    if(not value.has_value())
        return std::nullopt;
    return make_secondary_key(value);
}

std::size_t posting_span(
    const yar::db::object& field_selector,
    const yar::db::secondary_index_type& keys)
{
    const auto [begin, end] = analyze_key_bounds(field_selector, keys, make_secondary_key);
    return std::ranges::fold_left(
        begin, end, std::size_t{0},
        [](std::size_t total, const auto& entry) { return total + entry.second.size(); });
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

void yar::db::index::add(const std::string& key, yar::db::index_undo* undo)
{
    if(m_secondary_keys.contains(key))
        return;

    m_secondary_keys.emplace(key, yar::db::secondary_index_type{});
    const auto at = std::ranges::lower_bound(m_key_names, key);
    m_key_names.insert(at, key);
    if(undo != nullptr)
        undo->note_secondary_map(key);
}

void yar::db::index::add(std::span<const std::string> keys, yar::db::index_undo* undo)
{
    std::ranges::for_each(keys, [this, undo](const std::string& key) { add(key, undo); });
}

void yar::db::index::add_unique(std::span<const std::string> fields, yar::db::index_undo* undo)
{
    for(const auto& field : fields)
    {
        if(not m_secondary_keys.contains(field))
            continue;
        if(std::ranges::contains(m_unique_keys, field))
            continue;
        m_unique_keys.push_back(field);
        if(undo != nullptr)
            undo->note_unique(field);
    }
}

std::span<const std::string> yar::db::index::keys() const
{
    return m_key_names;
}

std::span<const std::string> yar::db::index::unique_keys() const
{
    return m_unique_keys;
}

std::optional<yar::db::primary_key_type> yar::db::index::id_at(yar::db::position_type position) const
{
    for(const auto& [id, stored] : m_primary_keys)
        if(stored == position)
            return id;
    return std::nullopt;
}

std::optional<yar::db::unique_conflict> yar::db::index::non_primitive_unique(
    const yar::db::object& document) const
{
    for(const auto& field : m_unique_keys)
    {
        if(not document.has(field))
            continue;

        const auto& value = document[field];
        if(value.is_null() or value.has_value())
            continue;

        auto clash = yar::db::unique_conflict{};
        clash.field = field;
        clash.primitive = false;
        if(document.has("_id"s) and document["_id"s].is_integer())
            clash.id = make_primary_key(document["_id"s]);
        return clash;
    }
    return std::nullopt;
}

std::optional<yar::db::unique_conflict> yar::db::index::conflicts_with(
    const yar::db::object& document) const
{
    if(const auto rejected = non_primitive_unique(document))
        return rejected;

    for(const auto& field : m_unique_keys)
    {
        if(not document.has(field))
            continue;

        const auto& value = document[field];
        if(not value.has_value() or value.is_null())
            continue;

        const auto secondary = m_secondary_keys.find(field);
        if(secondary == m_secondary_keys.end())
            continue;

        const auto found = secondary->second.find(make_secondary_key(value));
        if(found == secondary->second.end() or found->second.empty())
            continue;

        auto clash = yar::db::unique_conflict{};
        clash.field = field;
        clash.id = id_at(found->second.front());
        return clash;
    }
    return std::nullopt;
}

std::optional<yar::db::unique_conflict> yar::db::index::duplicate_unique() const
{
    for(const auto& field : m_unique_keys)
    {
        const auto secondary = m_secondary_keys.find(field);
        if(secondary == m_secondary_keys.end())
            continue;

        for(const auto& [key, positions] : secondary->second)
        {
            if(std::holds_alternative<std::monostate>(key) or positions.size() < 2)
                continue;

            auto clash = yar::db::unique_conflict{};
            clash.field = field;
            clash.id = id_at(positions.front());
            return clash;
        }
    }
    return std::nullopt;
}

bool yar::db::index::primary_key(const yar::db::object& selector) const
{
    return selector.has("_id"s);
}

bool yar::db::index::secondary_key(const yar::db::object& selector) const
{
    return std::ranges::any_of(
        m_key_names,
        [&](const std::string& field_name) { return selector.has(field_name); });
}

std::optional<std::size_t> yar::db::index::compound_equality_count(const yar::db::object& selector) const
{
    if(not selector.has_objects())
        return std::nullopt;

    auto wanted = std::vector<std::string>{};
    for(const auto& [key, field] : selector.get<yar::db::object::map>())
    {
        if(is_pagination_key(key))
            continue;
        if(not equality_primitive(field))
            return std::nullopt;
        wanted.push_back(key);
    }
    if(wanted.size() < 2)
        return std::nullopt;

    auto sorted_wanted = wanted;
    std::ranges::sort(sorted_wanted);

    for(const auto& name : m_key_names)
    {
        const auto parts = compound_parts(name);
        if(not parts)
            continue;
        auto sorted_parts = *parts;
        std::ranges::sort(sorted_parts);
        if(sorted_parts != sorted_wanted)
            continue;

        auto encoded = std::string{};
        for(const auto& part : *parts)
        {
            const auto value = equality_primitive(selector[part]);
            if(not value)
                return std::nullopt;
            encoded += encode_primitive(*value);
        }

        const auto secondary = m_secondary_keys.find(name);
        if(secondary == m_secondary_keys.end())
            return 0;
        const auto found = secondary->second.find(encoded);
        if(found == secondary->second.end())
            return 0;
        return found->second.size();
    }
    return std::nullopt;
}

std::size_t yar::db::index::count(std::istream& storage, const yar::db::object& selector) const
{
    if(const auto fast = try_index_only_count(*this, selector))
        return *fast;

    if(const auto exact = compound_equality_count(selector))
        return *exact;

    return count_by_scan(storage, *this, selector);
}

yar::db::index_view yar::db::index::view(const yar::db::object& selector) const
{
    if(primary_key(selector))
        return query_analysis_primary(selector["_id"s], m_primary_keys, make_primary_key);

    // Among exact secondary bounds, use the one with the fewest positions.
    // $ne, $in, and any selector the bound analysis cannot represent, count as
    // the whole collection, so a tighter key wins. An equal span keeps the
    // alphabetically earlier key. A compound name is not a document field.
    if(secondary_key(selector))
    {
        const yar::db::secondary_index_type* best = nullptr;
        const yar::db::object* best_field = nullptr;
        auto best_span = std::optional<std::size_t>{};
        for(const auto& entry : m_secondary_keys)
        {
            if(entry.first.contains('+'))
                continue;
            if(not selector.has(entry.first))
                continue;
            const auto& field_selector = selector[entry.first];
            if(not secondary_field_selector_usable(field_selector))
                continue;

            const auto span = index_only_field_selector(field_selector)
                ? posting_span(field_selector, entry.second)
                : m_primary_keys.size();
            if(not best_span or span < *best_span)
            {
                best_span = span;
                best = &entry.second;
                best_field = &field_selector;
            }
        }
        if(best != nullptr)
            return query_analysis_secondary(*best_field, *best);
    }

    if(not selector.has("$desc"s))
        return {std::ranges::cbegin(m_primary_keys), std::ranges::cend(m_primary_keys), m_primary_keys.size()};

    return {
        std::ranges::crbegin(m_primary_keys),
        std::ranges::crend(m_primary_keys),
        m_primary_keys.size()};
}

void yar::db::index::update(yar::db::object& document, yar::db::index_undo* undo)
{
    const auto previous = m_sequence;
    if(document.has("_id"s))
        m_sequence = std::max<yar::db::sequence_type>(m_sequence, document["_id"s]);
    else
        document["_id"s] = ++m_sequence;
    if(undo != nullptr and m_sequence != previous)
        undo->note_sequence(previous);
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

void yar::db::index::insert(yar::db::object& document, yar::db::position_type position, yar::db::index_undo* undo)
{
    const auto pk = make_primary_key(document["_id"s]);
    if(const auto primary_it = m_primary_keys.find(pk); primary_it == m_primary_keys.end())
    {
        m_primary_keys.emplace(pk, position);
        if(undo != nullptr)
            undo->note_primary_inserted(pk);
    }
    else if(primary_it->second != position)
    {
        if(undo != nullptr)
            undo->note_primary_replaced(pk, primary_it->second);
        primary_it->second = position;
    }

    for(const auto& field_name : m_key_names)
    {
        // A compound name is built from its fields. Object and array values are
        // skipped so create/reindex/restart cannot throw bad_variant_access.
        const auto sk = indexed_value(document, field_name);
        if(not sk)
            continue;

        auto& secondary = m_secondary_keys.at(field_name);
        const auto secondary_it = secondary.find(*sk);
        if(secondary_it == secondary.end())
        {
            secondary.emplace(*sk, yar::db::positions_type{position});
            if(undo != nullptr)
                undo->note_secondary_key(field_name, *sk);
        }
        else if(std::ranges::find(secondary_it->second, position) == secondary_it->second.end())
        {
            secondary_it->second.push_back(position);
            if(undo != nullptr)
                undo->note_secondary_pushed(field_name, *sk);
        }
    }
}

void yar::db::index::erase(const yar::db::object& document, yar::db::index_undo* undo)
{
    const auto pk = make_primary_key(document["_id"s]);
    const auto primary_it = m_primary_keys.find(pk);
    if(primary_it == m_primary_keys.end())
        return;

    const auto position = primary_it->second;
    m_primary_keys.erase(primary_it);
    if(undo != nullptr)
        undo->note_primary_removed(pk, position);

    for(const auto& field_name : m_key_names)
    {
        const auto sk = indexed_value(document, field_name);
        if(not sk)
            continue;

        auto& secondary = m_secondary_keys.at(field_name);
        const auto secondary_it = secondary.find(*sk);
        if(secondary_it == secondary.end())
            continue;

        auto& positions = secondary_it->second;
        const auto pos_it = std::ranges::find(positions, position);
        if(pos_it == positions.end())
            continue;

        const auto at = static_cast<std::size_t>(pos_it - positions.begin());
        const auto erased_key = positions.size() == 1;
        positions.erase(pos_it);
        if(erased_key)
            secondary.erase(secondary_it);
        if(undo != nullptr)
            undo->note_secondary_removed(field_name, *sk, at, position, erased_key);
    }
}

void yar::db::index_undo::apply(yar::db::index& target)
{
    for(auto op = m_ops.rbegin(); op != m_ops.rend(); ++op)
    {
        if(const auto* action = std::get_if<primary_inserted>(&*op))
            target.m_primary_keys.erase(action->id);
        else if(const auto* action = std::get_if<primary_replaced>(&*op))
            target.m_primary_keys.insert_or_assign(action->id, action->position);
        else if(const auto* action = std::get_if<primary_removed>(&*op))
            target.m_primary_keys.insert_or_assign(action->id, action->position);
        else if(const auto* action = std::get_if<secondary_key_added>(&*op))
        {
            if(const auto field = target.m_secondary_keys.find(action->field); field != target.m_secondary_keys.end())
                field->second.erase(action->key);
        }
        else if(const auto* action = std::get_if<secondary_pushed>(&*op))
        {
            const auto field = target.m_secondary_keys.find(action->field);
            if(field == target.m_secondary_keys.end())
                continue;
            const auto key = field->second.find(action->key);
            if(key == field->second.end() or key->second.empty())
                continue;
            key->second.pop_back();
            if(key->second.empty())
                field->second.erase(key);
        }
        else if(const auto* action = std::get_if<secondary_removed>(&*op))
        {
            auto& secondary = target.m_secondary_keys.at(action->field);
            if(action->erased_key)
            {
                secondary.emplace(action->key, yar::db::positions_type{action->position});
                continue;
            }
            auto& positions = secondary.at(action->key);
            const auto at = std::min(action->at, positions.size());
            positions.insert(positions.begin() + static_cast<std::ptrdiff_t>(at), action->position);
        }
        else if(const auto* action = std::get_if<secondary_map_added>(&*op))
        {
            target.m_secondary_keys.erase(action->field);
            const auto name = std::ranges::find(target.m_key_names, action->field);
            if(name != target.m_key_names.end())
                target.m_key_names.erase(name);
        }
        else if(const auto* action = std::get_if<unique_added>(&*op))
        {
            const auto name = std::ranges::find(target.m_unique_keys, action->field);
            if(name != target.m_unique_keys.end())
                target.m_unique_keys.erase(name);
        }
        else if(const auto* action = std::get_if<sequence_set>(&*op))
            target.m_sequence = action->previous;
    }
    m_ops.clear();
}
