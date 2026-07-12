#include <csignal>
import yar;
import net;
import std;

using namespace std;
using namespace net;

const auto usage = R"(
yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [--pat=<token>] [--pat-file=<path>] [service_or_port]

Optional PAT authentication (Bearer token):
  --pat=<token>       Accept a personal access token (repeatable)
  --pat-file=<path>   Load tokens from a file (one per line; # comments allowed)
When any PAT is configured, all API routes require Authorization: Bearer <token>.
)";

// Global atomic flag for shutdown request (async-signal-safe)
static std::atomic<bool> g_shutdown_requested{false};

// Signal handler - async-signal-safe: only sets flag
extern "C" void signal_handler(int)
{
    g_shutdown_requested.store(true, std::memory_order_release);
}

namespace {

auto trim(string_view text) -> string
{
    while(!text.empty() && isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while(!text.empty() && isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return string{text};
}

void load_pat_file(const string& path, vector<string>& tokens)
{
    auto input = ifstream{path};
    if(!input.is_open())
        throw runtime_error{"PAT file not found: "s + path};

    auto line = ""s;
    while(getline(input, line))
    {
        const auto trimmed = trim(line);
        if(trimmed.empty() || trimmed.starts_with('#'))
            continue;
        tokens.push_back(trimmed);
    }
}

auto normalize_bearer_auth(string_view token) -> string
{
    if(token.starts_with("Bearer "))
        return string{token};
    return "Bearer "s + string{token};
}

void append_normalized_pats(vector<string>& raw_tokens, set<string>& bearer_values)
{
    for(const auto& raw : raw_tokens)
    {
        if(raw.empty())
            throw runtime_error{"PAT token must not be empty"};
        bearer_values.insert(normalize_bearer_auth(raw));
    }
}

} // namespace

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto service_or_port = "2112"s;
    auto raw_pats = vector<string>{};
    auto pat_files = vector<string>{};

    slog.app_name("yardb")
        .log_level(net::syslog::severity::debug)
        .format(net::log_format::jsonl);

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

        if(option.starts_with("--file="))
        {
            file = option.substr(string_view{"--file="}.size());
            continue;
        }

        if(option.starts_with("--pat="))
        {
            raw_pats.push_back(string{option.substr(string_view{"--pat="}.size())});
            continue;
        }

        if(option.starts_with("--pat-file="))
        {
            pat_files.push_back(string{option.substr(string_view{"--pat-file="}.size())});
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

    for(const auto& pat_file : pat_files)
        load_pat_file(pat_file, raw_pats);

    auto bearer_pats = set<string>{};
    append_normalized_pats(raw_pats, bearer_pats);

    slog << notice << "Starting up server" << flush;
    auto server = yar::http::rest_api_server{file, service_or_port};

    if(!bearer_pats.empty())
    {
        auto valid_tokens = make_shared<set<string>>(std::move(bearer_pats));
        server.configure_authentication(
            [](string_view) { return false; },
            [valid_tokens](string_view authorization) {
                return valid_tokens->contains(string{authorization});
            },
            "YarDB API"sv
        );
        slog << notice << "PAT authentication enabled (" << valid_tokens->size() << " token(s))" << flush;
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    server.start();

    slog << notice << "Server started, waiting for shutdown signal" << flush;

    while(!g_shutdown_requested.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    slog << notice << "Shutdown signal received, stopping server" << flush;
    server.stop();

    slog << notice << "Server stopped, exiting" << flush;
    return 0;
}
catch(const system_error& e)
{
    slog << error << "System error with code " << e.code() << " aka " << quoted(e.what()) << flush;
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