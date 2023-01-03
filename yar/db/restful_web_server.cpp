#include <mutex>
#include <charconv>
#include "db/engine.hpp"
#include "http/server.hpp"
#include "http/uri.hpp"
#include "std/lockable.hpp"
#include "xson/json.hpp"

// https://www.odata.org/odata-services/

namespace db {

using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace net;

inline auto stoll(std::string_view sv)
{
    auto ll = 0ll;
    std::from_chars(sv.begin(),sv.end(),ll);
    return ll;
}

void restful_web_server(std::string_view file, const std::string_view port_or_service)
{
    auto server = http::server{};

    auto engine = ext::lockable<db::engine>{file};

    // List all collections
    server.get("/"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "GET /: "<< request << flush;

                const auto guard = std::lock_guard{engine};
                return xson::json::stringify({"collections", engine.collections()});
            });

    // Create
    server.post("/[a-z]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "POST /[a-z]+: "<< request << " <- " << body << flush;

                auto uri = http::uri{request};
                auto document = xson::json::parse(body);

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.create(document);
                return xson::json::stringify(document);
            });

    // Read _id
    server.get("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "GET /[a-z]+/[0-9]+: " << request << flush;

                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{"_id", stoll(uri.path[2])};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });


    // Read all is ascending order
    server.get("/[a-z]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "GET /[a-z]+: " << request << flush;

                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Read first
    server.get("/[a-z]+\\?\\$top"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "GET /[a-z]+\\?\\$top: "<< request << flush;

                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{"$top",1ll};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Read all in descending order
    server.get("/[a-z]+\\?\\$desc"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "GET /[a-z]+\\?\\$desc: "<< request << flush;

                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{"$desc",true};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Read last
    server.get("/[a-z]+\\?\\$desc&\\$top"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "GET /[a-z]+\\$desc&\\$top: "<< request << flush;

                auto uri = http::uri{request};
                auto document = db::object{};
                auto selector = db::object{{"$desc",true},{"$top",1ll}};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.read(selector, document);
                return xson::json::stringify(document);
            });

    // Update aka replace _id
    server.put("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "PUT /[a-z]+: "<< request << " <- " << body << flush;

                auto uri = http::uri{request};
                auto document = xson::json::parse(body);
                auto selector = db::object{"_id",stoll(uri.path[2])};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.replace(selector,document);
                return xson::json::stringify(document);
            });

    // Update aka modify _id
    server.patch("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "PATCH /[a-z]+: "<< request << " <- " << body << flush;

                auto uri = http::uri{request};
                auto updates = xson::json::parse(body);
                auto documents = db::object{};
                auto selector = db::object{"_id",stoll(uri.path[2])};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.upsert(selector,updates,documents);
                return xson::json::stringify(documents);
            });

    // Delete _id
    server.destroy("/[a-z]+/[0-9]+"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "DELETE /[a-z]+/[0-9]+: " << request << flush;

                auto uri = http::uri{request};
                auto documents = db::object{};
                auto selector = db::object{"_id",stoll(uri.path[2])};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.destroy(selector,documents);
                return xson::json::stringify(documents);
            });

    // Delete first
    server.destroy("/[a-z]+\\?\\$top"s).response(
        "application/json"sv,
        [&engine](std::string_view request, [[maybe_unused]] std::string_view body, [[maybe_unused]] const http::headers& headers)
            {
                slog << debug << "DELETE /[a-z]+?\\$top: " << request << flush;

                auto uri = http::uri{request};
                auto documents = db::object{};
                auto selector = db::object{"$top",1ll};

                const auto guard = std::lock_guard{engine};
                engine.collection(uri.path[1]);
                engine.destroy(selector,documents);
                return xson::json::stringify(documents);
            });

    server.listen(port_or_service);
}

} // namespace db
