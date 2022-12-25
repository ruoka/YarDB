#include <span>
#include <iostream>
#include <fstream>
#include "db/metadata.hpp"
#include "xson/fson.hpp"
#include "xson/json.hpp"

using namespace std;
using namespace string_literals;
using namespace xson;
using namespace ext;

const auto usage = R"(yarexport [--help] [--file=<name>])";

int main(int argc, char** argv)
try
{
    const auto arguments = span(argv,argc).subspan(1);
    auto file = "yar.db"s;

    for(const string_view option : arguments)
    {
        if(option.starts_with("--file"))
        {
            file = option.substr(option.find('=')+1);
        }
        else if(option.starts_with("--help"))
        {
            clog << usage << endl;
            return 0;
        }
        else if(option.starts_with("-"))
        {
            clog << usage << endl;
            return 1;
        }
    }

    auto storage = ifstream{file};

    if(!storage.is_open())
        throw runtime_error{"file "s + file + " not found"s};

    while(storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        storage >> metadata >> document;
        if(storage)
            clog << json::stringify(
                            {{"collection"s, metadata.collection            },
                             {"status"s,     to_string(metadata.status)     },
                             {"timestamp"s,  to_iso8601(metadata.timestamp) },
                             {"position"s,   metadata.position              },
                             {"previous"s,   metadata.previous              },
                             {"document"s,   document                       }})
                      << ",\n";
    }
}
catch(const system_error& e)
{
    cerr << "System error with code " << e.code() << " " << e.what() << endl;
    return 1;
}
catch(const exception& e)
{
    cerr << "Exception " << e.what() << endl;
    return 1;
}
catch(...)
{
    cerr << "Shit hit the fan!" << endl;
    return 1;
}
