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

bool rollback(
    std::fstream& storage,
    const std::string& db,
    std::uintmax_t original_size,
    const std::vector<yar::db::position_type>& status_positions)
{
    using xson::fson::operator <<;

    storage.clear();
    for(const auto position : status_positions)
    {
        storage.seekp(position, storage.beg);
        storage << yar::db::metadata{yar::db::metadata::created};
    }
    storage.flush();
    const auto statuses_restored = not storage.fail();

    storage.close();
    auto resize_error = std::error_code{};
    std::filesystem::resize_file(db, original_size, resize_error);
    const auto reopened = reopen(storage, db);
    return statuses_restored and not resize_error and reopened;
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

bool yar::db::engine::consume_write_failure()
{
    if(m_writes_until_failure == 0)
        return false;
    return --m_writes_until_failure == 0;
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
            rebuilt_index.insert(document, metadata.position);
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
    auto metadata = yar::db::metadata{std::string{collection}};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    index.update(document);
    m_storage << metadata << document;
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    m_storage.flush();
    if(m_storage.fail())
    {
        if(not rollback(m_storage, m_db, original_size, {}))
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
            if(skip > 0)
            {
                --skip;
                continue;
            }

            documents += std::move(document);
            success = true;
            if(--top == 0)
                break;
        }
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
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    m_storage.flush();
    if(m_storage.fail())
    {
        if(not rollback(m_storage, m_db, original_size, {}))
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
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    m_storage.flush();
    if(m_storage.fail())
    {
        if(not rollback(m_storage, m_db, original_size, status_positions))
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

            documents += std::move(document);
            positions.push_back(position);

            if(--top == 0)
                break;
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
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    m_storage.flush();
    if(m_storage.fail())
    {
        if(not rollback(m_storage, m_db, original_size, positions))
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
        }
    }

    if(positions.empty())
        return 0;

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
    staged_index.update(document);

    auto metadata = yar::db::metadata{std::string{collection}};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    m_storage << metadata << document;
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    m_storage.flush();
    if(m_storage.fail())
    {
        if(not rollback(m_storage, m_db, original_size, {}))
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

    for(const auto position : positions)
    {
        m_storage.clear();
        m_storage.seekp(position, m_storage.beg);
        m_storage << yar::db::deleted;
    }
    if(consume_write_failure())
        m_storage.setstate(std::ios::badbit);
    m_storage.flush();
    if(m_storage.fail())
    {
        if(not rollback(m_storage, m_db, original_size, positions))
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
