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
    file << net::syslog::getpid() << std::endl;
    locks.emplace(lock);
    std::atexit(unlock);
}

// Helper template to extract metadata value from first matching document
template<typename T, typename Extractor>
auto get_metadata_value_impl(std::fstream& storage, const yar::db::index_view& view, const yar::db::object& selector, Extractor extractor) -> std::optional<T>
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
    m_storage{std::move(e.m_storage)}
{}

yar::db::engine::~engine()
{
    ::unlock(m_db);
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

        auto& index = m_index[metadata.collection];

        // Update sequence counter for all documents
        index.update(document);

        // Process _db collection documents to set up secondary keys for other collections
        if(metadata.collection == "_db"s)
        {
            auto collection = document["collection"s];
            auto keys = document["keys"s];
            auto temp = std::vector<std::string>{};
            for(const auto& k : keys.get<yar::db::object::array>())
                temp.push_back(k);

            m_index[collection].add(temp);
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

void yar::db::engine::reindex()
{
    populate_indexes();
}

void yar::db::engine::index(std::vector<std::string> keys)
{
    auto& index = m_index[m_collection];
    index.add(keys);
    auto selector = yar::db::object{"collection"s, m_collection};
    auto document = yar::db::object{selector, {"keys"s, index.keys()}};
    auto collection = "_db"s;
    std::swap(collection, m_collection);
    upsert(selector, document);
    std::swap(collection, m_collection);
};

bool yar::db::engine::create(yar::db::object& document)
{
    using xson::fson::operator <<;

    auto& index = m_index[m_collection];
    auto metadata = yar::db::metadata{m_collection};
    m_storage.clear();
    m_storage.seekp(0, m_storage.end);
    index.update(document);
    m_storage << metadata << document;
    m_storage.flush();
    if(m_storage.fail())
        return false;
    
    // Insert into index using the position that was set by metadata operator<<
    // (which is the start position of the metadata record where data is written)
    index.insert(document, metadata.position);
    return true;
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

std::optional<std::chrono::system_clock::time_point> yar::db::engine::get_metadata_timestamp(const yar::db::object& selector) const
{
    const auto it = m_index.find(m_collection);
    if(it == m_index.end())
        return std::nullopt;
    const auto& index = it->second;
    return get_metadata_value_impl<std::chrono::system_clock::time_point>(
        m_storage, index.view(selector), selector, [](const yar::db::metadata& m) { return m.timestamp; });
}

std::optional<std::int64_t> yar::db::engine::get_metadata_position(const yar::db::object& selector) const
{
    const auto it = m_index.find(m_collection);
    if(it == m_index.end())
        return std::nullopt;
    const auto& index = it->second;
    return get_metadata_value_impl<std::int64_t>(
        m_storage, index.view(selector), selector, [](const yar::db::metadata& m) { return m.position; });
}

bool yar::db::engine::update(const yar::db::object& selector, const yar::db::object& updates, yar::db::object& documents)
{
    using xson::fson::operator >>;
    using xson::fson::operator <<;

    documents = yar::db::object{yar::db::object::array{}};
    auto success = false;
    auto& index = m_index[m_collection];

    for(const auto position : index.view(selector))
    {
        using namespace xson::fson;

        auto metadata = yar::db::metadata{};
        auto old_document = yar::db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> old_document;
        if(old_document.match(selector))
        {
            m_storage.clear();
            m_storage.seekp(position, m_storage.beg);
            m_storage << yar::db::updated;

            auto new_document = updates;
            new_document += std::move(old_document);

            m_storage.clear();
            m_storage.seekp(0, m_storage.end);
            index.update(new_document);
            index.insert(new_document, m_storage.tellp());
            m_storage << metadata << new_document;

            documents += std::move(new_document);
            success = true;
        }
    }

    if(success)
        m_storage.flush();

    return success;
}

bool yar::db::engine::destroy(const yar::db::object& selector, yar::db::object& documents)
{
    using xson::fson::operator >>;

    documents = yar::db::object{yar::db::object::array{}};
    auto top = std::numeric_limits<yar::db::sequence_type>::max();
    if(selector.has("$top"s))
        top = selector["$top"s];

    auto success = false;
    auto& index = m_index[m_collection];

    for(const auto position : index.view(selector))
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        m_storage.clear();
        m_storage.seekg(position, m_storage.beg);
        m_storage >> metadata >> document;
        if(document.match(selector))
        {
            m_storage.clear();
            m_storage.seekp(position, m_storage.beg);
            m_storage << yar::db::deleted;
            documents += std::move(document);
            success = true;

            if(--top == 0) break;
        }
    }

    if(success)
        m_storage.flush();

    for(const auto& document : documents.get<yar::db::object::array>())
        index.erase(document);

    return success;
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
