
#include <iostream>
#include <fstream>
#include "db/metadata.hpp"
#include "xson/json.hpp"

using namespace std;
using namespace string_literals;
using namespace xson;

const auto usage = R"(
Currently supported options commands are:
)";

int main(int, char**)
try
{
    ifstream storage{"./yar.db"};

    if(!storage.is_open())
        throw runtime_error{"file "s + "./yar.db" + " not found"s};

    while(storage)
    {
        auto metadata = db::metadata{};
        auto document = db::object{};
        storage >> metadata >> document;
        if(storage)
            std::clog << json::stringify(
                            {{"collection"s, metadata.collection         },
                             {"status"s,     to_string(metadata.status)  },
                             {"position"s,   metadata.position           },
                             {"previous"s,   metadata.previous           },
                             {"document"s,   document                    }})
                      << ",\n";
    }
}
catch(const std::exception& e)
{
    cerr << "Error: " << e.what() << endl;
    return 1;
}
catch(...)
{
    cerr << "Shit hit the fan!" << endl;
    return 1;
}
