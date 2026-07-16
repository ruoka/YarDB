#include <csignal>
import yar;
import net;
import cryptic;
import std;

using namespace std;
using namespace net;

const auto usage = R"(
yardb [--help] [--clog] [--slog_level=<level>] [--file=<name>] [--bind=<host>] [--pat=<token>] [--pat-file=<path>] [--admin-pat=<token>] [--admin-pat-file=<path>] [service_or_port]

Listen address (default: 127.0.0.1):
  --bind=<host>       Bind host (default 127.0.0.1). Use --bind=0.0.0.0 for Docker/port-forwarding.
                      Binding to 0.0.0.0 or :: requires --pat or --pat-file.

Optional PAT authentication (Bearer token):
  --pat=<token>            Accept a data-API personal access token (repeatable)
  --pat-file=<path>        Load data-API tokens from a file (one per line; # comments allowed)
                           Lines may be plaintext tokens or sha256:<hex> pre-hashed values.
  --admin-pat=<token>      Accept an admin-API token for /_* maintenance routes (repeatable)
  --admin-pat-file=<path>  Load admin-API tokens from a file (same format as --pat-file)
When data PATs are configured, ordinary API routes require Authorization: Bearer <token>
(except GET /health, /ready, /metrics). When admin PATs are configured, /_* routes require an
admin token; otherwise /_* uses the data PAT.
)";

// Global atomic flag for shutdown request (async-signal-safe)
static std::atomic<bool> g_shutdown_requested{false};

// Signal handler - async-signal-safe: only sets flag
extern "C" void signal_handler(int)
{
    g_shutdown_requested.store(true, std::memory_order_release);
}

namespace {

using pat_hash = string;

auto trim(string_view text) -> string
{
    while(not text.empty() and isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while(not text.empty() and isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return string{text};
}

void load_pat_file(const string& path, vector<string>& tokens)
{
    auto input = ifstream{path};
    if(not input.is_open())
        throw runtime_error{"PAT file not found: "s + path};

    auto line = ""s;
    while(getline(input, line))
    {
        const auto trimmed = trim(line);
        if(trimmed.empty() or trimmed.starts_with('#'))
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

auto is_hex_digit(char ch) -> bool
{
    return (ch >= '0' and ch <= '9')
        or (ch >= 'a' and ch <= 'f')
        or (ch >= 'A' and ch <= 'F');
}

auto is_sha256_hex(string_view text) -> bool
{
    if(text.size() != 64)
        return false;
    return ranges::all_of(text, is_hex_digit);
}

auto to_lower_hex(string_view text) -> string
{
    auto result = string{text};
    for(auto& ch : result)
    {
        if(ch >= 'A' and ch <= 'F')
            ch = static_cast<char>(ch - 'A' + 'a');
    }
    return result;
}

auto hash_pat_bearer(string_view bearer_auth) -> pat_hash
{
    return cryptic::sha256::hexadecimal(bearer_auth);
}

auto parse_pat_hash(string_view raw) -> pat_hash
{
    if(raw.empty())
        throw runtime_error{"PAT token must not be empty"};

    if(raw.starts_with("sha256:"))
    {
        const auto digest = trim(raw.substr(string_view{"sha256:"}.size()));
        if(not is_sha256_hex(digest))
            throw runtime_error{"PAT sha256 digest must be 64 hexadecimal characters"};
        return to_lower_hex(digest);
    }

    return hash_pat_bearer(normalize_bearer_auth(raw));
}

void append_hashed_pats(vector<string>& raw_tokens, set<pat_hash>& hashed_values)
{
    for(const auto& raw : raw_tokens)
        hashed_values.insert(parse_pat_hash(raw));
}

auto secure_equals(string_view left, string_view right) -> bool
{
    if(left.size() != right.size())
        return false;

    auto diff = 0u;
    for(auto i = 0uz; i < left.size(); ++i)
        diff |= static_cast<unsigned>(left[i] ^ right[i]);
    return diff == 0u;
}

auto validate_hashed_pat(const set<pat_hash>& hashed_values, string_view authorization) -> bool
{
    const auto candidate = hash_pat_bearer(authorization);
    for(const auto& stored : hashed_values)
    {
        if(secure_equals(candidate, stored))
            return true;
    }
    return false;
}

auto is_public_bind(string_view host) -> bool
{
    const auto normalized = trim(host);
    return normalized == "0.0.0.0"sv
        or normalized == "::"sv
        or normalized == "::0"sv
        or normalized == "[::]"sv;
}

void validate_bind_policy(string_view bind_host, bool has_pat)
{
    if(is_public_bind(bind_host) and not has_pat)
        throw runtime_error{
            "refusing to bind to "s + string{bind_host}
            + " without PAT authentication; use --pat or --pat-file, or bind to 127.0.0.1"};
}

} // namespace

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto bind_host = "127.0.0.1"s;
    auto service_or_port = "2112"s;
    auto raw_pats = vector<string>{};
    auto pat_files = vector<string>{};
    auto raw_admin_pats = vector<string>{};
    auto admin_pat_files = vector<string>{};

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

        if(option.starts_with("--bind="))
        {
            bind_host = string{option.substr(string_view{"--bind="}.size())};
            if(bind_host.empty())
            {
                cerr << "Error: --bind requires a host address" << endl;
                cerr << usage << endl;
                return 1;
            }
            continue;
        }

        if(option.starts_with("--admin-pat-file="))
        {
            admin_pat_files.push_back(string{option.substr(string_view{"--admin-pat-file="}.size())});
            continue;
        }

        if(option.starts_with("--admin-pat="))
        {
            raw_admin_pats.push_back(string{option.substr(string_view{"--admin-pat="}.size())});
            continue;
        }

        if(option.starts_with("--pat-file="))
        {
            pat_files.push_back(string{option.substr(string_view{"--pat-file="}.size())});
            continue;
        }

        if(option.starts_with("--pat="))
        {
            raw_pats.push_back(string{option.substr(string_view{"--pat="}.size())});
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
    for(const auto& pat_file : admin_pat_files)
        load_pat_file(pat_file, raw_admin_pats);

    const auto has_pat = not raw_pats.empty();
    validate_bind_policy(bind_host, has_pat);

    auto hashed_pats = set<pat_hash>{};
    auto hashed_admin_pats = set<pat_hash>{};
    append_hashed_pats(raw_pats, hashed_pats);
    append_hashed_pats(raw_admin_pats, hashed_admin_pats);

    slog << notice << "Starting up server on " << bind_host << ":" << service_or_port << flush;
    auto server = yar::http::rest_api_server{file, service_or_port, bind_host};

    if(not hashed_pats.empty() or not hashed_admin_pats.empty())
    {
        auto valid_hashes = make_shared<set<pat_hash>>(std::move(hashed_pats));
        auto valid_admin_hashes = make_shared<set<pat_hash>>(std::move(hashed_admin_pats));

        std::function<bool(string_view)> validate_data{};
        if(not valid_hashes->empty())
        {
            validate_data = [valid_hashes](string_view authorization) {
                return validate_hashed_pat(*valid_hashes, authorization);
            };
        }

        std::function<bool(string_view)> validate_admin{};
        if(not valid_admin_hashes->empty())
        {
            validate_admin = [valid_admin_hashes](string_view authorization) {
                return validate_hashed_pat(*valid_admin_hashes, authorization);
            };
        }

        server.configure_authentication(
            yar::http::details::is_public_api_path,
            std::move(validate_data),
            "YarDB API"sv,
            std::move(validate_admin)
        );

        if(not valid_hashes->empty())
            slog << notice << "PAT authentication enabled (" << valid_hashes->size() << " hashed token(s))" << flush;
        if(not valid_admin_hashes->empty())
            slog << notice << "Admin PAT authentication enabled (" << valid_admin_hashes->size()
                 << " hashed token(s) for /_* routes)" << flush;
    }

    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    server.start();

    slog << notice << "Server started, waiting for shutdown signal" << flush;

    while(not g_shutdown_requested.load(std::memory_order_acquire))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    slog << notice << "Shutdown signal received, draining server" << flush;
    server.drain();
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