module yar;
import :metadata;
import std;
import net;
import xson;

namespace {

using namespace std::string_literals;

auto locks = std::set<std::string>{};

inline void unlock()
{
    for(const auto& lock : locks)
        std::remove(lock.c_str());
    locks.clear();
}

inline void unlock(std::string_view db)
{
    const auto lock = std::string{db} + ".pid"s;
    std::remove(lock.c_str());
    locks.erase(lock);
}

inline void lock(std::string_view db)
{
    const auto lock = std::string{db} + ".pid"s;
    auto file = std::fstream{lock, std::ios::in};
    if(file.is_open())
    {
        auto pid = ""s;
        file >> pid;
        throw std::runtime_error{"DB "s + lock + " is already in use by PID "s + pid};
    }
    file.open(lock, std::ios::out | std::ios::trunc);
    if(!file.is_open())
        throw std::runtime_error{"Failed to create DB lock "s + lock};
    file << net::posix::getpid() << std::endl;
    locks.emplace(lock);
    std::atexit(unlock);
}

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
    return statuses_restored && not resize_error && reopened;
}

// Helper template to extract metadata value from first matching document
template<typename T, typename Extractor>
auto metadata_value(std::fstream& storage, const yar::db::index_view& view, const yar::db::object& selector, Extractor extractor) -> std::optional<T>
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
        {
            return extractor(metadata);
        }
    }
    
    return std::nullopt; // No matching document found
}

} // namespace

yar::db::engine::engine(std::string_view db) :
    m_db{db},
    m_collection{"_db"s},
    m_index{},
    m_storage{}
{
    ::lock(m_db);
    m_storage.open(m_db, std::ios::out | std::ios::in | std::ios::binary);
    if(!m_storage.is_open())
        m_storage.open(m_db, std::ios::out | std::ios::in | std::ios::binary | std::ios::trunc);
    if(!m_storage.is_open())
        throw std::runtime_error{"Failed to open/create DB "s + m_db};
    setup_index_structure();
    populate_indexes();
}

yar::db::engine::engine(yar::db::engine&& e) :
    m_db{std::move(e.m_db)},
    m_collection{std::move(e.m_collection)},
    m_index{std::move(e.m_index)},
    m_storage{std::move(e.m_storage)},
    m_writes_until_failure{std::exchange(e.m_writes_until_failure, 0)},
    m_writable{std::exchange(e.m_writable, false)}
{}

yar::db::engine::~engine()
{
    ::unlock(m_db);
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

// First pass: Set up index structure by discovering secondary keys from _db collection
// and updating sequence counters for all documents
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

        // Update sequence counter for all documents
        m_index[metadata.collection].update(document);

        // Process _db collection documents to set up secondary keys for other collections
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

// Second pass: Populate indexes with document positions
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

        // Skip deleted or updated documents (they're not in the current index)
        if(metadata.status == metadata::deleted || metadata.status == metadata::updated)
            continue;

        auto& index = m_index[metadata.collection];
        index.insert(document, metadata.position);
    }
}

yar::db::db_result<> yar::db::engine::reindex()
{
    using xson::fson::operator >>;

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
        if(metadata.status != metadata::deleted && metadata.status != metadata::updated)
            rebuilt_index.insert(document, metadata.position);
    }
    m_storage.clear();
    m_index = std::move(rebuilt);
    return {};
}

std::vector<std::string> yar::db::engine::indexed_keys() const
{
    const auto it = m_index.find(m_collection);
    if(it == m_index.end())
        return {};
    return it->second.keys();
}

yar::db::db_result<> yar::db::engine::index(std::vector<std::string> keys)
{
    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::index,
            "Database writes are disabled after an unsuccessful rollback"s)};

    auto original = m_index;
    auto& current_index = m_index[m_collection];
    current_index.add(keys);
    auto selector = yar::db::object{"collection"s, m_collection};
    auto document = yar::db::object{selector, {"keys"s, current_index.keys()}};
    const auto collection = m_collection;
    m_collection = "_db"s;
    auto result = upsert(selector, document);
    m_collection = collection;
    if(not result)
    {
        m_index = std::move(original);
        return std::unexpected{db_error(
            result.error().code,
            db_operation::index,
            result.error().message)};
    }
    return {};
};

yar::db::db_result<> yar::db::engine::create(yar::db::object& document)
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
    auto& index = staged[m_collection];
    auto metadata = yar::db::metadata{m_collection};
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
    
    // Insert into index using the position that was set by metadata operator<<
    // (which is the start position of the metadata record where data is written)
    index.insert(document, metadata.position);
    m_index = std::move(staged);
    return {};
}

std::size_t yar::db::engine::count(const yar::db::object& selector) const
{
    const auto it = m_index.find(m_collection);
    if(it == m_index.end())
        return 0;

    return it->second.count(m_storage, selector);
}

bool yar::db::engine::read(const yar::db::object& selector, yar::db::object& documents)
{
    using xson::fson::operator >>;

    documents = yar::db::object{yar::db::object::array{}};
    auto top = std::numeric_limits<yar::db::sequence_type>::max();
    if(selector.has("$top"s))
        top = selector["$top"s];

    auto skip = sequence_type{0};
    if(selector.has("$skip"s))
        skip = selector["$skip"s];

    auto success = false;
    const auto& index = m_index[m_collection];

    for(const auto position : index.view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(document.match(selector))
        {
            // Skip the first N matching documents
            if(skip > 0)
            {
                --skip;
                continue;
            }

            documents += std::move(document);
            success = true;
            if(--top == 0) break;
        }
    }

    return success;
}

std::optional<std::chrono::system_clock::time_point> yar::db::engine::metadata_timestamp(const yar::db::object& selector) const
{
    const auto it = m_index.find(m_collection);
    if(it == m_index.end())
        return std::nullopt;
    const auto& index = it->second;
    return metadata_value<std::chrono::system_clock::time_point>(
        m_storage, index.view(selector), selector, [](const yar::db::metadata& m) { return m.timestamp; });
}

std::optional<std::int64_t> yar::db::engine::metadata_position(const yar::db::object& selector) const
{
    const auto it = m_index.find(m_collection);
    if(it == m_index.end())
        return std::nullopt;
    const auto& index = it->second;
    return metadata_value<std::int64_t>(
        m_storage, index.view(selector), selector, [](const yar::db::metadata& m) { return m.position; });
}

yar::db::db_result<std::size_t> yar::db::engine::update(
    const yar::db::object& selector,
    const yar::db::object& updates,
    yar::db::object& documents)
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
    const auto& index = m_index[m_collection];

    for(const auto position : index.view(selector))
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
    auto& staged_index = staged[m_collection];
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
    const yar::db::object& selector,
    yar::db::object& documents)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

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
    const auto& index = m_index[m_collection];

    for(const auto position : index.view(selector))
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
            documents += std::move(document);
            positions.push_back(position);

            if(--top == 0) break;
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
    auto& staged_index = staged[m_collection];
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
    const yar::db::object& selector,
    yar::db::object& document)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

    if(not m_writable)
        return std::unexpected{db_error(
            db_error_code::rollback_failure,
            db_operation::replace,
            "Database writes are disabled after an unsuccessful rollback"s)};

    auto positions = std::vector<position_type>{};
    auto old_documents = std::vector<object>{};
    const auto& index = m_index[m_collection];
    for(const auto position : index.view(selector))
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
    auto& staged_index = staged[m_collection];
    for(const auto& old_document : old_documents)
        staged_index.erase(old_document);
    staged_index.update(document);

    auto metadata = yar::db::metadata{m_collection};
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

bool yar::db::engine::history(const yar::db::object& selector, yar::db::object& documents)
{
    using xson::fson::operator >>;

    documents = yar::db::object{yar::db::object::array{}};
    auto success = false;
    const auto& index = m_index[m_collection];

    for(auto position : index.view(selector))
        while(position >= 0)
        {
            auto metadata = yar::db::metadata{};
            auto document = yar::db::object{};
            m_storage.clear();
            m_storage.seekg(position, m_storage.beg);
            m_storage >> metadata >> document;
            position = metadata.previous;
            documents += std::move(document);
            success = true;
        }

    return success;
}
