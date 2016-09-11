
    enum comparison {equal, greater, less, greater_or_equal, less_or_equal};

    std::tuple<comparison,object> analyze(const object& selector)
    {
        auto comop = comparison{};
        auto value = object{};

        if(selector.has(u8"$eq"s))
        {
            comop = equal;
            value = selector[u8"$eq"s];
        }
        else if(selector.has(u8"$gt"s))
        {
            comop = greater;
            value = selector[u8"$gt"s];
        }
        else if(selector.has(u8"$lt"s))
        {
            comop = less;
            value = selector[u8"$lt"s];
        }
        else if(selector.has(u8"$gte"s))
        {
            comop = greater_or_equal;
            value = selector[u8"$gte"s];
        }
        else if(selector.has(u8"$lte"s))
        {
            comop = less_or_equal;
            value = selector[u8"$lte"s];
        }
        else
        {
            comop = equal;
            value = selector;
        }

        auto key = std::string{};
/*
        if(value.has(u8"id"s))
        {
            key = value[u8"id"s];
        }
        else
        {
            for(auto& key : m_secondary_keys)
                if(selector.has(key.first))
                    key = selector[key.first];
                    begin = end = index_iterator{key.second.find(selector[key.first]), key.second.end()};
        }
*/
        return {comop,value};
    }
