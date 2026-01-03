#include <iostream>
#include "deps/xson/xson/xson.c++m"

int main() {
    auto obj = xson::json::parse("[1, 2, 3]");
    std::cout << "Parsed object type: " << (obj.is_array() ? "array" : "not array") << std::endl;
    std::cout << "Size: " << obj.size() << std::endl;
    if (obj.is_array()) {
        for (size_t i = 0; i < obj.size(); ++i) {
            auto val = obj[i];
            std::cout << "Element " << i << ": " << (val.is_integer() ? "integer" : "not integer") << std::endl;
            if (val.is_integer()) {
                std::cout << "Value: " << static_cast<xson::integer_type>(val) << std::endl;
            }
        }
    }
    return 0;
}
