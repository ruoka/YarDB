#include <unistd.h>

import yar;
import std;
import xson;

using namespace std;
using namespace xson;

const auto usage = R"(
yarimport [--help] [--file=<name>] [--input=<path>] [--force]

Import live JSONL (from yarexport --live) into a new FSON database file.
Stop yardb before compacting; write to a new file, then swap.

  --file=<name>    Output database path (default: yar.db)
  --input=<path>   JSONL input (default: stdin)
  --force          Overwrite an existing non-empty output file

Each input line must be JSON with "collection" and "document".
Lines with status "updated" or "deleted" are rejected.
Secondary indexes are restored from live "_db" rows via engine.index().

Import always builds a unique temporary sidecar first and only replaces
--file after a successful create/index/reindex pass, so a failed --force
run leaves any existing database intact. Refuses to run when --file.pid
exists (stop yardb first). Without --force, install uses a hard link so an
appearing target cannot be clobbered.
)";

struct import_row
{
    string collection;
    object document;
};

auto trim(string_view text)
{
    while(not text.empty() and isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while(not text.empty() and isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

auto parse_import_row(string_view line, size_t line_no)
{
    auto parsed = json::parse(line);
    if(not parsed.has("collection"s) or not parsed.has("document"s))
        throw runtime_error{"line "s + to_string(line_no) + ": expected collection and document fields"s};

    if(parsed.has("status"s))
    {
        const auto status = static_cast<string>(parsed["status"s]);
        if(status == "updated"s or status == "deleted"s)
            throw runtime_error{
                "line "s + to_string(line_no)
                + ": refusing to import status="s + status
                + "; use yarexport --live"s};
    }

    return import_row{
        static_cast<string>(parsed["collection"s]),
        parsed["document"s]};
}

auto keys_from_db_document(const object& document, size_t line_no)
{
    if(not document.has("collection"s) or not document.has("keys"s))
        throw runtime_error{
            "line "s + to_string(line_no)
            + ": _db document requires collection and keys fields"s};

    auto keys = vector<string>{};
    for(const auto& key : document["keys"s].get<object::array>())
    {
        if(not key.is_string())
            throw runtime_error{"line "s + to_string(line_no) + ": _db keys must be strings"s};
        keys.push_back(static_cast<string>(key));
    }
    if(keys.empty())
        throw runtime_error{"line "s + to_string(line_no) + ": _db keys must not be empty"s};

    return pair{static_cast<string>(document["collection"s]), std::move(keys)};
}

struct temporary_database
{
    filesystem::path path;

    explicit temporary_database(filesystem::path target)
        : path{std::move(target)}
    {
        error_code ec{};
        filesystem::remove(path, ec);
        filesystem::remove(path.string() + ".pid"s, ec);
    }

    temporary_database(const temporary_database&) = delete;
    temporary_database& operator=(const temporary_database&) = delete;

    ~temporary_database()
    {
        if(path.empty())
            return;

        error_code ec{};
        filesystem::remove(path, ec);
        filesystem::remove(path.string() + ".pid"s, ec);
    }

    void release()
    {
        path.clear();
    }
};

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv, argc).subspan(1);
    auto file = "yar.db"s;
    auto input_path = ""s;
    auto force = false;

    for(const string_view option : arguments)
    {
        if(option == "--help")
        {
            clog << usage << endl;
            return 0;
        }

        if(option == "--force")
        {
            force = true;
            continue;
        }

        if(option.starts_with("--file="))
        {
            file = string{option.substr(string_view{"--file="}.size())};
            continue;
        }

        if(option.starts_with("--input="))
        {
            input_path = string{option.substr(string_view{"--input="}.size())};
            continue;
        }

        if(option.starts_with("-"))
        {
            cerr << "Error: unknown option " << option << endl;
            cerr << usage << endl;
            return 1;
        }
    }

    if(file.empty())
        throw runtime_error{"--file must not be empty"s};

    auto input_file = ifstream{};
    istream* input = &cin;
    if(not input_path.empty())
    {
        input_file.open(input_path);
        if(not input_file.is_open())
            throw runtime_error{"input file not found: "s + input_path};
        input = &input_file;
    }

    auto data_rows = vector<import_row>{};
    auto db_rows = vector<pair<size_t, import_row>>{};
    auto line_no = size_t{0};
    auto line = ""s;
    while(getline(*input, line))
    {
        ++line_no;
        if(trim(line).empty())
            continue;

        auto row = parse_import_row(line, line_no);
        if(row.collection == "_db"s)
            db_rows.emplace_back(line_no, std::move(row));
        else
            data_rows.push_back(std::move(row));
    }
    if(input->bad() or (input->fail() and not input->eof()))
        throw runtime_error{"failed reading import input"s};

    error_code ec{};
    const auto lock_path = file + ".pid"s;
    if(filesystem::exists(lock_path, ec))
        throw runtime_error{
            "database lock present: "s + lock_path
            + "; stop yardb before importing (remove a stale lock only after verifying no live owner)"s};

    const auto target_exists =
        filesystem::exists(file, ec) and filesystem::file_size(file, ec) > 0;
    if(target_exists and not force)
        throw runtime_error{
            "output file already exists: "s + file + "; use --force to overwrite"s};

    // Per-process staging name avoids concurrent imports deleting each other's sidecar/.pid.
    auto staging = temporary_database{
        filesystem::path{file + ".yarimport."s + to_string(getpid()) + ".tmp"s}};
    {
        auto engine = yar::db::engine{staging.path.string()};

        for(auto& row : data_rows)
        {
            const auto created = engine.create(row.collection, row.document);
            if(not created)
                throw runtime_error{
                    "create failed for collection "s + row.collection + ": "s + created.error().message};
        }

        for(auto& [source_line, row] : db_rows)
        {
            auto [collection_name, keys] = keys_from_db_document(row.document, source_line);
            const auto indexed = engine.index(collection_name, std::move(keys));
            if(not indexed)
                throw runtime_error{
                    "index failed for collection "s + collection_name + ": "s + indexed.error().message};
        }

        const auto reindexed = engine.reindex();
        if(not reindexed)
            throw runtime_error{"reindex failed: "s + reindexed.error().message};
    }

    // Re-check lock immediately before install — yardb may have started during import.
    if(filesystem::exists(lock_path, ec))
        throw runtime_error{
            "database lock appeared during import: "s + lock_path
            + "; stop yardb and retry (staging left uninstalled)"s};

    if(force)
    {
        filesystem::rename(staging.path, file, ec);
        if(ec)
            throw system_error{ec, "failed to replace output database "s + file};
    }
    else
    {
        // Hard-link install refuses to replace a target that appeared after the
        // earlier exists check (rename would silently clobber it).
        filesystem::create_hard_link(staging.path, file, ec);
        if(ec)
            throw system_error{
                ec,
                "failed to install output database "s + file
                    + " (does the file already exist? use --force to overwrite)"s};
        // Best-effort unlink of the staging path; `file` already links the inode.
        filesystem::remove(staging.path, ec);
    }
    staging.release();

    clog << "Imported " << data_rows.size() << " document(s) and "
         << db_rows.size() << " index configuration(s) into " << file << endl;
}
catch(const system_error& e)
{
    cerr << "System error with code " << e.code() << " aka " << quoted(e.what()) << endl;
    return 1;
}
catch(const exception& e)
{
    cerr << "Exception " << quoted(e.what()) << endl;
    return 1;
}
catch(...)
{
    cerr << "Unexpected error occurred" << endl;
    return 1;
}
