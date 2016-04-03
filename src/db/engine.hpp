#pragma once

#include <fstream>
#include "db/index.hpp"

namespace db {

class engine
{
public:

    engine(const std::string file = "./fson.db"s);

    void collection(const std::string& col)
    {
        m_collection = col;
    }

    void create(db::object& document);

    void read(db::object& selector, std::vector<db::object>& result);

    void update(db::object& selector, db::object& document);

    void destroy(db::object& selector);

private:

    void rebuild_indexes();

    std::string m_collection;

    db::index m_index;

    mutable std::fstream m_storage;
};

} // namespace db
