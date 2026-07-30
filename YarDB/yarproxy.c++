// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

import yar;
import net;
import std;

using namespace std;
using namespace chrono;
using namespace utils;
using namespace net;

const auto usage = R"(
yarproxy [--help] [--clog] [--slog_level=<level>] --replica=<URL> [service_or_port]

HTTP fan-out proxy for local testing (not built-in replication or HA).
GET/HEAD: round-robin to one yardb backend per request.
POST/PUT/PATCH/DELETE: best-effort fan-out to every --replica backend;
client sees one response (from the last backend). Backends use separate DB files.

Forwards end-to-end headers (Authorization, X-Correlation-ID, etc.) to backends.
Rewrites Host per backend and strips hop-by-hop headers (Connection, etc.).
)";

struct replica_backend
{
    endpointstream connection;
    string host_header;
    string url; // reconnect after unframed (SSE) responses poison keep-alive
};

struct replica_set : list<replica_backend>, mutex
{
    using list::list;
};

inline auto host_header_from_url(string_view replica_url) -> string
{
    // RFC 7230 Host is uri-host [ ":" port ] with a numeric port only.
    // When the replica URL omits a port, uri.port is empty — do not fall back
    // to the URI scheme (getaddrinfo service name). That produced illegal
    // values like Host: localhost:http (same class as net websocket host_header).
    const auto url = uri{replica_url};
    auto host = string{url.host};
    const auto port = string{url.port};
    if(not port.empty()
       and ranges::all_of(port, [](unsigned char c) {
               return std::isdigit(c) != 0;
           }))
        host += ':' + port;
    return host;
}

inline void copy_http_body(istream& is, ostream& os, const http::headers& hdrs, bool skip_body = false)
{
    // HEAD responses advertise Content-Length for the would-be GET body but
    // send no octets (net::http::server). Waiting for those bytes hangs the
    // keep-alive backend socket — and yarproxy holds the replica mutex while
    // reading — so skip body transfer for HEAD.
    if(skip_body)
    {
        os << flush;
        return;
    }

    // Requests and ordinary yardb responses always frame with Content-Length.
    // Missing length means no body for client→proxy requests; do not treat it
    // as a streaming response here (see read_backend_response).
    auto content_length = hdrs.contains("content-length"s) ? stoll(hdrs["content-length"s]) : 0ll;

    while(content_length > 0 and is and os)
    {
        os.put(is.get());
        --content_length;
    }

    os << flush;
}

inline void read_http_message(istream& is, ostream& os, bool skip_body = false)
{
    auto request_line = ""s;
    auto headers = http::headers{};

    getline(is, request_line, '\r') >> ws >> headers >> crlf;
    os << request_line << crlf << headers << crlf;
    copy_http_body(is, os, headers, skip_body);
}

// Read a backend response. Returns false when the body is unframed (no
// Content-Length) and was not skipped — e.g. MCP GET /sse text/event-stream.
// Caller must not reuse the keep-alive socket; unread SSE bytes would make the
// next status-line read hang while holding the replica mutex.
inline auto read_backend_response(istream& is, ostream& os, bool skip_body = false) -> bool
{
    auto status_line = ""s;
    auto headers = http::headers{};

    getline(is, status_line, '\r') >> ws >> headers >> crlf;
    os << status_line << crlf << headers << crlf;

    if(skip_body)
    {
        os << flush;
        return true;
    }

    if(not headers.contains("content-length"s))
    {
        os << flush;
        return false;
    }

    auto content_length = stoll(headers["content-length"s]);
    while(content_length > 0 and is and os)
    {
        os.put(is.get());
        --content_length;
    }
    os << flush;
    return true;
}

inline void reconnect_replica(replica_backend& replica)
{
    replica.connection.close();
    try
    {
        replica.connection = connect(replica.url);
    }
    catch(...)
    {
        // Leave closed; remove_if drops it when the client handler exits.
    }
}

inline auto strip_hop_by_hop(http::headers hdrs) -> http::headers
{
    static constexpr array hop_by_hop = {
        "connection"s, "proxy-connection"s, "keep-alive"s,
        "proxy-authenticate"s, "proxy-authorization"s, "te"s,
        "trailers"s, "transfer-encoding"s, "upgrade"s
    };

    const auto strip_names = flat_set<string>{hop_by_hop.begin(), hop_by_hop.end()};
    auto out = http::headers{};
    for(const auto& [name, value] : hdrs)
        if(not strip_names.contains(name))
            out.set(name, value);
    return out;
}

inline void forward_request(istream& is, ostream& os, string_view backend_host)
{
    auto request_line = ""s;
    auto headers = http::headers{};

    getline(is, request_line, '\r') >> ws >> headers >> crlf;
    headers = strip_hop_by_hop(std::move(headers));
    headers.set("host"s, string{backend_host});
    os << request_line << crlf << headers << crlf;
    copy_http_body(is, os, headers);
}

inline void handle(auto& client, auto& replicas)
{
    auto& [stream,endpoint,port] = client;

    slog << notice << "Accepted connection from " << endpoint << ":" << port << flush;

    auto buffer = stringstream{};

    auto request_and_response = [&buffer](replica_backend& replica, bool skip_body) {
        buffer.seekg(0);
        forward_request(buffer, replica.connection, replica.host_header);
        if(not read_backend_response(replica.connection, buffer.seekp(0), skip_body))
        {
            // Unframed streaming body (SSE). Close + reconnect so unread
            // event-stream bytes cannot poison the pooled keep-alive socket.
            reconnect_replica(replica);
            return false;
        }
        return replica.connection.good();
    };

    auto request = [&buffer](replica_backend& replica) {
        buffer.seekg(0);
        forward_request(buffer, replica.connection, replica.host_header);
        return replica.connection.good();
    };

    auto response = [&buffer](replica_backend& replica) {
        if(not read_backend_response(replica.connection, buffer.seekp(0)))
        {
            reconnect_replica(replica);
            return false;
        }
        return replica.connection.good();
    };

    auto disconnected = [](const replica_backend& replica) {
        return not replica.connection.good();
    };

    auto send_bad_gateway = [&stream] {
        stream << "HTTP/1.1 502 Bad Gateway" << crlf
               << "Content-Length: 0" << crlf
               << "Connection: close" << crlf
               << crlf << flush;
    };

    while(stream.good() and stream.peek() != char_traits<char>::eof())
    {
        buffer.str(""s);
        buffer.clear();
        read_http_message(stream, buffer);

        auto method = ""s;
        buffer.seekg(0) >> method;

        {
            const auto guard = std::lock_guard{replicas};
            // remove_if at connection end can leave an empty replica list after
            // every backend dies. ++begin on empty is UB; forwarding an empty
            // buffer also echoed the client request as a forged response.
            if(replicas.empty())
            {
                send_bad_gateway();
                break;
            }

            if(method == "GET"s or method == "HEAD"s)
            {
                const auto skip_body = method == "HEAD"s;
                if(not ranges::any_of(replicas, [&](replica_backend& replica) {
                        return request_and_response(replica, skip_body);
                    }))
                {
                    // All backends failed but may still look connected until
                    // remove_if runs — do not echo the buffered client request.
                    send_bad_gateway();
                    break;
                }
                // middle==end is valid for size==1 (no-op rotate).
                [[maybe_unused]] auto rotated = ranges::rotate(
                    replicas, ranges::next(ranges::begin(replicas)));
            }
            else
            {
                if(not ranges::all_of(replicas, request))
                {
                    send_bad_gateway();
                    break;
                }
                [[maybe_unused]] auto responded = ranges::all_of(replicas, response);
            }
        }

        buffer.seekg(0);
        read_http_message(buffer, stream, method == "HEAD"s);
        buffer.seekp(0);
    }

    const auto guard = std::lock_guard{replicas};
    replicas.remove_if(disconnected);
}

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto replicas = replica_set{};
    auto service_or_port = "2113"s;
    slog.app_name("yarproxy")
        .log_level(net::syslog::severity::debug);

    for(string_view option : arguments)
    {
        if(option == "--help")
        {
            clog << usage << endl;
            return 0;
        }

        if(option == "--clog")
        {
            slog.redirect(clog);
            continue;
        }

        if(option.starts_with("--slog_level="))
        {
            auto level_str = option.substr(string_view{"--slog_level="}.size());
            auto mask = 0u;
            auto [ptr,ec] = std::from_chars(level_str.begin(), level_str.end(), mask);
            if(ec != std::errc() or ptr != level_str.end())
            {
                cerr << "Error: invalid syslog mask --slog_level=" << level_str << endl;
                cerr << usage << endl;
                return 1;
            }
            slog.log_level(static_cast<net::syslog::severity>(mask));
            continue;
        }

        if(option.starts_with("--replica="))
        {
            auto replica_url = option.substr(string_view{"--replica="}.size());
            replicas.emplace_back(replica_backend{
                connect(replica_url),
                host_header_from_url(replica_url),
                string{replica_url}
            });
            continue;
        }

        if(option.starts_with("-"))
        {
            cerr << "Error: unknown option " << option << endl;
            cerr << usage << endl;
            return 1;
        }

        service_or_port = option;
    }

    if(replicas.empty())
    {
        cerr << usage << endl;
        return 1;
    }

    slog << info << "Starting up at "s << service_or_port << flush;
    auto endpoint = net::acceptor{service_or_port};
    endpoint.timeout(24h);
    slog << info << "Started up at "s << endpoint.host() << ":" << endpoint.service_or_port() << flush;

    while(true)
    {
        slog << notice << "Accepting connections" << flush;
        auto client = endpoint.accept();
        std::thread{
            [client = std::move(client), &replicas]() mutable {
                try
                {
                    handle(client, replicas);
                }
                catch(const system_error& e)
                {
                    slog << error("yarproxy") << "Thread system error with code " << e.code() << " " << quoted(e.what()) << flush;
                }
                catch(const exception& e)
                {
                    slog << error("yarproxy") << "Thread exception: " << quoted(e.what()) << flush;
                }
                catch(...)
                {
                    slog << error("yarproxy") << "Unexpected error in connection handler thread" << flush;
                }
            }
        }.detach();
    }
}
catch(const system_error& e)
{
    slog << error << "System error with code " << e.code() << " " << quoted(e.what()) << flush;
    return 1;
}
catch(const exception& e)
{
    slog << error << "Exception " << quoted(e.what()) << flush;
    return 1;
}
catch(...)
{
    slog << error << "Unexpected error occurred" << flush;
    return 1;
}
