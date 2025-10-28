// include/ParallelAnnealerManager.hpp
#pragma once

#include "ISolution.hpp"
#include "IMutation.hpp"
#include "ICoolingSchedule.hpp"
#include "SimulatedAnnealing.hpp"

#include <memory>
#include <vector>
#include <thread>
#include <mutex>
#include <random>
#include <functional>
#include <stdexcept>
#include <atomic>
#include <chrono>

namespace sched {

/**
 * @brief Параметры параллельного менеджера имитации отжига.
 *
 * Внешний цикл (итерации менеджера):
 *   - На каждой итерации поднимается группа потоков.
 *   - Каждый поток локально запускает свой SimulatedAnnealing (своим ГПСЧ и своей схемой охлаждения).
 *   - По завершении потоков менеджер проверяет, улучшилось ли глобально лучшее решение.
 *   - Если глобальное лучшее не улучшилось подряд outer_no_improve_limit раз, останавливаемся.
 *
 * Это соответствует описанию "параллельной версии алгоритма", где несколько
 * копий ИО работают независимо и периодически обмениваются лучшим решением.
 */
struct ParallelSAParams {
    std::size_t n_threads = 4;                 ///< число рабочих потоков в одной итерации
    std::size_t outer_no_improve_limit = 10;   ///< число итераций менеджера без улучшения глобального best перед остановкой

    // Параметры для внутренних SA-запусков:
    SAParams inner_sa_params{};               ///< параметры стагнации и лимитов для внутреннего SA
};


/**
 * @brief Фабрика для построения схемы охлаждения (температурного графика)
 * для каждого потока.
 *
 * Нам нельзя разделять один и тот же объект ICoolingSchedule между потоками,
 * потому что он хранит внутреннее состояние (текущую температуру, шаг итерации).
 *
 * Поэтому мы просим пользователя передать фабрику, которая может порождать
 * независимые экземпляры:
 *
 *    std::function<std::unique_ptr<ICoolingSchedule>()>
 *
 * Каждый поток будет вызывать эту фабрику и получать СВОЮ личную копию
 * ICoolingSchedule.
 */


/**
 * @brief Класс ParallelAnnealerManager: управляет множеством параллельных запусков SA.
 *
 * Использование:
 *
 * 1) Инициализировать менеджер начальными объектами:
 *    - initial_solution: стартовое решение (корректное).
 *    - mutation_op: оператор мутации, разделяемый (должен быть stateless или thread-safe по чтению).
 *    - cooling_factory: фабрика, создающая независимый ICoolingSchedule для каждого потока.
 *    - params: ParallelSAParams (число потоков и т.д.).
 *
 * 2) Вызвать run_parallel().
 *
 * Поведение:
 *
 * - Менеджер хранит "глобально лучшее" решение (global_best_) и защищает доступ к нему мьютексом.
 * - Внешний цикл:
 *      * Спавним n_threads потоков.
 *      * Каждый поток берёт текущую глобально лучшую копию (или initial_solution на первой итерации),
 *        создаёт свой SimulatedAnnealing и запускает его.
 *      * По завершении локально найденный лучший результат пытается улучшить global_best_
 *        (под мьютексом).
 *   После того как все потоки завершатся, менеджер проверяет,
 *   было ли улучшение global_best_ на этой итерации.
 * - Если улучшений не было outer_no_improve_limit итераций подряд, останавливаемся.
 *
 * Возвращаемое значение run_parallel(): копия лучшего найденного решения.
 *
 * Потокобезопасность:
 * - mutation_op передаётся по константной ссылке, считается, что он не изменяет
 *   внутреннее состояние (stateless) и его методы не модифицируют this.
 * - initial_solution перед запуском копируется (clone) при необходимости.
 * - cooling_factory вызывается внутри каждого потока для получения уникального
 *   объекта охлаждения.
 */
class ParallelAnnealerManager {
public:
    using CoolingFactory = std::function<std::unique_ptr<ICoolingSchedule>()>;

    ParallelAnnealerManager(
        const ISolution& initial_solution,
        const IMutation& mutation_op,
        CoolingFactory cooling_factory,
        ParallelSAParams params
    )
        : mutation_(mutation_op)
        , cooling_factory_(std::move(cooling_factory))
        , params_(params)
    {
        if (params_.n_threads == 0) {
            throw std::invalid_argument("ParallelAnnealerManager: n_threads must be >= 1");
        }
        if (params_.outer_no_improve_limit == 0) {
            throw std::invalid_argument("ParallelAnnealerManager: outer_no_improve_limit must be >= 1");
        }

        // Сохраняем глобально лучшее решение как клон initial_solution
        global_best_ = initial_solution.clone();
        if (!global_best_) {
            throw std::runtime_error("ParallelAnnealerManager: failed to clone initial_solution");
        }

        last_improvement_iter_ = 0;
    }

    /**
     * @brief Запускает многопоточный поиск.
     *
     * Возвращает лучшую найденную конфигурацию (новая копия, полностью независимая).
     *
     * Алгоритм:
     *  outer_iter = 0
     *  while outer_iter - last_improvement_iter_ < outer_no_improve_limit:
     *      запустить n_threads потоков
     *      каждый поток:
     *          - получить копию текущего глобального решения
     *          - создать свой cooling_schedule через фабрику
     *          - создать свой rng с уникальным сидом
     *          - запустить SimulatedAnnealing(...)
     *          - попытаться обновить global_best_
     *      дождаться всех потоков (join)
     *      outer_iter++
     *
     * Остановка: если с момента последнего улучшения прошло
     * outer_no_improve_limit итераций внешнего цикла.
     */
    std::unique_ptr<ISolution> run_parallel() {
        std::size_t outer_iter = 0;

        for (;;) {
            // Проверка критерия останова ДО запуска новой волны?
            // В постановке сказано "10 итераций внешнего цикла без улучшения" —
            // то есть даём хотя бы одну волну.
            if (outer_iter > 0) {
                std::size_t stagnation = outer_iter - last_improvement_iter_;
                if (stagnation >= params_.outer_no_improve_limit) {
                    break;
                }
            }

            // В каждой внешней итерации мы создаём n_threads потоков
            // и даём им поработать.
            std::vector<std::thread> workers;
            workers.reserve(params_.n_threads);

            // Флаг "в этой итерации было улучшение?"
            std::atomic<bool> improved_this_round(false);

            for (std::size_t t = 0; t < params_.n_threads; ++t) {
                workers.emplace_back([&, t]() {
                    // Локальный ГПСЧ с уникальным сидом.
                    // Сид зависит от времени и индекса потока.
                    std::uint64_t seed_base =
                        static_cast<std::uint64_t>(
                            std::chrono::high_resolution_clock::now().time_since_epoch().count()
                        );
                    std::uint64_t seed = seed_base ^ (static_cast<std::uint64_t>(t) * 0x9E3779B97F4A7C15ull);
                    std::mt19937_64 rng(seed);

                    // Получаем СВОЮ копию схемы охлаждения.
                    auto cooling_ptr = cooling_factory_();
                    if (!cooling_ptr) {
                        // Если фабрика вернула пусто — это критическая ошибка настройки.
                        throw std::runtime_error("ParallelAnnealerManager worker: cooling_factory returned null");
                    }

                    // Получаем текущую копию глобального лучшего решения как стартовую точку.
                    std::unique_ptr<ISolution> local_start;
                    {
                        std::lock_guard<std::mutex> lock(global_mutex_);
                        local_start = global_best_->clone();
                    }

                    // Запускаем локальный SA.
                    SimulatedAnnealing sa(
                        *local_start,
                        mutation_,
                        *cooling_ptr,
                        params_.inner_sa_params,
                        rng
                    );

                    auto local_best = sa.run(); // лучшее решение, найденное этим потоком

                    // Попробуем обновить глобально лучшее решение
                    {
                        std::lock_guard<std::mutex> lock(global_mutex_);
                        if (local_best->cost() < global_best_->cost()) {
                            global_best_ = local_best->clone();
                            improved_this_round.store(true, std::memory_order_relaxed);
                        }
                    }
                });
            }

            // join всех потоков
            for (auto& th : workers) {
                if (th.joinable()) {
                    th.join();
                }
            }

            // Если было улучшение — обновляем last_improvement_iter_
            if (improved_this_round.load(std::memory_order_relaxed)) {
                last_improvement_iter_ = outer_iter;
            }

            ++outer_iter;
        }

        // Возвращаем копию глобально лучшего решения
        std::lock_guard<std::mutex> lock(global_mutex_);
        return global_best_->clone();
    }

    /**
     * @brief Получить текущее глобальное лучшее решение (константная ссылка).
     *        Полезно для отладки.
     */
    const ISolution& global_best_ref() const {
        std::lock_guard<std::mutex> lock(global_mutex_);
        return *global_best_;
    }

private:
    const IMutation& mutation_;
    CoolingFactory cooling_factory_;
    ParallelSAParams params_;

    // Глобально лучшее решение, совместно используемое всеми потоками.
    std::unique_ptr<ISolution> global_best_;

    // Для защиты доступа к global_best_
    mutable std::mutex global_mutex_;

    // Номер последней внешней итерации, на которой было улучшение global_best_.
    std::size_t last_improvement_iter_{0};
};

} // namespace sched
