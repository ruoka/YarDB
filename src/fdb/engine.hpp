#pragma once

#include <fstream>
#include "fdb/index.hpp"

namespace fdb {

using object = xson::fson::object;

class engine
{
public:

    engine();

    void database(const std::string& db)
    {
        m_database = db;
    }

    void collection(const std::string& col)
    {
        m_collection = col;
    }

    void create(fdb::object& document);

    void read(fdb::object& selector, std::vector<fdb::object>& result);

    void update(fdb::object& selector, fdb::object& document);

    void destroy(fdb::object& selector);

private:

    std::string m_database;

    std::string m_collection;

    fdb::index m_index;

    mutable std::fstream m_storage;
};

}
