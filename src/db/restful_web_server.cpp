#include <mutex>
#include "db/engine.hpp"
#include "http/server.hpp"
#include "http/uri.hpp"
#include "xson/json.hpp"
#include "std/lockable.hpp"

namespace db {

using namespace std::string_literals;

void restful_web_server(const std::string& file, const std::string& port_or_service)
{
    auto server = http::server{};

    auto engine = ext::lockable<db::engine>{file};

    // Create
    server.post("/[a-z]+").response(
        "application/json"s,
        [&](const std::string& request, const std::string& body)
            {
                auto uri = http::uri{request};
                auto stream = std::stringstream{body};
                auto document = xson::json::parse(stream);

                const auto lock = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.create(document);
                return xson::json::stringify(document);
            });

    // Read
    server.get("/[a-z]+").response(
        "application/json"s,
        [&](const std::string& request, const std::string& body)
            {
                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{};

                const auto lock = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Read
    server.get("/[a-z]+\\?desc").response(
        "application/json"s,
        [&](const std::string& request, const std::string& body)
            {
                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{"$desc", true};

                const auto lock = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Update = replace
    server.put("/[a-z]+/[0-9]+").response(
        "application/json"s,
        [&](const std::string& request, const std::string& body)
            {
                auto uri = http::uri{request};
                auto stream = std::stringstream{body};
                auto document = xson::json::parse(stream);
                auto selector = db::object{"_id",ext::stoll(uri.path[2])};

                const auto lock = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.replace(selector,document);
                return xson::json::stringify(document);
            });

    // Update = modify
    server.patch("/[a-z]+/[0-9]+").response(
        "application/json"s,
        [&](const std::string& request, const std::string& body)
            {
                auto uri = http::uri{request};
                auto stream = std::stringstream{body};
                auto updates = xson::json::parse(stream);
                auto documents = db::object{};
                auto selector = db::object{"_id",ext::stoll(uri.path[2])};

                const auto lock = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.upsert(selector,updates,documents);
                return xson::json::stringify(documents);
            });

    // Delete
    server.destroy("/[a-z]+/[0-9]+").response(
        "application/json"s,
        [&](const std::string& request, const std::string& body)
            {
                auto uri = http::uri{request};
                auto stream = std::stringstream{body};
                auto document = xson::json::parse(stream);
                auto selector = db::object{"_id",ext::stoll(uri.path[2])};

                const auto lock = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.destroy(selector,document);
                return xson::json::stringify(document);
            });

    server.listen(port_or_service);
}

} // namespace db
