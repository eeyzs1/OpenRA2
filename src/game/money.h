#pragma once
#include <cstdint>
#include <limits>

using Credits = int64_t;
// Leave arithmetic headroom for percentage transfers and refundable production.
constexpr Credits MAX_CREDIT_BALANCE=std::numeric_limits<Credits>::max()/4;
