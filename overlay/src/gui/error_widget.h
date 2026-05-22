#pragma once
#include "tesla.hpp"

namespace ErrorWidget {
    tsl::elm::List *create(
        const std::string& symbol, const std::string& text,
        const std::optional<std::string>& button_text = std::nullopt,
        const std::function<bool(u64 keys)>& button_listener = {});
}
