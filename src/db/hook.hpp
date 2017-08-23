#pragma once

#include "db/engine.hpp"
#include "net/syslogstream.hpp"

using namespace net;
using namespace xson;

namespace db {

class hook
{
public:

    hook(engine& e) : m_engine{e}
    {}

    void pre_post(object& new_document)
    try
    {
        if(m_engine.collection() == "transfers")
        {
            auto selector = db::object{
                {"safekeepingAccount", new_document["safekeepingAccount"s]},
                {"isin", new_document["isin"s]},
                {"$desc"s,""s},
                {"$top"s,1ll}
            };
            auto prev_documents = xson::array_type{};
            auto quantity = xson::integer_type{0},
                 balance = xson::integer_type{0};
            m_engine.read(selector, prev_documents);
            slog << debug << json::stringify(prev_documents) << flush;
            if(new_document.has("quantity"))
                quantity = new_document["quantity"s];
            if(new_document.match({"movementType"s, "DELI"s}))
                quantity = -quantity;
            if(prev_documents.count() > 0)
                balance = prev_documents[0]["balance"s];
            new_document["balance"s] = balance + quantity;
            slog << debug << json::stringify(prev_documents) << flush;
        }
    }
    catch(const std::exception& e)
    {
        slog << error << e.what() << flush;
    }

    void post_post(object& ob)
    {}

private:

    engine& m_engine;
};

} // namespace db
