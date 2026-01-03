// test_xson_debug.cpp - Debug program for xson:object issues
import xson.json;

int main() {
    xson::object obj;
    obj["key1"] = 42;  // scalar int
    obj["key2"] = "value";  // string
    obj["key3"] = true;  // bool

    // Nested object
    obj["nested"] = xson::object{};
    obj["nested"]["subkey"] = 3.14;

    // Array
    obj["array"] = xson::object{"arr", {1, 2, 3}};

    // Using initializer list
    xson::object init_list = { xson::object{"init1", "val1"}, xson::object{"init2", 100} };

    // Merge
    obj += init_list;

    // Pretty print
    std::cout << xson::json::stringify(obj, 2) << std::endl;

    return 0;
}
