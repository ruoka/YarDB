#include <thread>
#include <mutex>
#include <span>
#include "net/connector.hpp"
#include "net/acceptor.hpp"
#include "net/syslogstream.hpp"
#include "http/headers.hpp"
#include "std/lockable.hpp"

using namespace std;
using namespace string_literals;
using namespace chrono_literals;
using namespace ext;
using namespace net;

const auto usage = R"(yarproxy [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] --replica=<URL> [service_or_port])";

using replica_set = lockable<list<endpointstream>>;

inline auto& operator >> (istream& is, ostream& os)
{
    auto request_line = ""s;
    auto headers = http::headers{};

    getline(is,request_line) >> ws >> headers >> crlf;

    slog << info << "Received \"" << request_line << "\"" << flush;

    os << request_line << crlf << headers << crlf;

    slog << info << "Forwarded \"" << request_line << "\"" << flush;

    auto content_length = headers.contains("content-length") ? std::stoll(headers["content-length"]) : 0ull;

    while(content_length && is && os)
    {
        slog << debug << "Body \"" << static_cast<char>(is.peek()) << "\"" << flush;
        os.put(is.get());
        --content_length;
    }

    os << flush;
    return is;
}

void handle(endpointstream client, replica_set& replicas)
{
    auto buffer = stringstream{};

    auto request = [&buffer](auto& replica)->bool {
        buffer.seekg(0) >> replica;
        return replica.good();
    };

    auto response = [&buffer](auto& replica)->bool {
        replica >> buffer.seekp(0);
        return replica.good();
    };

    auto request_and_response = [&buffer](auto& replica)->bool {
        buffer.seekg(0) >> replica;
        replica >> buffer.seekp(0);
        return replica.good();
    };

    auto disconnected = [](auto& replica)->bool {
        return !replica.good();
    };

    while(client >> buffer)
    {
        auto method = ""s;
        buffer.seekg(0) >> method;

        if(method == "GET"s || method == "HEAD"s)
        {
            const auto lock = lock_guard(replicas);
            any_of(begin(replicas), end(replicas), request_and_response);
            rotate(begin(replicas), ++begin(replicas), end(replicas));
        }
        else
        {
            const auto lock = lock_guard(replicas);
            all_of(begin(replicas), end(replicas), request);
            all_of(begin(replicas), end(replicas), response);
        }
        buffer.seekg(0) >> client;
        buffer.seekp(0);
    }

    const auto lock = lock_guard(replicas);
    replicas.remove_if(disconnected);
}

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto replicas = replica_set{};
    auto service_or_port = "2113"s;
    slog.tag("YarPROXY");
    slog.level(net::syslog::severity::debug);

    for(const string_view option : arguments)
    {
        if(option.starts_with("--clog"))
        {
            slog.redirect(clog);
        }
        else if(option.starts_with("--slog_tag="))
        {
            const auto name = option.substr(option.find('=')+1);
            slog.tag(name);
        }
        else if(option.starts_with("--slog_level="))
        {
            const auto mask = stol(option.substr(option.find('=')+1));
            slog.level(mask);
        }
        else if(option.starts_with("--replica"))
        {
            const auto url = option.substr(option.find('=')+1);
            replicas.emplace_back(connect(url));
        }
        else if(option.starts_with("--help"))
        {
            clog << usage << endl;
            return 0;
        }
        else if(option.starts_with("-"))
        {
            cerr << usage << endl;
            return 1;
        }
        else
        {
            service_or_port = option;
        }
    }

    if(replicas.empty())
    {
        cerr << usage << endl;
        return 1;
    }

    slog << notice << "Starting up at " << service_or_port << flush;
    auto endpoint = net::acceptor{service_or_port};
    endpoint.timeout(24h);
    slog << info << "Started up at "s + endpoint.host() + ":" + endpoint.service_or_port() << flush;

    while(true)
    {
        slog << notice << "Accepting connections" << flush;
        auto client = endpoint.accept();
        slog << notice << "Accepted connection from " << std::get<1>(client) << ":" << std::get<2>(client) << flush;
        std::thread{[&client,&replicas](){handle(move(std::get<0>(client)),replicas);}}.detach();
        std::this_thread::sleep_for(1ms);
    }
}
catch(const system_error& e)
{
    slog << error << "System error with code " << e.code() << " " << e.what() << flush;
    return 1;
}
catch(const exception& e)
{
    slog << error << "Exception " << e.what() << flush;
    return 1;
}
catch(...)
{
    slog << error << "Shit hit the fan!" << flush;
    return 1;
}
