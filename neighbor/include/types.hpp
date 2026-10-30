#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

/** @brief Идентификатор шарда эксперта в модели. */
struct ShardId {
    uint16_t model, layer, expert;
};

/** @brief Уровни источников весов: от пула памяти до первичного источника. */
enum class Tier : uint8_t {
    Pool,
    LocalDisk,
    Peer,
    Origin
};

/**
 * @brief Три матрицы эксперта в памяти хоста.
 * @warning Представления не владеют памятью. Владелец буферов обязан сохранять
 * их действительность до завершения использования весов.
 */
struct ExpertBytes {
    std::span<const std::byte> gate, up, down;
};
