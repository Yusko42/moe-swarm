#pragma once

#include "types.hpp"

/** @brief Записывает веса эксперта в слот пула для вычислительного бэкенда. */
struct PoolWriter {
    virtual ~PoolWriter() = default;

    /**
     * @brief Записывает три матрицы эксперта в указанный слот.
     * @param layer Номер слоя.
     * @param slot Номер целевого слота.
     * @param w Заимствованные представления весов.
     * @warning Сохранение доступа к весам после возврата требует явного
     * соглашения с вызывающей стороной о времени жизни памяти.
     */
    virtual void write(
        int layer,
        int slot,
        const ExpertBytes& w
    ) = 0;
};
