#pragma once

#include <cstdint>
#include <span>

#include "types.hpp"

/** @brief Предоставляет llama.cpp слоты пула с весами экспертов. */
struct ExpertSource {
    virtual ~ExpertSource() = default;

    /**
     * @brief Ожидает готовности весов и закрепляет слоты пула.
     * @param layer Номер слоя.
     * @param experts Идентификаторы запрошенных экспертов.
     * @param[out] slots_out Номера слотов в порядке запрошенных экспертов.
     * @pre Размер slots_out не меньше размера experts.
     * @note Оба представления заимствуют память вызывающей стороны на время вызова.
     */
    virtual void acquire(
        int layer,
        std::span<const int32_t> experts,
        std::span<int32_t> slots_out
    ) = 0;

    /**
     * @brief Освобождает закреплённые слоты слоя.
     * @param layer Номер слоя.
     * @pre Все вычисления, использующие слоты слоя, завершены.
     * @warning Перекрывающиеся запросы одного слоя требуют отдельного соглашения.
     */
    virtual void release(int layer) = 0;

    /**
     * @brief Запрашивает неблокирующую предварительную загрузку.
     * @param ids Идентификаторы шардов; память заимствуется только на время вызова.
     * @note Для отложенной обработки реализация должна сохранить копии идентификаторов.
     */
    virtual void prefetch(
        std::span<const ShardId> ids
    ) = 0;
};
