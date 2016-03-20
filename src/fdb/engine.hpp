#pragma once

#include <fstream>
#include "xson/fson.hpp"

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

    bool create(const fdb::object& document);

    bool read(const fdb::object& selector, std::vector<fdb::object>& result);

    bool update(const fdb::object& selector, const fdb::object& document);

    bool destroy(const fdb::object& selector);

private:

    std::string m_database;

    std::string m_collection;

    std::fstream m_storage;
};

}
