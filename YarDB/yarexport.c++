import yar;
import std;
import xson;

using namespace std;
using namespace xson;

const auto usage = R"(
yarexport [--help] [--file=<name>] [--live]

Export FSON database records to JSONL on stdout (one JSON object per line).
Stop yardb before exporting the same file to avoid concurrent read issues.

  --live   Export only current (status=created) documents for offline compaction.
           Omits history/tombstones and file positions. Pair with yarimport.
)";

int main(int argc, char** argv)
try
{
    using xson::fson::operator >>;

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

        if(live_only)
        {
            if(metadata.status != yar::db::metadata::created)
                continue;

            cout << json::stringify(
                            {{"collection"s, metadata.collection},
                             {"document"s,   document           }},
                            0)
                 << '\n';
            continue;
        }

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
