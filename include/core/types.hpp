#pragma once
#include <cstddef>

namespace core {

    // Идентификатор игрока (индексация с 0)
    using PlayerId = std::size_t;

    // Итог игры
    enum class Winner {
        None,
        Town,
        Mafia,
        Maniac
    };

} // namespace core
