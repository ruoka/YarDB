#include <mutex>
#include "db/engine.hpp"
#include "http/server.hpp"
#include "http/uri.hpp"
#include "xson/json.hpp"
#include "std/lockable.hpp"

namespace db {

using namespace std::string_literals;
using namespace std::string_view_literals;

void restful_web_server(std::string_view file, const std::string_view port_or_service)
{
    auto server = http::server{};

    auto engine = ext::lockable<db::engine>{file};

    // List all collections
    server.get("/"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                const auto guard = std::lock_guard(engine);
                return xson::json::stringify({"collections", engine.collections()});
            });

    // Create
    server.post("/[a-z]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto document = xson::json::parse(body);

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.create(document);
                return xson::json::stringify(document);
            });

    // Read all
    server.get("/[a-z]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{};

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Read all in descending order
    server.get("/[a-z]+\\?desc"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{"$desc", true};

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Read _id
    server.get("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{"_id", ext::stoll(uri.path[2])};

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Update aka replace _id
    server.put("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto document = xson::json::parse(body);
                auto selector = db::object{"_id",ext::stoll(uri.path[2])};

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.replace(selector,document);
                return xson::json::stringify(document);
            });

    // Update aka modify _id
    server.patch("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto updates = xson::json::parse(body);
                auto documents = db::object{};
                auto selector = db::object{"_id",ext::stoll(uri.path[2])};

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.upsert(selector,updates,documents);
                return xson::json::stringify(documents);
            });

    // Delete _id
    server.destroy("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body)
            {
                auto uri = http::uri{request};
                auto documents = db::object{};
                auto selector = db::object{"_id",ext::stoll(uri.path[2])};

                const auto guard = std::lock_guard(engine);
                engine.collection(uri.path[1]);
                engine.destroy(selector,documents);
                return xson::json::stringify(documents);
            });

    server.listen(port_or_service);
}

} // namespace db
