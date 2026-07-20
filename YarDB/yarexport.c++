// Copyright (c) 2025-2026 Kaius Ruokonen. All rights reserved.
// SPDX-License-Identifier: MIT
// See the LICENSE file in the project root for full license text.

import yar;
import std;
import xson;

using namespace std;
using namespace xson;

const auto usage = R"(
yarexport [--help] [--file=<name>] [--live]

Export FSON database records to JSONL on stdout (one JSON object per line).
Claims --file.pid for the duration of a history export so yardb cannot open
the database mid-scan; refuses to run when the lock already exists.

  --live   Export only current documents for offline compaction.
           Opens via the database engine so dual-live crash windows are
           healed to a single current version per _id.
           Omits history/tombstones and file positions. Pair with yarimport.
)";

auto require_no_live_lock(const string& file)
{
    error_code lock_ec{};
    const auto lock_path = file + ".pid"s;
    if(filesystem::exists(lock_path, lock_ec))
        throw runtime_error{
            "database lock present: "s + lock_path
            + "; stop yardb before exporting (remove a stale lock only after verifying no live owner)"s};
}

// Exclusive {file}.pid for raw history export. Existence checks alone are a
// TOCTOU gap: yardb can start after the check and append while we scan.
class history_export_lock
{
public:
    explicit history_export_lock(const string& file) :
        m_path{file + ".pid"s}
    {
        auto lock_file = ofstream{m_path, ios::out | ios::noreplace};
        if(not lock_file.is_open())
            throw runtime_error{
                "database lock present: "s + m_path
                + "; stop yardb before exporting (remove a stale lock only after verifying no live owner)"s};
        lock_file << chrono::system_clock::now().time_since_epoch().count() << '\n';
        lock_file.flush();
        if(lock_file.fail())
        {
            error_code ec{};
            filesystem::remove(m_path, ec);
            throw runtime_error{"failed to initialize export lock: "s + m_path};
        }
        m_owns_lock = true;
    }

    history_export_lock(const history_export_lock&) = delete;
    history_export_lock& operator=(const history_export_lock&) = delete;

    ~history_export_lock()
    {
        if(not exchange(m_owns_lock, false))
            return;
        error_code ec{};
        filesystem::remove(m_path, ec);
    }

private:
    string m_path;
    bool m_owns_lock = false;
};

auto export_live(const string& file)
{
    error_code exists_ec{};
    if(not filesystem::exists(file, exists_ec))
        throw runtime_error{"file "s + file + " not found"s};

    // Engine open heals dual-live crash windows (two status=created rows for
    // one _id) before reads, matching yardb reopen semantics. A raw status
    // scan would export stale pre-images and break yarimport compaction.
    auto engine = yar::db::engine{file};
    for(const auto& collection : engine.collections())
    {
        auto documents = object{};
        if(not engine.read(collection, object{}, documents))
            continue;

        for(const auto& document : documents.get<object::array>())
        {
            cout << json::stringify(
                            {{"collection"s, collection},
                             {"document"s,   document  }},
                            0)
                 << '\n';
        }
    }
}

auto export_history(const string& file)
{
    using xson::fson::operator >>;

    auto storage = ifstream{file, ios::binary};
    if(not storage.is_open())
        throw runtime_error{"file "s + file + " not found"s};

    while(storage)
    {
        auto metadata = yar::db::metadata{};
        auto document = yar::db::object{};
        storage >> metadata >> document;
        if(not storage)
            break;

        cout << json::stringify(
                        {{"collection"s, metadata.collection                  },
                         {"status"s,     to_string(metadata.status)           },
                         {"timestamp"s,  xson::to_iso8601(metadata.timestamp) },
                         {"position"s,   metadata.position                    },
                         {"previous"s,   metadata.previous                    },
                         {"document"s,   document                             }},
                        0)
             << '\n';
    }
}

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;
    auto live_only = false;

    for(const string_view option : arguments)
    {
        if(option == "--help")
        {
            clog << usage << endl;
            return 0;
        }

        if(option == "--live")
        {
            live_only = true;
            continue;
        }

        if(option.starts_with("--file="))
        {
            file = option.substr(string_view{"--file="}.size());
            continue;
        }

        if(option.starts_with("-"))
        {
            cerr << "Error: unknown option " << option << endl;
            cerr << usage << endl;
            return 1;
        }
    }

    if(live_only)
    {
        // Engine open claims .pid; fail closed if yardb wins the race.
        require_no_live_lock(file);
        export_live(file);
    }
    else
    {
        // Hold .pid across the raw scan so yardb cannot append mid-export.
        auto lock = history_export_lock{file};
        export_history(file);
    }

    // Compaction/import trusts a successful exit. A full disk or broken pipe
    // must not look like a complete export.
    cout.flush();
    if(not cout)
        throw runtime_error{"failed writing export to stdout"s};
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
