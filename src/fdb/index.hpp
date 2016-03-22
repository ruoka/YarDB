#pragma once

#include <map>

namespace fdb {

using index = std::map<std::int64_t,std::streamoff>;

static std::int64_t s_sequence{0};

} // namespace fdb
