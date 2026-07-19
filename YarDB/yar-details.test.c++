module yar;
import :details;
import tester;
import std;
import net;

namespace yar::details_unit_test {

using namespace std;
using namespace std::string_literals;
using namespace std::string_view_literals;
using namespace tester::basic;
using namespace tester::assertions;
using namespace yar::http::details;

auto register_details_tests()
{
    using namespace tester::bdd;

    scenario("quote-aware scan helpers split and find outside literals, [yardb]") = []
    {
        given("Text with delimiters inside quoted strings") = []
        {
            when("split_outside_quotes splits on and") = []
            {
                then("Keeps and inside string literals") = []
                {
                    const auto parts = split_outside_quotes(
                        "name eq 'Smith and Sons' and city eq 'Boston'"sv, " and "sv)
                        | std::ranges::to<std::vector<std::string_view>>();
                    require_eq(parts.size(), 2u);
                    require_eq(parts[0], "name eq 'Smith and Sons'"sv);
                    require_eq(parts[1], "city eq 'Boston'"sv);
                };
            };

            when("split_outside_quotes splits on comma") = []
            {
                then("Keeps commas inside quoted list values") = []
                {
                    const auto parts = split_outside_quotes("'active,pending','done'"sv, ","sv)
                        | std::ranges::to<std::vector<std::string_view>>();
                    require_eq(parts.size(), 2u);
                    require_eq(parts[0], "'active,pending'"sv);
                    require_eq(parts[1], "'done'"sv);
                };
            };

            when("find_outside_quotes searches for eq") = []
            {
                then("Skips operators inside quoted values") = []
                {
                    const auto pos = find_outside_quotes("note eq 'a eq b'"sv, " eq "sv);
                    require_true(pos.has_value());
                    require_eq(*pos, 4u);
                };
            };

            when("find_outside_quotes_and_parens searches for or") = []
            {
                then("Skips or inside grouping parentheses") = []
                {
                    const auto nested = find_outside_quotes_and_parens(
                        "(name eq 'Alice' or name eq 'Bob') and age gt 25"sv, " or "sv);
                    require_false(nested.has_value());

                    const auto top = find_outside_quotes_and_parens(
                        "(name eq 'Alice' or name eq 'Bob') or age gt 25"sv, " or "sv);
                    require_true(top.has_value());
                    require_eq(*top, 34u);
                };
            };

            when("split_outside_quotes_and_parens splits on and") = []
            {
                then("Keeps and/or groups inside parentheses together") = []
                {
                    const auto parts = split_outside_quotes_and_parens(
                        "(name eq 'Alice' or name eq 'Bob') and age gt 25"sv, " and "sv)
                        | std::ranges::to<std::vector<std::string_view>>();
                    require_eq(parts.size(), 2u);
                    require_eq(parts[0], "(name eq 'Alice' or name eq 'Bob')"sv);
                    require_eq(parts[1], "age gt 25"sv);
                };
            };

            when("split_outside_quotes receives an empty delimiter") = []
            {
                then("Throws invalid_argument") = []
                {
                    require_throws_as([]
                    {
                        (void)split_outside_quotes("a,b"sv, ""sv);
                    }, std::invalid_argument{"delimiter cannot be empty"});
                };
            };
        };
    };

    scenario("parse_comma_separated_list respects quoted commas, [yardb]") = []
    {
        given("A select list with a comma inside quotes") = []
        {
            when("Parsing the list") = []
            {
                then("Splits only on commas outside quotes") = []
                {
                    const auto fields = parse_comma_separated_list("name,'a,b',email"sv);
                    require_eq(fields.size(), 3u);
                    require_eq(fields[0], "name"sv);
                    require_eq(fields[1], "'a,b'"sv);
                    require_eq(fields[2], "email"sv);
                };
            };
        };
    };

    scenario("parse_query_params splits query string, [yardb]") = []
    {
        given("A URI with query parameters") = []
        {
            when("Parsing $top and $select") = []
            {
                then("Returns decoded key/value pairs") = []
                {
                    const auto uri = ::http::uri{"/users?$top=5&$select=name%2Cemail"sv};
                    const auto params = parse_query_params(uri);
                    require_eq(params.at("$top"s), "5"s);
                    require_eq(params.at("$select"s), "name,email"s);
                };
            };
        };
    };

    scenario("validate_collection_name rejects invalid characters, [yardb]") = []
    {
        given("A collection name with uppercase letters") = []
        {
            when("Validating the name") = []
            {
                then("Throws invalid_argument") = []
                {
                    require_throws_as([]
                    {
                        validate_collection_name("Users"sv);
                    }, std::invalid_argument{"Collection name contains invalid character (only lowercase letters, digits, and underscore allowed)"});
                };
            };
        };
    };

    return true;
}

const auto _ = register_details_tests();

} // namespace yar::details_unit_test
