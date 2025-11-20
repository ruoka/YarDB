import yar;
import net;
import std;

using namespace std;
using namespace chrono;
using namespace ext;
using namespace net;

const auto usage = R"(yarproxy [--help] [--clog] [--slog_tag=<tag>] [--slog_level=<level>] --replica=<URL> [service_or_port])";

using replica = endpointstream;

using replica_set = lockable<list<replica>>;

inline auto& operator >> (istream& is, ostream& os)
{
    auto request_line = ""s;
    auto headers = http::headers{};

    getline(is,request_line,'\r') >> ws >> headers >> crlf;
    os << request_line << crlf << headers << crlf;

    auto content_length = headers.contains("content-length") ? std::stoll(headers["content-length"]) : 0ull;

    while(content_length and is and os)
    {
        os.put(is.get());
        --content_length;
    }

    os << flush;
    return is;
}

inline void handle(auto& client, auto& replicas)
{
    auto& [stream,endpoint,port] = client;

    slog << notice << "Accepted connection from " << endpoint << ":" << port << flush;

    auto buffer = stringstream{};

    auto request_and_response = [&buffer](replica& connection) {
        buffer.seekg(0) >> connection;
        connection >> buffer.seekp(0);
        return connection.good();
    };

    auto request = [&buffer](replica& connection) {
        buffer.seekg(0) >> connection;
        return connection.good();
    };

    auto response = [&buffer](replica& connection) {
        connection >> buffer.seekp(0);
        return connection.good();
    };

    auto disconnected = [](const replica& connection) {
        return not connection.good();
    };

    while(stream >> buffer)
    {
        auto method = ""s;
        buffer.seekg(0) >> method;

        if(method == "GET"s || method == "HEAD"s)
        {
            const auto guard = std::lock_guard{replicas};
            [[maybe_unused]] auto r1 = ranges::any_of(replicas, request_and_response);
            [[maybe_unused]] auto r2 = ranges::rotate(replicas, ++ranges::begin(replicas));
        }
        else
        {
            const auto guard = std::lock_guard{replicas};
            [[maybe_unused]] auto r1 = ranges::all_of(replicas, request);
            [[maybe_unused]] auto r2 = ranges::all_of(replicas, response);
        }

        buffer.seekg(0) >> stream;
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
    slog.appname("YarPROXY");
    slog.level(net::syslog::severity::debug);

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

        if(option.starts_with("--slog_tag="))
        {
            auto tag = option.substr(string_view{"--slog_tag="}.size());
            slog.appname(tag);
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
            slog.level(mask);
            continue;
        }

        if(option.starts_with("--replica="))
        {
            auto replica_url = option.substr(string_view{"--replica="}.size());
            replicas.emplace_back(connect(replica_url));
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
        std::thread{[client = std::move(client), &replicas]() mutable {handle(client,replicas);}}.detach();
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
    slog << error << "Shit hit the fan!" << flush;
    return 1;
}
