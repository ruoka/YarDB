// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

module yar;
import std;
import xson;

namespace {

auto make_primary_key = [](const xson::primitive& v){return std::get<xson::integer_type>(v);};

auto make_secondary_key = [](const xson::primitive& v){return v;};

// A term with more keys than this joins an intersection only when it drives
// the walk. Seeking it costs a search for each of its runs it passes. The same
// cap limits a pure $in membership list that becomes discrete runs.
constexpr auto max_seek_runs = std::size_t{64};

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
// and secondary maps. Callers apply $head/$tail and wrap the key span.
template<typename Map, typename MakeKey>
yar::db::key_span<Map> analyze_key_bounds(const yar::db::object& selector, const Map& keys, MakeKey make_key)
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
    // A join over an inverted span is undefined; collapse it to empty.
    if(bounds_inverted(keys, begin, end))
        begin = end;

    return {begin, end};
}

// The whole primary map is keys.size(). Distance walks only a real subrange
// ($head, $tail, or a bound that is not the full map).
std::size_t span_count(
    const yar::db::primary_index_type& keys,
    yar::db::primary_index_type::const_iterator first,
    yar::db::primary_index_type::const_iterator last)
{
    if(first == keys.cbegin() and last == keys.cend())
        return keys.size();
    return static_cast<std::size_t>(std::ranges::distance(first, last));
}

// The _id bound, then $head/$tail on key iterators, only when equality did
// not already replace the range (same exclusive chain as before).
yar::db::key_span<yar::db::primary_index_type> primary_span(
    const yar::db::object& selector,
    const yar::db::primary_index_type& keys)
{
    auto span = analyze_key_bounds(selector, keys, make_primary_key);
    if(not selector.has("$eq"s) and not selector.has_value())
    {
        if(selector.has("$head"s))
        {
            const xson::integer_type n = selector["$head"s];
            const auto len = static_cast<xson::integer_type>(span_count(keys, span.begin(), span.end()));
            auto itr = span.begin();
            std::ranges::advance(itr, std::min(n, len));
            span = {span.begin(), itr};
        }
        else if(selector.has("$tail"s))
        {
            const xson::integer_type n = selector["$tail"s];
            const auto len = static_cast<xson::integer_type>(span_count(keys, span.begin(), span.end()));
            auto itr = span.end();
            std::ranges::advance(itr, -std::min(n, len));
            span = {itr, span.end()};
        }
    }
    return span;
}

yar::db::index_view query_analysis_primary(
    const yar::db::object& selector,
    const yar::db::primary_index_type& keys,
    bool backward)
{
    const auto span = primary_span(selector, keys);
    const auto known_size = span_count(keys, span.begin(), span.end());

    if(not backward)
        return {span.begin(), span.end(), known_size};

    return {
        std::make_reverse_iterator(span.end()),
        std::make_reverse_iterator(span.begin()),
        known_size};
}

yar::db::index_view full_walk(const yar::db::primary_index_type& keys, bool backward)
{
    if(not backward)
        return {keys.cbegin(), keys.cend(), keys.size()};
    return {keys.crbegin(), keys.crend(), keys.size()};
}

yar::db::index_view query_analysis_secondary(
    const yar::db::object& selector,
    const yar::db::secondary_index_type& keys,
    bool backward)
{
    const auto span = analyze_key_bounds(selector, keys, make_secondary_key);
    auto known_size = std::optional<std::size_t>{};

    if(not selector.has("$head"s) and not selector.has("$tail"s))
        known_size = std::ranges::fold_left(
            span, std::size_t{0},
            [](std::size_t total, const auto& entry) { return total + entry.second.size(); });

    // A reverse walk keeps the full key span. $head/$tail on a reverse secondary
    // bound matched the earlier iterators, which also left the key span alone.
    if(backward)
        return {yar::db::reverse_secondary_positions(span), known_size};

    auto positions = yar::db::secondary_positions(span);
    auto drop = std::size_t{0};
    auto take = std::optional<std::size_t>{};

    // Secondary $head/$tail limit positions (one doc can share a key), not keys.
    // views::take ends in a sentinel; the owned walk stores drop/take counts.
    if(selector.has("$head"s))
        take = static_cast<std::size_t>(static_cast<xson::integer_type>(selector["$head"s]));
    else if(selector.has("$tail"s))
    {
        const auto n = static_cast<std::size_t>(static_cast<xson::integer_type>(selector["$tail"s]));
        const auto total = static_cast<std::size_t>(std::ranges::distance(positions));
        if(total > n)
            drop = total - n;
    }

    return {std::move(positions), drop, take, known_size};
}

bool is_pagination_key(const std::string& key)
{
    return key == "$top"s or key == "$skip"s or key == "$desc"s or key == "$orderby"s;
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

// True when the field is exactly {$in: array|map}. Membership values are the
// array elements or the map's values (OData stores indexed keys "0","1",…).
bool is_pure_in(const yar::db::object& field)
{
    if(not field.has_objects())
        return false;
    if(field.get<yar::db::object::map>().size() != 1 or not field.has("$in"s))
        return false;
    const auto& list = field["$in"s];
    return list.is_array() or list.has_objects();
}

std::size_t in_membership_count(const yar::db::object& field)
{
    const auto& list = field["$in"s];
    if(list.is_array())
        return list.get<yar::db::object::array>().size();
    return list.get<yar::db::object::map>().size();
}

template<typename Fn>
void for_each_in_member(const yar::db::object& field, Fn&& fn)
{
    const auto& list = field["$in"s];
    if(list.is_array())
    {
        for(const auto& value : list.get<yar::db::object::array>())
            fn(value);
        return;
    }
    for(const auto& [_, value] : list.get<yar::db::object::map>())
        fn(value);
}

// True when index.view()'s range analysis can represent the field selector
// exactly (view size == match count). Multi-op maps from AND-merged $filter
// are not always safe: query_analysis_* lets $eq overwrite prior bounds, and
// $gt/$gte (or $lt/$lte) are if/else-if so only one bound per side is applied.
// A pure $in with at most max_seek_runs members is exact via discrete runs.
bool index_only_field_selector(const yar::db::object& field_selector)
{
    if(field_selector.has_value())
        return true;

    if(not field_selector.has_objects())
        return false;

    if(is_pure_in(field_selector))
        return in_membership_count(field_selector) <= max_seek_runs;

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

using run = yar::db::posting_stream_iterator::run;

using term = yar::db::posting_stream_iterator::term;

// An ascending walk takes a run from its front, a descending one from its back.
const yar::db::posting& head(const run& postings, bool descending)
{
    return descending ? *(postings.last - 1) : *postings.first;
}

// lhs comes before rhs in the walk.
bool before(yar::db::primary_key_type lhs, yar::db::primary_key_type rhs, bool descending)
{
    return descending ? rhs < lhs : lhs < rhs;
}

// Heap order: the front is the run whose head the walk reaches first.
struct later_head
{
    bool descending;

    bool operator()(const run& lhs, const run& rhs) const
    {
        return before(head(rhs, descending).id, head(lhs, descending).id, descending);
    }
};

// Drops the postings of a run that come before target. Its head comes before
// target, so the step doubles from there and a search finishes the span.
void gallop(run& postings, yar::db::primary_key_type target, bool descending)
{
    const auto by_id = &yar::db::posting::id;
    const auto size = postings.last - postings.first;
    auto step = std::ptrdiff_t{1};
    if(not descending)
    {
        while(step < size and postings.first[step].id < target)
            step *= 2;
        const auto low = postings.first + step / 2 + 1;
        const auto high = postings.first + std::min(step, size);
        postings.first = std::ranges::lower_bound(low, high, target, {}, by_id);
        return;
    }
    while(step < size and (postings.last - 1 - step)->id > target)
        step *= 2;
    const auto low = postings.last - std::min(step, size);
    const auto high = postings.last - step / 2 - 1;
    postings.last = std::ranges::upper_bound(low, high, target, {}, by_id);
}

// Moves a term to its first posting at or after target. Only the runs whose
// head comes before target move.
void seek(term& runs, yar::db::primary_key_type target, bool descending)
{
    const auto order = later_head{descending};
    while(not runs.empty() and before(head(runs.front(), descending).id, target, descending))
    {
        std::ranges::pop_heap(runs, order);
        gallop(runs.back(), target, descending);
        if(runs.back().first == runs.back().last)
            runs.pop_back();
        else
            std::ranges::push_heap(runs, order);
    }
}

// The _id range a selector allows, [low, high), open where absent.
struct id_range
{
    std::optional<yar::db::primary_key_type> low;
    std::optional<yar::db::primary_key_type> high;
};

// nullopt when no live _id is in the bound.
std::optional<id_range> id_bounds(const yar::db::object& id_selector, const yar::db::primary_index_type& keys)
{
    const auto [begin, end] = primary_span(id_selector, keys);
    if(begin == end)
        return std::nullopt;

    auto range = id_range{};
    if(begin != keys.cbegin())
        range.low = begin->first;
    if(end != keys.cend())
        range.high = end->first;
    return range;
}

run trimmed(const yar::db::postings_type& postings, const id_range& ids)
{
    const auto by_id = &yar::db::posting::id;
    const auto* first = postings.data();
    const auto* last = first + postings.size();
    if(ids.low)
        first = std::ranges::lower_bound(first, last, *ids.low, {}, by_id);
    if(ids.high)
        last = std::ranges::lower_bound(first, last, *ids.high, {}, by_id);
    return {first, last};
}

// A field's bound: its keys, how many, and the postings they hold. A contiguous
// bound borrows a key span; a pure $in stores discrete trimmed runs (sparse
// keys). Runs are pointer pairs into existing posting vectors.
struct term_plan
{
    const yar::db::object* field = nullptr;
    const yar::db::secondary_index_type* keys = nullptr;
    yar::db::secondary_key_span span{};
    term prebuilt{};
    std::size_t key_count = 0;
    std::size_t size = 0;
    bool discrete = false;
};

term_plan term_of(const yar::db::object& field, const yar::db::secondary_index_type& keys)
{
    const auto span = analyze_key_bounds(field, keys, make_secondary_key);
    auto plan = term_plan{&field, &keys, span};
    for(const auto& [key, postings] : span)
    {
        ++plan.key_count;
        plan.size += postings.size();
    }
    return plan;
}

// One run per distinct membership value that exists in the index, trimmed by
// the current _id range. Missing values and non-primitives are omitted.
term_plan term_of_in(
    const yar::db::object& field,
    const yar::db::secondary_index_type& keys,
    const id_range& ids)
{
    auto plan = term_plan{&field, &keys};
    plan.discrete = true;
    plan.prebuilt.reserve(in_membership_count(field));
    for_each_in_member(field, [&](const yar::db::object& value)
    {
        if(not value.has_value())
            return;
        const auto found = keys.find(make_secondary_key(value));
        if(found == keys.end())
            return;
        const auto postings = trimmed(found->second, ids);
        if(postings.first == postings.last)
            return;
        // Duplicate membership values share one posting list; keep a single run.
        if(std::ranges::any_of(plan.prebuilt, [&](const run& existing)
        {
            return existing.first == postings.first;
        }))
            return;
        plan.prebuilt.push_back(postings);
        plan.size += static_cast<std::size_t>(postings.last - postings.first);
    });
    plan.key_count = plan.prebuilt.size();
    return plan;
}

term runs_of(const term_plan& plan, const id_range& ids)
{
    if(plan.discrete)
        return plan.prebuilt;

    auto runs = term{};
    runs.reserve(plan.key_count);
    for(const auto& [key, postings] : plan.span)
    {
        if(const auto trimmed_postings = trimmed(postings, ids); trimmed_postings.first != trimmed_postings.last)
            runs.push_back(trimmed_postings);
    }
    return runs;
}

yar::db::index_view stream(std::vector<term> terms, bool backward)
{
    auto known_size = std::optional<std::size_t>{};
    if(terms.size() == 1)
    {
        known_size = std::ranges::fold_left(terms.front(), std::size_t{0}, [](std::size_t total, const run& postings)
        {
            return total + static_cast<std::size_t>(postings.last - postings.first);
        });
    }
    return yar::db::index_view{
        yar::db::posting_stream_iterator{std::move(terms), backward},
        known_size};
}

yar::db::index_view empty_view()
{
    return yar::db::index_view{yar::db::posting_stream_iterator{}, std::size_t{0}};
}

// A read that pages follows _id: $top or $skip without $orderby, or $orderby
// of _id alone.
bool pages_by_id(const yar::db::object& selector)
{
    const auto keys = yar::db::orderby_keys(selector);
    if(not keys.empty())
        return keys.size() == 1 and keys.front().field == "_id"s;
    return selector.has("$top"s) or selector.has("$skip"s);
}

bool walks_backward(const yar::db::object& selector)
{
    return selector.has("$desc"s) or (selector.has("_id"s) and selector["_id"s].has("$desc"s));
}

// $head and $tail take a field's first or last postings in key order.
bool windowed(const yar::db::object& field)
{
    return field.has("$head"s) or field.has("$tail"s);
}

// Every bound is one the index answers exactly: an _id comparison span, or an
// indexed field that is not a compound name. A pure $in is exact on a secondary
// map because plan() turns it into discrete runs. The primary span is only $eq
// and $gt/$gte/$lt/$lte, so an _id $in is the whole map (or the comparison
// span beside it) and must be counted by match().
bool exact_bounds_only(const yar::db::object& selector, std::span<const std::string> indexed)
{
    if(not selector.has_objects())
        return true;

    auto names = selector.get<yar::db::object::map>()
        | std::views::keys
        | std::views::filter([](const std::string& key) { return not is_pagination_key(key); });
    return std::ranges::all_of(names, [&](const auto& key)
    {
        if(key != "_id"s and (key.contains('+') or not std::ranges::contains(indexed, key)))
            return false;
        const auto& field = selector[key];
        if(key == "_id"s and is_pure_in(field))
            return false;
        return index_only_field_selector(field);
    });
}

std::size_t count_by_scan(
    std::istream& storage,
    const yar::db::index_view& view,
    const yar::db::object& selector)
{
    using xson::fson::operator >>;

    auto count = std::size_t{0};
    for(const auto position : view)
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

yar::db::posting_stream_iterator::posting_stream_iterator(std::vector<term> terms, bool descending) :
    m_terms{std::move(terms)},
    m_descending{descending}
{
    const auto order = later_head{m_descending};
    for(auto& runs : m_terms)
    {
        std::erase_if(runs, [](const run& postings) { return postings.first == postings.last; });
        std::ranges::make_heap(runs, order);
    }
    align();
}

yar::db::posting_stream_iterator& yar::db::posting_stream_iterator::operator++()
{
    const auto order = later_head{m_descending};
    auto& runs = m_terms.front();
    std::ranges::pop_heap(runs, order);
    auto& passed = runs.back();
    if(m_descending)
        --passed.last;
    else
        ++passed.first;
    if(passed.first == passed.last)
        runs.pop_back();
    else
        std::ranges::push_heap(runs, order);
    align();
    return *this;
}

// Every term seeks the furthest head. A term that passes it names the next
// target, so the target only moves forward and the loop ends.
void yar::db::posting_stream_iterator::align()
{
    m_current = nullptr;
    while(not m_terms.empty() and std::ranges::none_of(m_terms, [](const term& runs) { return runs.empty(); }))
    {
        auto target = head(m_terms.front().front(), m_descending).id;
        for(const auto& runs : m_terms)
        {
            if(before(target, head(runs.front(), m_descending).id, m_descending))
                target = head(runs.front(), m_descending).id;
        }

        auto aligned = true;
        for(auto& runs : m_terms)
        {
            seek(runs, target, m_descending);
            if(runs.empty() or head(runs.front(), m_descending).id != target)
            {
                aligned = false;
                break;
            }
        }
        if(aligned)
        {
            m_current = &head(m_terms.front().front(), m_descending);
            return;
        }
    }
    m_terms.clear();
}

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

namespace {

std::optional<yar::db::unique_conflict> non_primitive_part(
    const std::string& field,
    const yar::db::object& value,
    const yar::db::object& document)
{
    if(value.is_null() or value.has_value())
        return std::nullopt;

    auto clash = yar::db::unique_conflict{};
    clash.field = field;
    clash.primitive = false;
    if(document.has("_id"s) and document["_id"s].is_integer())
        clash.id = make_primary_key(document["_id"s]);
    return clash;
}

} // namespace

std::optional<yar::db::unique_conflict> yar::db::index::non_primitive_unique(
    const yar::db::object& document) const
{
    for(const auto& field : m_unique_keys)
    {
        // "service+type" is not a document field. An object or array in any
        // part would skip the posting list and slip past the constraint.
        if(const auto parts = compound_parts(field))
        {
            for(const auto& part : *parts)
            {
                if(not document.has(part))
                    continue;
                if(const auto clash = non_primitive_part(field, document[part], document))
                    return clash;
            }
            continue;
        }

        if(not document.has(field))
            continue;
        if(const auto clash = non_primitive_part(field, document[field], document))
            return clash;
    }
    return std::nullopt;
}

std::optional<yar::db::unique_conflict> yar::db::index::conflicts_with(
    const yar::db::object& document) const
{
    if(const auto rejected = non_primitive_unique(document))
        return rejected;

    auto own = std::optional<yar::db::primary_key_type>{};
    if(document.has("_id"s) and document["_id"s].is_integer())
        own = make_primary_key(document["_id"s]);

    for(const auto& field : m_unique_keys)
    {
        const auto secondary = m_secondary_keys.find(field);
        if(secondary == m_secondary_keys.end())
            continue;

        auto key = std::optional<yar::db::secondary_key_type>{};
        if(const auto parts = compound_parts(field))
        {
            // Missing or null parts are not a tuple, matching a single field.
            const auto absent = std::ranges::any_of(*parts, [&](const std::string& part)
            {
                return not document.has(part) or document[part].is_null();
            });
            if(absent)
                continue;
            key = indexed_value(document, field);
        }
        else
        {
            if(not document.has(field))
                continue;

            const auto& value = document[field];
            if(not value.has_value() or value.is_null())
                continue;
            key = make_secondary_key(value);
        }
        if(not key)
            continue;

        const auto found = secondary->second.find(*key);
        if(found == secondary->second.end())
            continue;

        const auto other = [&](const yar::db::posting& entry) { return entry.id != own; };
        const auto holder = std::ranges::find_if(found->second, other);
        if(holder == found->second.end())
            continue;

        auto clash = yar::db::unique_conflict{};
        clash.field = field;
        clash.id = holder->id;
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

        for(const auto& [key, postings] : secondary->second)
        {
            if(std::holds_alternative<std::monostate>(key) or postings.size() < 2)
                continue;

            auto clash = yar::db::unique_conflict{};
            clash.field = field;
            clash.id = postings.front().id;
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
    if(const auto exact = compound_equality_count(selector))
        return *exact;

    const auto planned = plan(selector);
    if(planned.complete and exact_bounds_only(selector, m_key_names))
        return static_cast<std::size_t>(std::ranges::distance(planned.view));

    return count_by_scan(storage, planned.view, selector);
}

yar::db::index_view yar::db::index::view(const yar::db::object& selector) const
{
    return plan(selector).view;
}

yar::db::index::planned_view yar::db::index::plan(const yar::db::object& selector) const
{
    const auto backward = walks_backward(selector);
    const auto paged = pages_by_id(selector);
    const auto bounded = primary_key(selector);

    auto ids = id_range{};
    if(bounded)
    {
        const auto range = id_bounds(selector["_id"s], m_primary_keys);
        if(not range)
            return {empty_view(), true};
        ids = *range;
    }

    // $ne and any selector the bound analysis cannot represent narrow only a
    // walk that no exact bound narrows. A pure $in becomes discrete runs when
    // small enough. A compound name is not a document field.
    auto exact = std::vector<term_plan>{};
    auto loose = std::optional<term_plan>{};
    for(const auto& [name, keys] : m_secondary_keys)
    {
        if(name.contains('+') or not selector.has(name))
            continue;
        const auto& field = selector[name];
        if(not secondary_field_selector_usable(field))
            continue;
        if(is_pure_in(field))
        {
            if(in_membership_count(field) <= max_seek_runs)
            {
                exact.push_back(term_of_in(field, keys, ids));
                continue;
            }
            if(not loose)
                loose = term_of(field, keys);
            continue;
        }
        if(index_only_field_selector(field))
            exact.push_back(term_of(field, keys));
        else if(not loose)
            loose = term_of(field, keys);
    }

    if(exact.empty())
    {
        if(loose and not bounded and (not paged or windowed(*loose->field)))
        {
            const auto reverse = backward or loose->field->has("$desc"s);
            return {query_analysis_secondary(*loose->field, *loose->keys, reverse), true};
        }
        if(loose and loose->key_count <= max_seek_runs)
            return {stream({runs_of(*loose, ids)}, backward), true};
        if(bounded)
            return {query_analysis_primary(selector["_id"s], m_primary_keys, backward), true};
        return {full_walk(m_primary_keys, backward), true};
    }

    // The bound with the fewest postings drives the walk, and an equal size
    // keeps the alphabetically earlier key. A bound with none matches nothing.
    std::ranges::stable_sort(exact, {}, &term_plan::size);
    if(exact.front().size == 0)
        return {empty_view(), true};

    auto complete = true;
    auto kept = std::vector<term_plan>{};
    for(const auto& candidate : exact)
    {
        if(kept.empty() or candidate.key_count <= max_seek_runs)
            kept.push_back(candidate);
        else
            complete = false;
    }

    // A lone contiguous bound that neither _id nor paging orders walks its
    // keys in order. A sparse $in has no contiguous span; stream its runs.
    if(kept.size() == 1 and not bounded and not paged and not kept.front().discrete)
        return {query_analysis_secondary(*kept.front().field, *kept.front().keys, backward), complete};

    auto terms = std::vector<term>{};
    terms.reserve(kept.size());
    for(const auto& candidate : kept)
    {
        auto runs = runs_of(candidate, ids);
        if(runs.empty())
            return {empty_view(), true};
        terms.push_back(std::move(runs));
    }
    return {stream(std::move(terms), backward), complete};
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

void yar::db::index::place_primary(
    yar::db::primary_key_type id,
    yar::db::position_type position,
    yar::db::index_undo* undo)
{
    if(const auto primary_it = m_primary_keys.find(id); primary_it == m_primary_keys.end())
    {
        m_primary_keys.emplace(id, position);
        if(undo != nullptr)
            undo->note_primary_inserted(id);
    }
    else if(primary_it->second != position)
    {
        if(undo != nullptr)
            undo->note_primary_replaced(id, primary_it->second);
        primary_it->second = position;
    }
}

void yar::db::index::add_posting(
    const std::string& field,
    const yar::db::secondary_key_type& key,
    yar::db::primary_key_type id,
    yar::db::position_type position,
    yar::db::index_undo* undo)
{
    auto& secondary = m_secondary_keys.at(field);
    const auto found = secondary.find(key);
    if(found == secondary.end())
    {
        secondary.emplace(key, yar::db::postings_type{yar::db::posting{id, position}});
        if(undo != nullptr)
            undo->note_secondary_key(field, key);
        return;
    }

    // A new document has the largest _id, so its posting is an append.
    auto& postings = found->second;
    const auto at = postings.empty() or postings.back().id < id
        ? postings.end()
        : std::ranges::lower_bound(postings, id, {}, &yar::db::posting::id);
    if(at != postings.end() and at->id == id)
    {
        if(at->position == position)
            return;
        if(undo != nullptr)
            undo->note_secondary_moved(field, key, id, at->position);
        at->position = position;
        return;
    }

    postings.insert(at, yar::db::posting{id, position});
    if(undo != nullptr)
        undo->note_secondary_inserted(field, key, id);
}

void yar::db::index::remove_posting(
    const std::string& field,
    const yar::db::secondary_key_type& key,
    yar::db::primary_key_type id,
    yar::db::index_undo* undo)
{
    auto& secondary = m_secondary_keys.at(field);
    const auto found = secondary.find(key);
    if(found == secondary.end())
        return;

    auto& postings = found->second;
    const auto at = std::ranges::lower_bound(postings, id, {}, &yar::db::posting::id);
    if(at == postings.end() or at->id != id)
        return;

    const auto removed = *at;
    const auto erased_key = postings.size() == 1;
    postings.erase(at);
    if(erased_key)
        secondary.erase(found);
    if(undo != nullptr)
        undo->note_secondary_removed(field, key, removed, erased_key);
}

void yar::db::index::insert(yar::db::object& document, yar::db::position_type position, yar::db::index_undo* undo)
{
    const auto pk = make_primary_key(document["_id"s]);
    place_primary(pk, position, undo);

    for(const auto& field_name : m_key_names)
    {
        // A compound name is built from its fields. Object and array values are
        // skipped so create/reindex/restart cannot throw bad_variant_access.
        if(const auto sk = indexed_value(document, field_name))
            add_posting(field_name, *sk, pk, position, undo);
    }
}

void yar::db::index::replace(
    const yar::db::object& prior,
    yar::db::object& document,
    yar::db::position_type position,
    yar::db::index_undo* undo)
{
    const auto pk = make_primary_key(document["_id"s]);
    if(make_primary_key(prior["_id"s]) != pk)
    {
        erase(prior, undo);
        insert(document, position, undo);
        return;
    }

    place_primary(pk, position, undo);
    for(const auto& field_name : m_key_names)
    {
        const auto before = indexed_value(prior, field_name);
        const auto after = indexed_value(document, field_name);
        const auto less = m_secondary_keys.at(field_name).key_comp();
        const auto same_key = before and after and not less(*before, *after) and not less(*after, *before);
        if(before and not same_key)
            remove_posting(field_name, *before, pk, undo);
        if(after)
            add_posting(field_name, *after, pk, position, undo);
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
        if(const auto sk = indexed_value(document, field_name))
            remove_posting(field_name, *sk, pk, undo);
    }
}

void yar::db::index::load(yar::db::object& document, yar::db::position_type position)
{
    const auto pk = make_primary_key(document["_id"s]);
    m_primary_keys.insert_or_assign(pk, position);
    for(const auto& field_name : m_key_names)
    {
        if(const auto sk = indexed_value(document, field_name))
            m_secondary_keys.at(field_name)[*sk].push_back(yar::db::posting{pk, position});
    }
}

void yar::db::index::finish_load()
{
    const auto superseded = [this](const yar::db::posting& entry)
    {
        const auto live = m_primary_keys.find(entry.id);
        return live == m_primary_keys.end() or live->second != entry.position;
    };
    for(auto& secondary : m_secondary_keys | std::views::values)
    {
        for(auto key = secondary.begin(); key != secondary.end();)
        {
            auto& postings = key->second;
            std::erase_if(postings, superseded);
            if(not std::ranges::is_sorted(postings, {}, &yar::db::posting::id))
                std::ranges::sort(postings, {}, &yar::db::posting::id);
            if(postings.empty())
                key = secondary.erase(key);
            else
                ++key;
        }
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
        else if(const auto* action = std::get_if<secondary_inserted>(&*op))
        {
            const auto field = target.m_secondary_keys.find(action->field);
            if(field == target.m_secondary_keys.end())
                continue;
            const auto key = field->second.find(action->key);
            if(key == field->second.end())
                continue;
            auto& postings = key->second;
            const auto at = std::ranges::lower_bound(postings, action->id, {}, &yar::db::posting::id);
            if(at != postings.end() and at->id == action->id)
                postings.erase(at);
            if(postings.empty())
                field->second.erase(key);
        }
        else if(const auto* action = std::get_if<secondary_moved>(&*op))
        {
            const auto field = target.m_secondary_keys.find(action->field);
            if(field == target.m_secondary_keys.end())
                continue;
            const auto key = field->second.find(action->key);
            if(key == field->second.end())
                continue;
            auto& postings = key->second;
            const auto at = std::ranges::lower_bound(postings, action->id, {}, &yar::db::posting::id);
            if(at != postings.end() and at->id == action->id)
                at->position = action->position;
        }
        else if(const auto* action = std::get_if<secondary_removed>(&*op))
        {
            auto& secondary = target.m_secondary_keys.at(action->field);
            if(action->erased_key)
            {
                secondary.emplace(action->key, yar::db::postings_type{action->entry});
                continue;
            }
            auto& postings = secondary.at(action->key);
            const auto at = std::ranges::lower_bound(postings, action->entry.id, {}, &yar::db::posting::id);
            postings.insert(at, action->entry);
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
