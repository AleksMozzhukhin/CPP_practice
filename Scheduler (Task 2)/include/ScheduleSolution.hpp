// include/ScheduleSolution.hpp
#pragma once

#include "ISolution.hpp"
#include "ProblemInstance.hpp"

#include <vector>
#include <memory>
#include <cstdint>
#include <limits>
#include <random>

namespace sched {

/**
 * @brief Класс конкретного решения задачи расписания.
 *
 * Данное решение описывает распределение работ по процессорам и порядок их выполнения.
 * Это соответствует допустимой матрице расписания R из формальной постановки задачи,
 * но хранится в более удобном виде:
 *
 *  assignment[m] = { j0, j1, j2, ... }
 *
 * где m — индекс процессора (0..M-1),
 * а значения внутри — индексы работ (0..N-1) в порядке их запуска.
 *
 * Требования корректности (эквивалент условий корректности расписания):
 *  - Каждая работа встречается ровно один раз среди всех процессоров.
 *  - В каждой строке нет "дыр": список плотный сам по себе.
 *  - Индексы работ допустимы (0 <= job < N).
 *
 * Целевая функция (cost) — критерий K2 = сумма времён завершения всех работ.
 * Время завершения работы i равно моменту окончания её выполнения на выделенном процессоре.
 *
 * Также класс предоставляет вспомогательную метрику makespan (максимальное время завершения
 * по всем процессорам).
 */
class ScheduleSolution final : public ISolution {
public:
    using Assignment = std::vector<std::vector<int>>;

private:
    const ProblemInstance* instance_;  // не-владеющий указатель; ProblemInstance должен жить дольше
    Assignment assignment_;            // assignment[m] - упорядоченный список работ на процессоре m

    // Кэш стоимости для ускорения.
    mutable CostType cached_cost_ = 0;
    mutable bool cost_valid_ = false;

public:
    /**
     * @brief Создание решения на основе готового назначения работ.
     *
     * @param inst       Экземпляр задачи (должен существовать дольше решения).
     * @param assignment Упорядоченный список работ по процессорам.
     *
     * Внимание: конструктор НЕ проверяет корректность assignment.
     * Пользователь может вызвать isValid() для валидации.
     */
    ScheduleSolution(const ProblemInstance* inst,
                     Assignment assignment)
        : instance_(inst),
          assignment_(std::move(assignment)),
          cached_cost_(0),
          cost_valid_(false)
    {}

    /**
     * @brief Вернуть ссылку на экземпляр задачи.
     */
    const ProblemInstance& instance() const noexcept {
        return *instance_;
    }

    /**
     * @brief Доступ к назначению работ (константный).
     */
    const Assignment& assignment() const noexcept {
        return assignment_;
    }

    /**
     * @brief Доступ к назначению работ (неконстантный).
     *
     * ВНИМАНИЕ: любое изменение assignment через этот метод должно сопровождаться
     * вызовом markDirty(), чтобы сбросить кэш стоимости.
     */
    Assignment& assignment() noexcept {
        return assignment_;
    }

    /**
     * @brief Пометить кэш стоимости как устаревший.
     *
     * Вызывать после любых модификаций assignment.
     */
    void markDirty() noexcept {
        cost_valid_ = false;
    }

    /**
     * @brief Проверка корректности расписания.
     *
     * Условия:
     *  1. assignment_.size() == M
     *  2. каждая работа встречается ровно один раз;
     *  3. нет некорректных индексов работ (<0 или >=N).
     *
     * @return true, если корректно; false иначе.
     */
    bool isValid() const;

    /**
     * @brief Вычисление критерия качества (K2).
     *
     * cost() кэшируется. Если кэш невалиден, он будет пересчитан.
     *
     * Алгоритм вычисления:
     *  Для каждого процессора m:
     *    t = 0
     *    для каждой работы job в assignment[m] по порядку:
     *      t += p[job]
     *      C[job] = t
     *  Далее cost = sum_j C[j].
     *
     * @return Значение целевой функции K2 (чем меньше, тем лучше).
     */
    CostType cost() const override;

    /**
     * @brief Вычисление makespan (максимальное время окончания среди всех процессоров).
     *
     * Это полезно как вспомогательный показатель качества, но НЕ та функция,
     * которую оптимизирует имитация отжига по умолчанию.
     *
     * @return Время завершения самого "длинного" процессора.
     */
    CostType computeMakespan() const;

    /**
     * @brief Глубокое копирование решения.
     *
     * Должно возвращать независимый объект: изменение оригинала не меняет клон.
     * При этом указатель на ProblemInstance копируется как есть
     * (то есть клон ссылается на тот же экземпляр задачи).
     */
    std::unique_ptr<ISolution> clone() const override;

    /**
     * @brief Построение жадного начального решения.
     *
     * Стартовая эвристика:
     *  - Имеем M пустых очередей процессоров и вектор текущих загрузок load[m] = 0.
     *  - Для каждой работы i от 0 до N-1:
     *      находим процессор m* с минимальной текущей загрузкой load[m],
     *      кладём работу i в конец assignment[m*],
     *      обновляем load[m*] += p[i].
     *
     * Такое решение всегда корректно, и служит хорошей отправной точкой
     * для имитации отжига.
     *
     * @param inst экземпляр задачи, относительно которого строится решение.
     * @return корректное расписание для inst.
     */
    static ScheduleSolution buildGreedy(const ProblemInstance& inst);
};

} // namespace sched
