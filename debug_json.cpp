#include <iostream>
#include "deps/xson/xson/xson.c++m"

int main() {
    auto obj = xson::object{
        {"LuckyNumbers", xson::array_type{xson::object{2}, xson::object{22}, xson::object{2112}}}
    };
    
    auto json_str = xson::json::stringify(obj, 2);
    std::cout << "JSON output:" << std::endl << json_str << std::endl;
    
    auto parsed = xson::json::parse(json_str);
    std::cout << "Parsed LuckyNumbers is_array: " << parsed["LuckyNumbers"].is_array() << std::endl;
    if (parsed["LuckyNumbers"].is_array()) {
        std::cout << "Size: " << parsed["LuckyNumbers"].size() << std::endl;
    }
    return 0;
}
