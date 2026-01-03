#include <iostream>
#include "deps/xson/xson/xson.c++m"

int main() {
    // Create a simple array
    xson::object arr_obj{xson::array_type{xson::object{1}, xson::object{2}, xson::object{3}}};
    
    std::cout << "Array object is_array: " << arr_obj.is_array() << std::endl;
    std::cout << "Array size: " << arr_obj.size() << std::endl;
    
    // Test JSON stringify
    auto json = xson::json::stringify(arr_obj);
    std::cout << "JSON: " << json << std::endl;
    
    // Test parse back
    auto parsed = xson::json::parse(json);
    std::cout << "Parsed is_array: " << parsed.is_array() << std::endl;
    if (parsed.is_array()) {
        std::cout << "Parsed size: " << parsed.size() << std::endl;
        for (size_t i = 0; i < parsed.size(); ++i) {
            std::cout << "Element " << i << " is_integer: " << parsed[i].is_integer();
            if (parsed[i].is_integer()) {
                std::cout << " value: " << static_cast<xson::integer_type>(parsed[i]);
            }
            std::cout << std::endl;
        }
    }
    
    return 0;
}
