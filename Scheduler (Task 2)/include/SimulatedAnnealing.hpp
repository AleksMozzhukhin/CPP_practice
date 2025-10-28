// include/SimulatedAnnealing.hpp
#pragma once

#include "ISolution.hpp"
#include "IMutation.hpp"
#include "ICoolingSchedule.hpp"

#include <memory>
#include <random>
#include <cstddef>
#include <stdexcept>
#include <cmath>

namespace sched {

/**
 * @brief Параметры работы алгоритма имитации отжига.
 *
 * Основной критерий останова:
 *   - нет улучшения лучшего решения в течение max_no_improve_iters итераций.
 *
 * Дополнительно можно задать ограничение по общему числу итераций
 * (safety-breaker), чтобы не попасть в бесконечный цикл при странных параметрах.
 */
struct SAParams {
    std::size_t max_no_improve_iters = 100;   ///< стагнация по улучшению best
    std::size_t hard_iter_limit      = 1'000'000; ///< жёсткий лимит итераций (страховка)

    // Возможное расширение: число мутаций на одну температуру, но
    // в текущей реализации каждая итерация = одна мутация + один шаг охлаждения.
};

/**
 * @brief Класс, реализующий последовательный алгоритм имитации отжига (SA).
 *
 * Алгоритм:
 *   1. Имеем текущее решение current и лучшее найденное best.
 *   2. На каждой итерации:
 *        a) Генерируем соседа neighbor через оператор мутации.
 *        b) Считаем Δ = neighbor.cost() - current.cost().
 *        c) Если Δ <= 0: принимаем neighbor безусловно.
 *           Иначе принимаем с вероятностью exp(-Δ / T), где T = cooling.current_temperature().
 *        d) Обновляем best, если текущее стало лучше.
 *        e) Понижаем температуру cooling.next_step().
 *        f) Увеличиваем счётчик итераций без улучшения.
 *   3. Останавливаемся, если:
 *        - best не улучшался max_no_improve_iters подряд
 *        - или достигнут жёсткий лимит итераций.
 *
 * Результат run(): лучшая найденная конфигурация (глубокая копия).
 *
 * ВАЖНО:
 *   - Конструктор копирует начальное решение (clone()).
 *   - Класс ВЛАДЕЕТ своими копиями current и best.
 *   - Мутация обязана возвращать корректное решение.
 */
class SimulatedAnnealing {
public:
    SimulatedAnnealing(
        const ISolution& initial_solution,
        const IMutation& mutation_op,
        ICoolingSchedule& cooling_schedule,
        const SAParams& params,
        std::mt19937_64 rng
    )
        : mutation_(mutation_op)
        , cooling_(cooling_schedule)
        , params_(params)
        , rng_(std::move(rng))
    {
        // Клонируем начальное решение
        current_ = initial_solution.clone();
        best_    = initial_solution.clone();

        if (!current_ || !best_) {
            throw std::runtime_error("SimulatedAnnealing: failed to clone initial solution");
        }
    }

    /**
     * @brief Запуск алгоритма ИО.
     *
     * По завершении возвращается лучшая найденная версия решения.
     * Возвращаемый указатель — новая копия (caller's ownership).
     */
    std::unique_ptr<ISolution> run() {
        std::size_t no_improve_iters = 0;
        std::size_t iter_count = 0;

        // safety checks
        if (!current_ || !best_) {
            throw std::runtime_error("SimulatedAnnealing::run: not initialized");
        }

        cooling_.reset(); // привести температуру к начальному значению

        while (true) {
            // 1. Сгенерировать соседа
            auto neighbor = mutation_.mutate(*current_, rng_);
            if (!neighbor) {
                throw std::runtime_error("SimulatedAnnealing::run: mutation returned null neighbor");
            }

            // 2. Вычислить дельту по целевой функции
            CostType c_curr = current_->cost();
            CostType c_neig = neighbor->cost();
            CostType delta  = c_neig - c_curr;

            bool accept_neighbor = false;
            if (delta <= 0) {
                // Улучшение или равноценный ход — принимаем детерминированно
                accept_neighbor = true;
            } else {
                // Ухудшение — принимаем с вероятностью exp(-Δ / T)
                double T = cooling_.current_temperature();
                if (T <= 0.0) {
                    // температура не может быть <= 0.0 по контракту CoolingSchedule,
                    // но, на всякий случай, интерпретируем как "не принимаем".
                    T = 1e-12;
                }
                double prob = std::exp(-static_cast<double>(delta) / T);

                std::uniform_real_distribution<double> dist01(0.0, 1.0);
                double r = dist01(rng_);

                if (r < prob) {
                    accept_neighbor = true;
                }
            }

            if (accept_neighbor) {
                current_ = std::move(neighbor);
            }

            // 3. Проверка улучшения best
            if (current_->cost() < best_->cost()) {
                best_ = current_->clone();
                no_improve_iters = 0;
            } else {
                ++no_improve_iters;
            }

            // 4. Понизить температуру
            cooling_.next_step();

            // 5. Критерий останова (стагнация по улучшению best)
            if (no_improve_iters >= params_.max_no_improve_iters) {
                break;
            }

            // 6. Жёсткий лимит итераций
            ++iter_count;
            if (iter_count >= params_.hard_iter_limit) {
                break;
            }
        }

        // По завершении возвращаем лучшую копию решения
        return best_->clone();
    }

    /**
     * @brief Получить текущее лучшее решение без запуска run().
     * В основном полезно во внешних менеджерах/отладке.
     */
    const ISolution& best_solution_ref() const {
        return *best_;
    }

private:
    const IMutation&      mutation_;
    ICoolingSchedule&     cooling_;
    SAParams              params_;
    std::mt19937_64       rng_;

    std::unique_ptr<ISolution> current_;
    std::unique_ptr<ISolution> best_;
};

} // namespace sched
