#include <thread>
#include <mutex>
#include <span>
#include "net/connector.hpp"
#include "net/acceptor.hpp"
#include "net/syslogstream.hpp"
#include "std/lockable.hpp"

using namespace std;
using namespace string_literals;
using namespace chrono_literals;
using namespace this_thread;
using namespace ext;
using namespace net;

const auto usage = R"(yardb [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] --replica=<URL> [service_or_port])";

using replica_set = lockable<list<endpointstream>>;

inline auto& operator >> (istream& is, ostream& os)
{
    auto line = ""s;
    auto content_length = 0u;
    getline(is, line);
    trim(line);
    slog << debug << line << flush;
    os << line << crlf;
    is >> ws;
    while(is && is.peek() != '\r')
    {
        auto header = ""s, value = ""s;
        getline(is, header, ':');
        trim(header);
        getline(is, value);
        trim(value);
        slog << debug << header << ": " << value << flush;
        os << header << ": " << value << crlf;
        if(header == "Content-Length"s)
            content_length = stoul(value);
    }
    is.ignore(2);
    os << crlf;
    while(content_length && is && os)
    {
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

    slog << notice << "Starting up at "s + service_or_port << flush;
    auto endpoint = net::acceptor{service_or_port};
    endpoint.timeout(24h);
    slog << info << "Started up at "s + endpoint.host() + ":" + endpoint.service_or_port() << flush;

    while(true)
    {
        slog << notice << "Accepting connections" << flush;
        auto host = ""s, port = ""s;
        auto client = endpoint.accept(host, port);
        slog << info << "Accepted connection from "s + host + ":" + port << flush;
        auto worker = thread{[&client,&replicas](){handle(move(client), replicas);}};
        sleep_for(1s);
        worker.detach();
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
