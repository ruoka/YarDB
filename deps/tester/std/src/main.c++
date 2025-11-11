import std;

using namespace std::literals;

int main()
{
    std::cout << "Hello World! "s + "YEE!"s << std::endl;

    auto map = std::map<int,int>{};
    map[1] = 2;

    auto list = std::list{1,2,3,4,5,6,7,8,9};
    auto test = std::views::reverse(list);

    for(auto v : test | std::views::take(5))
        std::clog << v << '\n';

    return 0;
}
