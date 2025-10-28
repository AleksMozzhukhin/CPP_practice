// include/MutationOperators.hpp
#pragma once

#include "IMutation.hpp"
#include "ScheduleSolution.hpp"

#include <random>
#include <memory>
#include <stdexcept>
#include <vector>

namespace sched {

/**
 * @brief Оператор мутации для расписания: случайное перемещение одной работы.
 *
 * Идея:
 *  1. Выбираем случайный процессор-источник m_src, у которого есть хотя бы одна работа.
 *  2. Выбираем случайную позицию pos_src внутри assignment[m_src], извлекаем работу job.
 *  3. Выбираем случайный процессор-приёмник m_dst (может совпадать с m_src).
 *  4. Выбираем случайную позицию вставки pos_dst в диапазоне [0 .. size(assignment[m_dst])],
 *     и вставляем туда job.
 *
 * Такая операция сохраняет:
 *  - работу job в точности один раз в общем назначении;
 *  - корректность индексов работ;
 *  - плотность списка на каждом процессоре.
 *
 * Важно:
 *  - Если M=1 и на единственном процессоре одна работа, тогда после удаления и вставки мы
 *    просто сдвинем её в другое место в том же списке (в этом случае pos_dst может совпасть
 *    с pos_src или нет — это допустимо).
 *
 * Гарантии:
 *  - Результат всегда корректен (isValid()==true), при условии корректности src.
 *  - Если по какой-то причине невозможно выбрать m_src (теоретически при M>0 такого не бывает,
 *    кроме некорректного src), будет выброшено исключение std::runtime_error.
 */
class ScheduleMutationMoveOne final : public IMutation {
public:
    std::unique_ptr<ISolution> mutate(
        const ISolution& src_base,
        std::mt19937_64& rng
    ) const override
    {
        // Попытка привести базовое решение к ScheduleSolution.
        auto const* src = dynamic_cast<ScheduleSolution const*>(&src_base);
        if (!src) {
            throw std::runtime_error("ScheduleMutationMoveOne::mutate: src is not ScheduleSolution");
        }

        // Клонируем исходное решение.
        auto dst_uptr = src->clone();
        auto* dst = dynamic_cast<ScheduleSolution*>(dst_uptr.get());
        if (!dst) {
            throw std::runtime_error("ScheduleMutationMoveOne::mutate: clone() is not ScheduleSolution");
        }

        // Получаем ссылку на назначение.
        auto& assignment = dst->assignment(); // неконстантный доступ
        const ProblemInstance& inst = dst->instance();
        const int M = inst.M;

        if (M <= 0) {
            throw std::runtime_error("ScheduleMutationMoveOne::mutate: invalid ProblemInstance.M");
        }

        // Соберём индексы процессоров, у которых есть хотя бы одна работа
        std::vector<int> non_empty_procs;
        non_empty_procs.reserve(M);
        for (int m = 0; m < M; ++m) {
            if (!assignment[m].empty()) {
                non_empty_procs.push_back(m);
            }
        }

        if (non_empty_procs.empty()) {
            // Это означает, что в расписании вообще нет работ, что противоречит корректности,
            // но формально мы обработаем это как исключение.
            throw std::runtime_error("ScheduleMutationMoveOne::mutate: all processors empty");
        }

        // 1. Выбираем процессор-источник m_src
        {
            std::uniform_int_distribution<std::size_t> dist_src_proc(
                0u, non_empty_procs.size() - 1u
            );
            int m_src = non_empty_procs[dist_src_proc(rng)];

            // 2. Выбираем позицию внутри этого процессора
            auto& src_vec = assignment[m_src];
            std::uniform_int_distribution<std::size_t> dist_src_pos(
                0u, src_vec.size() - 1u
            );
            std::size_t pos_src = dist_src_pos(rng);

            int job = src_vec[pos_src];

            // Удаляем выбранную работу из src_vec
            src_vec.erase(src_vec.begin() + static_cast<std::ptrdiff_t>(pos_src));

            // 3. Выбираем процессор-приёмник m_dst (любой от 0 до M-1)
            std::uniform_int_distribution<int> dist_dst_proc(0, M - 1);
            int m_dst = dist_dst_proc(rng);

            auto& dst_vec = assignment[m_dst];

            // 4. Выбираем позицию вставки в диапазоне [0 .. dst_vec.size()]
            std::uniform_int_distribution<std::size_t> dist_dst_pos(
                0u, dst_vec.size()
            );
            std::size_t pos_dst = dist_dst_pos(rng);

            // Вставляем работу
            dst_vec.insert(dst_vec.begin() + static_cast<std::ptrdiff_t>(pos_dst), job);
        }

        // Помечаем стоимость как устаревшую
        dst->markDirty();

        // Контроль корректности (на debug-сборке важно)
        // В релизной версии можно опустить проверку isValid() ради скорости.
        if (!dst->isValid()) {
            throw std::runtime_error("ScheduleMutationMoveOne::mutate: produced invalid schedule");
        }

        return dst_uptr;
    }
};

} // namespace sched
