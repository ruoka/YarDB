import yar;
import net;
import std;

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

    getline(is,request_line,'\r') >> ws >> headers >> crlf;
    os << request_line << crlf << headers << crlf;

    auto content_length = headers.contains("content-length") ? std::stoll(headers["content-length"]) : 0ull;

    while(content_length && is && os)
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

    auto request = [&buffer](auto& replica) {
        buffer.seekg(0) >> replica;
        return replica.good();
    };

    auto response = [&buffer](auto& replica) {
        replica >> buffer.seekp(0);
        return replica.good();
    };

    auto request_and_response = [&buffer](auto& replica) {
        buffer.seekg(0) >> replica;
        replica >> buffer.seekp(0);
        return replica.good();
    };

    auto disconnected = [](auto& replica) {
        return !replica.good();
    };

    while(stream >> buffer)
    {
        auto method = ""s;
        buffer.seekg(0) >> method;

        if(method == "GET"s || method == "HEAD"s)
        {
            const auto lock = ext::make_lock(replicas);
            any_of(begin(replicas), end(replicas), request_and_response);
            rotate(begin(replicas), ++begin(replicas), end(replicas));
        }
        else
        {
            const auto lock = ext::make_lock(replicas);
            all_of(begin(replicas), end(replicas), request);
            all_of(begin(replicas), end(replicas), response);
        }

        buffer.seekg(0) >> stream;
        buffer.seekp(0);
    }

    const auto lock = ext::make_lock(replicas);
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

    for(string_view option : arguments)
    {
        if(option.starts_with("--clog"))
        {
            slog.redirect(clog);
        }
        else if(option.starts_with("--slog_tag="))
        {
            option.remove_prefix(option.find_first_not_of("--slog_tag="));
            slog.tag(option);
        }
        else if(option.starts_with("--slog_level="))
        {
            option.remove_prefix(option.find_first_not_of("--slog_level="));
            auto mask = 0;
            auto [ptr,ec] = std::from_chars(option.begin(),option.end(),mask);
            if(ec != std::errc() or ptr != option.end())
            {
                slog << error << " invalid syslog mask --slog_level=" << option << flush;
                return 1;
            }
            slog.level(mask);
        }
        else if(option.starts_with("--replica="))
        {
            option.remove_prefix(option.find_first_not_of("--replica="));
            replicas.emplace_back(connect(option));
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