#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "types.hpp"

/** @brief Постоянное локальное хранилище назначенных шардов, отдельное от пула ОЗУ. */
struct ShardStore {
    virtual ~ShardStore() = default;

    /**
     * @brief Читает шард с проверкой SHA-256.
     * @param id Идентификатор шарда.
     * @return Необязательное представление трёх матриц эксперта.
     * @warning Результат не владеет байтами. Срок их действительности
     * необходимо согласовать с конкретной реализацией хранилища.
     */
    virtual std::optional<ExpertBytes> read(
        ShardId id
    ) = 0;

    /**
     * @brief Читает диапазон байт для аудита.
     * @param id Идентификатор шарда.
     * @param off Смещение в байтах.
     * @param len Длина диапазона в байтах.
     * @return Массив байт, владеющий своей памятью.
     */
    virtual std::vector<std::byte> read_range(
        ShardId id,
        uint64_t off,
        uint32_t len
    ) = 0;

    /**
     * @brief Атомарно сохраняет шард.
     * @param id Идентификатор шарда.
     * @param w Веса, память которых заимствуется на время вызова.
     */
    virtual void put(
        ShardId id,
        const ExpertBytes& w
    ) = 0;
};
