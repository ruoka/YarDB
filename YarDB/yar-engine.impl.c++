module yar;
import :metadata;
import std;
import xson;

namespace {

using namespace std::string_literals;

bool reopen(std::fstream& storage, const std::string& db)
{
    storage.close();
    storage.clear();
    storage.open(db, std::ios::out | std::ios::in | std::ios::binary);
    return storage.is_open();
}

// Distinguish status-restore failure from truncate/reopen failure so destroy /
// update / replace can publish the durable outcome instead of reporting failure
// while API and disk disagree.
//
// truncate_failed: resize_file failed — appends past original_size remain.
// reopen_failed: resize succeeded but the stream could not be reopened — file
// matches the pre-image size; callers must not publish successor indexes.
enum class rollback_result
{
    ok,
    status_restore_failed,
    truncate_failed,
    reopen_failed
};

rollback_result rollback(
    std::fstream& storage,
    const std::string& db,
    std::uintmax_t original_size,
    const std::vector<yar::db::position_type>& status_positions,
    bool inject_status_failure = false,
    bool inject_truncate_failure = false)
{
    using xson::fson::operator <<;

    // Restore prior rows to created before truncating appends. If restore fails
    // (e.g. ENOSPC) and we still truncate, reopen skips updated/deleted rows and
    // the documents disappear — silent data loss.
    storage.clear();
    auto statuses_restored = true;
    if(not status_positions.empty())
    {
        if(inject_status_failure)
            statuses_restored = false;
        else
        {
            for(const auto position : status_positions)
            {
                storage.seekp(position, storage.beg);
                storage << yar::db::metadata{yar::db::metadata::created};
                if(storage.fail())
                    break;
            }
            storage.flush();
            statuses_restored = not storage.fail();
        }
    }

    if(not statuses_restored)
    {
        storage.close();
        reopen(storage, db);
        return rollback_result::status_restore_failed;
    }

    storage.close();
    if(inject_truncate_failure)
    {
        // Leave appends in place (same as a failed resize_file) and reopen.
        reopen(storage, db);
        return rollback_result::truncate_failed;
    }

    auto resize_error = std::error_code{};
    std::filesystem::resize_file(db, original_size, resize_error);
    if(resize_error)
    {
        reopen(storage, db);
        return rollback_result::truncate_failed;
    }

    if(reopen(storage, db))
        return rollback_result::ok;
    return rollback_result::reopen_failed;
}

// True when every record past original_size parses completely and the file ends
// on a record boundary. Used before publishing staged indexes on truncate_failed
// so a torn ENOSPC append cannot be treated as a durable create/update/replace.
bool complete_appended_records(
    std::fstream& storage,
    const std::string& db,
    std::uintmax_t original_size,
    std::size_t expected_records)
{
    using xson::fson::operator >>;

    if(expected_records == 0)
        return false;

    auto size_error = std::error_code{};
    const auto file_size = std::filesystem::file_size(db, size_error);
    if(size_error or file_size <= original_size)
        return false;

    storage.clear();
    storage.seekg(static_cast<std::streamoff>(original_size), storage.beg);
    for(std::size_t i = 0; i < expected_records; ++i)
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        try
        {
            storage >> metadata >> document;
        }
        catch(const std::runtime_error&)
        {
            return false;
        }
        if(storage.fail())
            return false;
    }

    const auto end_pos = storage.tellg();
    if(end_pos < 0)
        return false;
    return static_cast<std::uintmax_t>(end_pos) == file_size;
}

void validate_and_recover_storage(std::fstream& storage, const std::string& db)
{
    using xson::fson::operator >>;

    auto size_error = std::error_code{};
    const auto file_size = std::filesystem::file_size(db, size_error);
    if(size_error)
        throw std::runtime_error{"Failed to determine database size: "s + size_error.message()};

    auto last_complete = std::streamoff{0};
    storage.clear();
    storage.seekg(0, storage.beg);

    while(static_cast<std::uintmax_t>(last_complete) < file_size)
    {
        const auto record_start = last_complete;
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        try
        {
            storage >> metadata >> document;
        }
        catch(const std::runtime_error&)
        {
            if(not storage.eof())
                throw std::runtime_error{
                    "Corrupt database record at offset "s + std::to_string(record_start)};
        }

        if(storage.fail())
        {
            if(not storage.eof())
                throw std::runtime_error{
                    "Failed to read database record at offset "s + std::to_string(record_start)};
            break;
        }

        if(metadata.status != yar::db::metadata::created
            and metadata.status != yar::db::metadata::updated
            and metadata.status != yar::db::metadata::deleted)
            throw std::runtime_error{
                "Invalid database record status at offset "s + std::to_string(record_start)};

        if(metadata.position != record_start)
            throw std::runtime_error{
                "Invalid database record position at offset "s + std::to_string(record_start)};

        if(metadata.previous >= metadata.position or metadata.previous < -1)
            throw std::runtime_error{
                "Invalid database history link at offset "s + std::to_string(record_start)};

        last_complete = storage.tellg();
        if(last_complete <= record_start)
            throw std::runtime_error{
                "Database scan made no progress at offset "s + std::to_string(record_start)};
    }

    if(static_cast<std::uintmax_t>(last_complete) == file_size)
    {
        storage.clear();
        return;
    }

    storage.close();
    auto resize_error = std::error_code{};
    std::filesystem::resize_file(db, static_cast<std::uintmax_t>(last_complete), resize_error);
    if(resize_error or not reopen(storage, db))
        throw std::runtime_error{
            "Failed to recover truncated database tail at offset "s + std::to_string(last_complete)};
}

template<typename T, typename Extractor>
auto metadata_value(
    std::istream& storage,
    const yar::db::index_view& view,
    const yar::db::object& selector,
    Extractor extractor) -> std::optional<T>
{
    using xson::fson::operator >>;

    for(const auto position : view)
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        storage.clear();
        storage.seekg(position, storage.beg);
        storage >> metadata >> document;
        if(document.match(selector))
            return extractor(metadata);
    }

    return std::nullopt;
}

const yar::db::index* find_index(
    const std::flat_map<std::string, yar::db::index>& indexes,
    std::string_view collection)
{
    const auto it = indexes.find(std::string{collection});
    if(it == indexes.end())
        return nullptr;
    return std::addressof(it->second);
}

yar::db::index* find_index(
    std::flat_map<std::string, yar::db::index>& indexes,
    std::string_view collection)
{
    return std::addressof(indexes[std::string{collection}]);
}

// Crash between appending a successor and tombstoning the prior row can leave
// two status=created records for one _id. Primary insert would overwrite while
// secondary entries for the stale pre-image remain. Drop the earlier live row
// from the index (and heal its on-disk status) before indexing the later one.
void supersede_prior_live_row(
    std::fstream& storage,
    yar::db::index& index,
    const yar::db::object& document,
    bool heal_status)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

    if(not document.has("_id"s) or not document["_id"s].is_integer())
        return;

    const auto id = static_cast<yar::db::sequence_type>(document["_id"s]);
    const auto prior_position = index.position(id);
    if(not prior_position.has_value())
        return;

    const auto resume = storage.tellg();
    auto prior_metadata = yar::db::metadata{};
    auto prior_document = yar::db::object{};
    storage.clear();
    storage.seekg(*prior_position, storage.beg);
    storage >> prior_metadata >> prior_document;
    if(not storage.fail())
    {
        index.erase(prior_document);
        if(heal_status and prior_metadata.status == yar::db::metadata::created)
        {
            storage.clear();
            storage.seekp(*prior_position, storage.beg);
            storage << yar::db::updated;
            storage.flush();
        }
    }

    storage.clear();
    storage.seekg(resume, storage.beg);
}

} // namespace

yar::db::engine::engine(std::string_view db) :
    m_db{db},
    m_lock{m_db},
    m_index{},
    m_storage{}
{
    m_storage.open(m_db, std::ios::out | std::ios::in | std::ios::binary);
    if(not m_storage.is_open())
        m_storage.open(m_db, std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if(not m_storage.is_open())
        throw std::runtime_error{"Failed to open/create DB "s + m_db};
    validate_and_recover_storage(m_storage, m_db);
    setup_index_structure();
    populate_indexes();
}

yar::db::engine::engine(yar::db::engine&& e) :
    m_db{std::move(e.m_db)},
    m_lock{std::move(e.m_lock)},
    m_index{std::move(e.m_index)},
    m_storage{std::move(e.m_storage)},
    m_writes_until_failure{std::exchange(e.m_writes_until_failure, 0)},
    m_fail_rollback_status{std::exchange(e.m_fail_rollback_status, false)},
    m_fail_truncate{std::exchange(e.m_fail_truncate, false)},
    m_fail_torn_append{std::exchange(e.m_fail_torn_append, false)},
    m_writable{std::exchange(e.m_writable, false)}
{}

yar::db::engine::~engine() = default;

std::ifstream yar::db::engine::open_reader() const
{
    auto reader = std::ifstream{m_db, std::ios::in | std::ios::binary};
    if(not reader.is_open())
        throw std::runtime_error{"Failed to open database for reading: "s + m_db};
    return reader;
}

bool yar::db::engine::preconditions_met(
    const metadata& metadata_record,
    const write_preconditions& preconditions) const
{
    if(preconditions.if_match_position
        and metadata_record.position != *preconditions.if_match_position)
        return false;

    if(preconditions.if_unmodified_since)
    {
        using namespace std::chrono;
        const auto document_seconds = floor<seconds>(metadata_record.timestamp);
        const auto client_seconds = floor<seconds>(*preconditions.if_unmodified_since);
        if(document_seconds > client_seconds)
            return false;
    }

    return true;
}

void yar::db::fail_next_write(yar::db::engine& engine)
{
    engine.m_writes_until_failure = 1;
}

void yar::db::fail_write(yar::db::engine& engine, std::size_t phase)
{
    engine.m_writes_until_failure = phase;
}

void yar::db::fail_next_rollback_status(yar::db::engine& engine)
{
    engine.m_fail_rollback_status = true;
}

void yar::db::fail_next_truncate(yar::db::engine& engine)
{
    engine.m_fail_truncate = true;
}

void yar::db::fail_next_torn_append(yar::db::engine& engine)
{
    engine.m_fail_torn_append = true;
}

bool yar::db::engine::consume_write_failure()
{
    if(m_writes_until_failure == 0)
        return false;
    return --m_writes_until_failure == 0;
}

bool yar::db::engine::consume_rollback_status_failure()
{
    return std::exchange(m_fail_rollback_status, false);
}

bool yar::db::engine::consume_truncate_failure()
{
    return std::exchange(m_fail_truncate, false);
}

bool yar::db::engine::consume_torn_append_failure()
{
    return std::exchange(m_fail_torn_append, false);
}

// After a flushed append, tear the trailing record and mark the stream failed so
// append-phase rollback sees durable-but-incomplete bytes (ENOSPC mid-record).
bool yar::db::engine::inject_torn_append(std::uintmax_t original_size)
{
    if(not consume_torn_append_failure())
        return false;

    m_storage.flush();
    m_storage.close();
    auto size_error = std::error_code{};
    const auto file_size = std::filesystem::file_size(m_db, size_error);
    if(not size_error and file_size > original_size)
    {
        auto resize_error = std::error_code{};
        std::filesystem::resize_file(m_db, original_size + 1, resize_error);
    }
    reopen(m_storage, m_db);
    m_storage.setstate(std::ios::badbit);
    return true;
}

void yar::db::engine::setup_index_structure()
{
    using xson::fson::operator >>;

    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);

    while(m_storage)
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        m_storage >> metadata >> document;

        if(m_storage.fail())
            break;

        m_index[metadata.collection].update(document);

        if(metadata.collection == "_db"s)
        {
            const auto collection_name = static_cast<std::string>(document["collection"s]);
            auto keys = document["keys"s];
            auto temp = std::vector<std::string>{};
            for(const auto& k : keys.get<yar::db::object::array>())
                temp.push_back(k);

            m_index[collection_name].add(temp);
        }
    }
}

void yar::db::engine::populate_indexes()
{
    using xson::fson::operator >>;

    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);

    while(m_storage)
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        m_storage >> metadata >> document;

        if(m_storage.fail())
            break;

        if(metadata.status == metadata::deleted or metadata.status == metadata::updated)
            continue;

        auto& index = m_index[metadata.collection];
        supersede_prior_live_row(m_storage, index, document, true);
        index.insert(document, metadata.position);
    }
}

yar::db::db_result<> yar::db::engine::reindex()
{
    using xson::fson::operator >>;

    auto lock = std::unique_lock{m_rwlock};

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::reindex,
            "Database writes are disabled after an unsuccessful rollback"s)};

    auto rebuilt = decltype(m_index){};
    for(const auto& [collection, current] : m_index)
        rebuilt[collection].add(current.keys());

    m_storage.clear();
    m_storage.seekg(0, m_storage.beg);
    while(m_storage.peek() != std::char_traits<char>::eof())
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        m_storage >> metadata >> document;
        if(m_storage.fail())
        {
            m_storage.clear();
            return std::unexpected{db_error(
                db_error_code::corrupt_storage,
                db_operation::reindex,
                "Failed to read a stored document while rebuilding indexes"s)};
        }

        auto& rebuilt_index = rebuilt[metadata.collection];
        rebuilt_index.update(document);
        if(metadata.status != metadata::deleted and metadata.status != metadata::updated)
        {
            // Rebuilds must not mutate durable statuses; reopen already heals
            // dual-live rows. Here only the in-memory index is corrected.
            supersede_prior_live_row(m_storage, rebuilt_index, document, false);
            rebuilt_index.insert(document, metadata.position);
        }
    }
    m_storage.clear();
    m_index = std::move(rebuilt);
    return {};
}

std::vector<std::string> yar::db::engine::indexed_keys(std::string_view collection) const
{
    auto lock = std::shared_lock{m_rwlock};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return {};
    return index->keys();
}

yar::db::db_result<> yar::db::engine::index(std::string_view collection, std::vector<std::string> keys)
{
    auto lock = std::unique_lock{m_rwlock};

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::index,
            "Database writes are disabled after an unsuccessful rollback"s)};

    auto original = m_index;
    auto& current_index = m_index[std::string{collection}];
    current_index.add(keys);
    auto selector = yar::db::object{"collection"s, std::string{collection}};
    auto document = yar::db::object{selector, {"keys"s, current_index.keys()}};
    auto documents = yar::db::object{};
    auto result = upsert_impl("_db"s, selector, document, documents);
    if(not result)
    {
        m_index = std::move(original);
        return std::unexpected{db_error(
            result.error().code,
            db_operation::index,
            result.error().message)};
    }
    return {};
}

yar::db::db_result<> yar::db::engine::create(std::string_view collection, yar::db::object& document)
{
    auto lock = std::unique_lock{m_rwlock};
    return create_impl(collection, document);
}

yar::db::db_result<> yar::db::engine::create_impl(std::string_view collection, yar::db::object& document)
{
    using xson::fson::operator <<;

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::create,
            "Database writes are disabled after an unsuccessful rollback"s)};

    auto file_size_error = std::error_code{};
    const auto original_size = std::filesystem::file_size(m_db, file_size_error);
    if(file_size_error)
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::create,
            "Failed to determine database size: "s + file_size_error.message())};

    auto staged = m_index;
    auto& index = staged[std::string{collection}];

    // Client-/import-supplied _id must not clobber an existing primary key.
    // index::insert overwrites m_primary_keys[id], which orphans the prior
    // live record and can leave secondary indexes pointing at the old position.
    if(document.has("_id"s))
    {
        if(not document["_id"s].is_integer())
            return std::unexpected{db_error(
                db_error_code::conflict,
                db_operation::create,
                "Document _id must be an integer"s)};

        const auto id = static_cast<yar::db::sequence_type>(document["_id"s]);
        if(index.contains_id(id))
            return std::unexpected{db_error(
                db_error_code::conflict,
                db_operation::create,
                "Document with _id "s + std::to_string(id) + " already exists"s)};
    }

    auto metadata = yar::db::metadata{std::string{collection}};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    index.update(document);
    m_storage << metadata << document;
    // Flush before synthetic failure so rollback exercises truncating a durable
    // append (same model as status-phase inject), not merely an unflushed buffer.
    m_storage.flush();
    if(not inject_torn_append(original_size) and consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    if(m_storage.fail())
    {
        const auto rolled_back = rollback(
            m_storage,
            m_db,
            original_size,
            {},
            false,
            consume_truncate_failure());
        // Durable complete append remains when truncate fails. Publish so the
        // live index matches reopen. A torn append must not publish — reopen
        // would drop the incomplete tail while we would have tombstoned nothing
        // here, but update/replace siblings of this path would lose priors.
        if(rolled_back == rollback_result::truncate_failed)
        {
            if(complete_appended_records(m_storage, m_db, original_size, 1))
            {
                index.insert(document, metadata.position);
                m_index = std::move(staged);
                return {};
            }
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::create,
                "Create append was incomplete and could not be rolled back"s)};
        }
        if(rolled_back != rollback_result::ok)
        {
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::create,
                "Create failed and the appended record could not be rolled back"s)};
        }
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::create,
            "Failed to append and flush the new document"s)};
    }

    index.insert(document, metadata.position);
    m_index = std::move(staged);
    return {};
}

std::size_t yar::db::engine::count(std::string_view collection, const yar::db::object& selector) const
{
    auto lock = std::shared_lock{m_rwlock};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return 0;

    auto reader = open_reader();
    return index->count(reader, selector);
}

void yar::db::apply_orderby(yar::db::object& documents, std::string_view field, bool descending)
{
    if(not documents.is_array())
        return;

    auto& items = documents.get<yar::db::object::array>();
    const auto field_name = std::string{field};

    const auto sortable = [&](const yar::db::object& doc)
    {
        if(not doc.has(field_name))
            return false;
        const auto& value = doc[field_name];
        return value.has_value() and not value.is_null();
    };

    const auto id_of = [](const yar::db::object& doc) -> std::int64_t
    {
        if(doc.has("_id"s) and doc["_id"s].is_integer())
            return static_cast<std::int64_t>(doc["_id"s]);
        return 0;
    };

    std::ranges::sort(items, [&](const yar::db::object& lhs, const yar::db::object& rhs)
    {
        const auto lhs_present = sortable(lhs);
        const auto rhs_present = sortable(rhs);
        if(lhs_present != rhs_present)
            return lhs_present and not rhs_present;

        if(lhs_present)
        {
            const auto& left = lhs[field_name].get<xson::primitive>();
            const auto& right = rhs[field_name].get<xson::primitive>();
            if(not xson::primitive_equal(left, right))
            {
                const auto less = xson::primitive_less(left, right);
                return descending ? not less : less;
            }
        }

        return id_of(lhs) < id_of(rhs);
    });
}

bool yar::db::engine::read(std::string_view collection, const yar::db::object& selector, yar::db::object& documents)
{
    using xson::fson::operator >>;

    auto lock = std::shared_lock{m_rwlock};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return false;

    documents = yar::db::object{yar::db::object::array{}};
    auto top = std::numeric_limits<yar::db::sequence_type>::max();
    if(selector.has("$top"s))
        top = selector["$top"s];

    auto skip = sequence_type{0};
    if(selector.has("$skip"s))
        skip = selector["$skip"s];

    const auto orderby = selector.has("$orderby"s);
    const auto descending = selector.has("$desc"s);

    auto success = false;
    auto reader = open_reader();

    for(const auto position : index->view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        reader.clear();
        reader.seekg(position, reader.beg);
        reader >> metadata >> document;
        if(document.match(selector))
        {
            if(not orderby)
            {
                if(skip > 0)
                {
                    --skip;
                    continue;
                }

                // $top=0 must yield an empty page. Post-decrement (`--top == 0`)
                // never stops when top starts at 0 (underflow), and would return
                // every match instead.
                if(top == 0)
                    break;

                documents += std::move(document);
                success = true;
                --top;
                continue;
            }

            documents += std::move(document);
            success = true;
        }
    }

    if(orderby and success)
    {
        apply_orderby(documents, static_cast<std::string>(selector["$orderby"s]), descending);

        auto& items = documents.get<yar::db::object::array>();
        if(skip > 0)
        {
            if(static_cast<std::size_t>(skip) >= items.size())
            {
                documents = yar::db::object{yar::db::object::array{}};
                return false;
            }
            items.erase(items.begin(), items.begin() + static_cast<std::ptrdiff_t>(skip));
        }
        if(top < std::numeric_limits<yar::db::sequence_type>::max()
            and static_cast<std::size_t>(top) < items.size())
        {
            items.resize(static_cast<std::size_t>(top));
        }
        success = not items.empty();
    }

    return success;
}

std::optional<std::chrono::system_clock::time_point> yar::db::engine::metadata_timestamp(
    std::string_view collection,
    const yar::db::object& selector) const
{
    auto lock = std::shared_lock{m_rwlock};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return std::nullopt;

    auto reader = open_reader();
    return metadata_value<std::chrono::system_clock::time_point>(
        reader, index->view(selector), selector, [](const yar::db::metadata& m) { return m.timestamp; });
}

std::optional<std::int64_t> yar::db::engine::metadata_position(
    std::string_view collection,
    const yar::db::object& selector) const
{
    auto lock = std::shared_lock{m_rwlock};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return std::nullopt;

    auto reader = open_reader();
    return metadata_value<std::int64_t>(
        reader, index->view(selector), selector, [](const yar::db::metadata& m) { return m.position; });
}

yar::db::db_result<std::size_t> yar::db::engine::update(
    std::string_view collection,
    const yar::db::object& selector,
    const yar::db::object& updates,
    yar::db::object& documents,
    write_preconditions preconditions)
{
    auto lock = std::unique_lock{m_rwlock};
    return update_impl(collection, selector, updates, documents, preconditions);
}

yar::db::db_result<std::size_t> yar::db::engine::update_impl(
    std::string_view collection,
    const yar::db::object& selector,
    const yar::db::object& updates,
    yar::db::object& documents,
    write_preconditions preconditions)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::update,
            "Database writes are disabled after an unsuccessful rollback"s)};

    documents = yar::db::object{yar::db::object::array{}};
    struct pending_update
    {
        position_type position;
        metadata metadata_record;
        object old_document;
        object new_document;
    };
    auto pending = std::vector<pending_update>{};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return 0;

    for(const auto position : index->view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto old_document = yar::db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> old_document;
        if(m_storage.fail())
            return std::unexpected{db_error(
                db_error_code::corrupt_storage,
                db_operation::update,
                "Failed to read a document selected for update"s)};
        if(old_document.match(selector))
        {
            if(not preconditions_met(metadata, preconditions))
                return std::unexpected{db_error(
                    db_error_code::precondition_failed,
                    db_operation::update,
                    "Write preconditions were not met"s)};

            auto new_document = old_document;
            new_document += updates;
            if(new_document.has("_id"s))
            {
                if(not new_document["_id"s].is_integer())
                    return std::unexpected{db_error(
                        db_error_code::conflict,
                        db_operation::update,
                        "Document _id must be an integer"s)};

                const auto new_id = static_cast<sequence_type>(new_document["_id"s]);
                const auto old_id = static_cast<sequence_type>(old_document["_id"s]);
                if(new_id != old_id)
                    return std::unexpected{db_error(
                        db_error_code::conflict,
                        db_operation::update,
                        "Document _id is immutable"s)};
            }
            pending.push_back({position, metadata, std::move(old_document), std::move(new_document)});
        }
    }

    if(pending.empty())
        return 0;

    auto file_size_error = std::error_code{};
    const auto original_size = std::filesystem::file_size(m_db, file_size_error);
    if(file_size_error)
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::update,
            "Failed to determine database size: "s + file_size_error.message())};

    auto staged = m_index;
    auto& staged_index = staged[std::string{collection}];
    for(auto& entry : pending)
    {
        staged_index.erase(entry.old_document);
        staged_index.update(entry.new_document);
        m_storage.clear();
        m_storage.seekp(0, m_storage.end);
        m_storage << entry.metadata_record << entry.new_document;
        staged_index.insert(entry.new_document, entry.metadata_record.position);
    }
    // Flush before synthetic failure so append-phase rollback sees durable
    // successors (mirrors status-phase inject ordering).
    m_storage.flush();
    if(not inject_torn_append(original_size) and consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    if(m_storage.fail())
    {
        const auto rolled_back = rollback(
            m_storage,
            m_db,
            original_size,
            {},
            false,
            consume_truncate_failure());
        // Complete successors remain on disk. Without publishing staged, live
        // reads keep the pre-image while reopen supersedes to the new version.
        // Incomplete (torn) appends must not tombstone priors — reopen drops the
        // torn tail and would otherwise lose the documents.
        if(rolled_back == rollback_result::truncate_failed)
        {
            if(complete_appended_records(m_storage, m_db, original_size, pending.size()))
            {
                for(const auto& entry : pending)
                {
                    m_storage.clear();
                    m_storage.seekp(entry.position, m_storage.beg);
                    m_storage << yar::db::updated;
                }
                m_storage.flush();
                for(auto& entry : pending)
                    documents += std::move(entry.new_document);
                m_index = std::move(staged);
                return pending.size();
            }
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::update,
                "Update append was incomplete and could not be rolled back"s)};
        }
        if(rolled_back != rollback_result::ok)
        {
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::update,
                "Update append failed and could not be rolled back"s)};
        }
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::update,
            "Failed to append and flush updated documents"s)};
    }

    auto status_positions = std::vector<position_type>{};
    status_positions.reserve(pending.size());
    for(const auto& entry : pending)
    {
        m_storage.clear();
        m_storage.seekp(entry.position, m_storage.beg);
        m_storage << yar::db::updated;
        status_positions.push_back(entry.position);
    }
    // Flush before synthetic failure so rollback exercises restore of on-disk
    // updated markers (ENOSPC-during-restore), not merely unflushed buffers.
    m_storage.flush();
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    if(m_storage.fail())
    {
        // Successors were flushed. If undo cannot restore created on prior
        // rows, or restore succeeds but truncating appends fails, reopen would
        // index the successors (supersede dual-live) while a stale m_index /
        // error return would disagree — publish staged instead.
        const auto rolled_back = rollback(
            m_storage,
            m_db,
            original_size,
            status_positions,
            consume_rollback_status_failure(),
            consume_truncate_failure());
        if(rolled_back == rollback_result::status_restore_failed
            or rolled_back == rollback_result::truncate_failed)
        {
            if(rolled_back == rollback_result::truncate_failed)
            {
                // Status restore left priors created beside durable successors.
                // Re-assert updated markers so the live chain matches staged.
                for(const auto position : status_positions)
                {
                    m_storage.clear();
                    m_storage.seekp(position, m_storage.beg);
                    m_storage << yar::db::updated;
                }
                m_storage.flush();
            }
            for(auto& entry : pending)
                documents += std::move(entry.new_document);
            m_index = std::move(staged);
            return pending.size();
        }
        if(rolled_back != rollback_result::ok)
        {
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::update,
                "Update status write failed and could not be rolled back"s)};
        }
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::update,
            "Failed to mark previous document versions as updated"s)};
    }

    for(auto& entry : pending)
        documents += std::move(entry.new_document);
    m_index = std::move(staged);
    return pending.size();
}

yar::db::db_result<std::size_t> yar::db::engine::destroy(
    std::string_view collection,
    const yar::db::object& selector,
    yar::db::object& documents,
    write_preconditions preconditions)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

    auto lock = std::unique_lock{m_rwlock};

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::destroy,
            "Database writes are disabled after an unsuccessful rollback"s)};

    documents = yar::db::object{yar::db::object::array{}};
    auto top = std::numeric_limits<yar::db::sequence_type>::max();
    if(selector.has("$top"s))
        top = selector["$top"s];

    auto positions = std::vector<position_type>{};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return 0;

    for(const auto position : index->view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(m_storage.fail())
            return std::unexpected{db_error(
                db_error_code::corrupt_storage,
                db_operation::destroy,
                "Failed to read a document selected for deletion"s)};
        if(document.match(selector))
        {
            if(not preconditions_met(metadata, preconditions))
                return std::unexpected{db_error(
                    db_error_code::precondition_failed,
                    db_operation::destroy,
                    "Write preconditions were not met"s)};

            // Same $top=0 pitfall as read: post-decrement never hits 0 when top
            // starts at 0, which would delete every matching document.
            if(top == 0)
                break;

            documents += std::move(document);
            positions.push_back(position);
            --top;
        }
    }

    if(positions.empty())
        return 0;

    auto file_size_error = std::error_code{};
    const auto original_size = std::filesystem::file_size(m_db, file_size_error);
    if(file_size_error)
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::destroy,
            "Failed to determine database size: "s + file_size_error.message())};

    auto staged = m_index;
    auto& staged_index = staged[std::string{collection}];
    for(const auto& document : documents.get<yar::db::object::array>())
        staged_index.erase(document);

    for(const auto position : positions)
    {
        m_storage.clear();
        m_storage.seekp(position, m_storage.beg);
        m_storage << yar::db::deleted;
    }
    m_storage.flush();
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    if(m_storage.fail())
    {
        // Delete markers were flushed. If undo cannot restore created, durable
        // tombstones remain and reopen would drop the docs while we reported
        // failure — commit the staged index so API and disk agree. Re-assert
        // deleted markers in case a partial restore revived some rows.
        const auto rolled_back = rollback(
            m_storage,
            m_db,
            original_size,
            positions,
            consume_rollback_status_failure(),
            consume_truncate_failure());
        if(rolled_back == rollback_result::status_restore_failed)
        {
            for(const auto position : positions)
            {
                m_storage.clear();
                m_storage.seekp(position, m_storage.beg);
                m_storage << yar::db::deleted;
            }
            m_storage.flush();
            m_index = std::move(staged);
            return positions.size();
        }
        if(rolled_back != rollback_result::ok)
        {
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::destroy,
                "Delete failed and changed statuses could not be restored"s)};
        }
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::destroy,
            "Failed to mark documents as deleted"s)};
    }

    m_index = std::move(staged);
    return positions.size();
}

yar::db::db_result<std::size_t> yar::db::engine::replace(
    std::string_view collection,
    const yar::db::object& selector,
    yar::db::object& document,
    write_preconditions preconditions)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

    auto lock = std::unique_lock{m_rwlock};

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::replace,
            "Database writes are disabled after an unsuccessful rollback"s)};

    auto positions = std::vector<position_type>{};
    auto old_documents = std::vector<object>{};
    auto chain_metadata = std::optional<yar::db::metadata>{};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return 0;

    for(const auto position : index->view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto old_document = yar::db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> old_document;
        if(m_storage.fail())
            return std::unexpected{db_error(
                db_error_code::corrupt_storage,
                db_operation::replace,
                "Failed to read a document selected for replacement"s)};
        if(old_document.match(selector))
        {
            if(not preconditions_met(metadata, preconditions))
                return std::unexpected{db_error(
                    db_error_code::precondition_failed,
                    db_operation::replace,
                    "Write preconditions were not met"s)};

            positions.push_back(position);
            old_documents.push_back(std::move(old_document));
            // Reuse the first matched record's metadata so operator<< chains
            // previous → old position (same as update_impl).
            if(not chain_metadata.has_value())
                chain_metadata = metadata;
        }
    }

    if(positions.empty())
        return 0;

    // replace appends a single successor. Matching multiple live rows would
    // tombstone every match while only one document remains — silent data loss.
    if(positions.size() > 1)
        return std::unexpected{db_error(
            db_error_code::conflict,
            db_operation::replace,
            "Replace requires a selector that matches exactly one document"s)};

    auto file_size_error = std::error_code{};
    const auto original_size = std::filesystem::file_size(m_db, file_size_error);
    if(file_size_error)
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::replace,
            "Failed to determine database size: "s + file_size_error.message())};

    auto staged = m_index;
    auto& staged_index = staged[std::string{collection}];
    for(const auto& old_document : old_documents)
        staged_index.erase(old_document);

    // Mirror create/update: do not let replace clobber another live primary key.
    // After erasing matched rows, contains_id only sees uninvolved documents.
    if(document.has("_id"s))
    {
        if(not document["_id"s].is_integer())
            return std::unexpected{db_error(
                db_error_code::conflict,
                db_operation::replace,
                "Document _id must be an integer"s)};

        const auto new_id = static_cast<sequence_type>(document["_id"s]);
        if(staged_index.contains_id(new_id))
            return std::unexpected{db_error(
                db_error_code::conflict,
                db_operation::replace,
                "Document with _id "s + std::to_string(new_id) + " already exists"s)};
    }

    staged_index.update(document);

    auto metadata = *chain_metadata;
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    m_storage << metadata << document;
    // Flush before synthetic failure so append-phase rollback sees a durable
    // successor (mirrors status-phase inject ordering).
    m_storage.flush();
    if(not inject_torn_append(original_size) and consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    if(m_storage.fail())
    {
        const auto rolled_back = rollback(
            m_storage,
            m_db,
            original_size,
            {},
            false,
            consume_truncate_failure());
        // Complete successor remains on disk. Publish staged (and tombstone the
        // prior) so API and reopen agree. A torn successor must not tombstone
        // the prior — reopen would drop the incomplete tail and lose the row.
        if(rolled_back == rollback_result::truncate_failed)
        {
            if(complete_appended_records(m_storage, m_db, original_size, 1))
            {
                staged_index.insert(document, metadata.position);
                for(const auto position : positions)
                {
                    m_storage.clear();
                    m_storage.seekp(position, m_storage.beg);
                    m_storage << yar::db::updated;
                }
                m_storage.flush();
                m_index = std::move(staged);
                return positions.size();
            }
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::replace,
                "Replacement append was incomplete and could not be rolled back"s)};
        }
        if(rolled_back != rollback_result::ok)
        {
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::replace,
                "Replacement append failed and could not be rolled back"s)};
        }
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::replace,
            "Failed to append and flush the replacement document"s)};
    }
    staged_index.insert(document, metadata.position);

    // Mark prior live versions as updated (not deleted) so history remains walkable.
    for(const auto position : positions)
    {
        m_storage.clear();
        m_storage.seekp(position, m_storage.beg);
        m_storage << yar::db::updated;
    }
    m_storage.flush();
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    if(m_storage.fail())
    {
        // Replacement was flushed. If undo cannot restore created on prior
        // rows, or restore succeeds but truncating the successor fails, reopen
        // indexes the successor while a stale m_index / error return would
        // disagree — publish staged instead.
        const auto rolled_back = rollback(
            m_storage,
            m_db,
            original_size,
            positions,
            consume_rollback_status_failure(),
            consume_truncate_failure());
        if(rolled_back == rollback_result::status_restore_failed
            or rolled_back == rollback_result::truncate_failed)
        {
            if(rolled_back == rollback_result::truncate_failed)
            {
                for(const auto position : positions)
                {
                    m_storage.clear();
                    m_storage.seekp(position, m_storage.beg);
                    m_storage << yar::db::updated;
                }
                m_storage.flush();
            }
            m_index = std::move(staged);
            return positions.size();
        }
        if(rolled_back != rollback_result::ok)
        {
            m_writable = false;
            return std::unexpected{db_error(
                db_error_code::rollback_failure,
                db_operation::replace,
                "Replacement status write failed and could not be rolled back"s)};
        }
        return std::unexpected{db_error(
            db_error_code::io_failure,
            db_operation::replace,
            "Failed to mark replaced document versions as updated"s)};
    }

    m_index = std::move(staged);
    return positions.size();
}

bool yar::db::engine::history(std::string_view collection, const yar::db::object& selector, yar::db::object& documents)
{
    using xson::fson::operator >>;

    auto lock = std::shared_lock{m_rwlock};
    const auto* index = find_index(m_index, collection);
    if(index == nullptr)
        return false;

    documents = yar::db::object{yar::db::object::array{}};
    auto success = false;
    auto reader = open_reader();

    for(auto position : index->view(selector))
        while(position >= 0)
        {
            auto metadata = yar::db::metadata{};
            auto document = yar::db::object{};
            reader.clear();
            reader.seekg(position, reader.beg);
            reader >> metadata >> document;
            position = metadata.previous;
            documents += std::move(document);
            success = true;
        }

    return success;
}

yar::db::db_result<std::size_t> yar::db::engine::upsert_impl(
    std::string_view collection,
    const object& selector,
    object& updates,
    object& documents)
{
    auto result = update_impl(collection, selector, updates, documents, {});
    if(not result)
        return std::unexpected{result.error()};
    if(*result > 0)
        return result;

    auto created = create_impl(collection, updates);
    if(not created)
        return std::unexpected{created.error()};
    documents += updates;
    return 1;
}
