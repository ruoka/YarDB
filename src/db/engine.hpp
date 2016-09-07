#pragma once

#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string file = "./yar.db"s);

    void index(std::vector<std::string> keys);

    void reindex();

    bool create(db::object& document);

    bool read(const db::object& selector, db::object& documents);

    bool update(const db::object& selector, const db::object& updates, bool upsert = false);

    bool upsert(const db::object& selector, const db::object& updates);

    bool replace(const db::object& selector, db::object& updates);

    bool destroy(const db::object& selector);

    bool history(const db::object& selector, db::object& documents);

    void dump();

public:

    const auto& collection() const {return m_collection;};

    void collection(const std::string& in_use) {m_collection = in_use;}

private:

    std::string m_collection;

    std::map<std::string,db::index> m_index;

    std::fstream m_storage;
};

} // namespace db
