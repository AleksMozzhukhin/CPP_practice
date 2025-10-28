// src/ScheduleSolution.cpp

#include "ScheduleSolution.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <cassert>

namespace sched {

bool ScheduleSolution::isValid() const {
    if (!instance_) {
        return false;
    }

    const int M = instance_->M;
    const int N = instance_->N;

    // 1. Количество процессоров должно совпадать
    if (static_cast<int>(assignment_.size()) != M) {
        return false;
    }

    // 2. Каждая работа должна встретиться ровно один раз
    std::vector<int> seen(N, 0);

    for (const auto& proc_list : assignment_) {
        for (int job : proc_list) {
            // Проверка корректного индекса работы
            if (job < 0 || job >= N) {
                return false;
            }
            // Счётчик вхождений
            seen[job] += 1;
            if (seen[job] > 1) {
                // Работа встречается больше одного раза
                return false;
            }
        }
    }

    // Теперь проверим, что каждая работа встречается ровно один раз
    for (int i = 0; i < N; ++i) {
        if (seen[i] != 1) {
            return false;
        }
    }

    return true;
}


CostType ScheduleSolution::cost() const {
    if (cost_valid_) {
        return cached_cost_;
    }

    assert(instance_ && "ScheduleSolution::cost(): instance_ is null");

    const int M = instance_->M;
    const int N = instance_->N;
    const auto& p = instance_->p;

    // Вектор времен завершения C[j] для каждой работы j
    std::vector<CostType> completion_time(N, 0);

    // Для каждого процессора моделируем последовательный запуск работ
    for (int m = 0; m < M; ++m) {
        CostType t = 0;
        const auto& proc_list = assignment_[m];
        for (int job : proc_list) {
            // job должен быть допустимым индексом
            assert(job >= 0 && job < N);
            // работа выполняется целиком, без прерываний
            t += static_cast<CostType>(p[job]);
            completion_time[job] = t;
        }
    }

    // Цель K2 = сумма времен завершения всех работ
    CostType total = 0;
    for (int j = 0; j < N; ++j) {
        total += completion_time[j];
    }

    cached_cost_ = total;
    cost_valid_ = true;
    return cached_cost_;
}


CostType ScheduleSolution::computeMakespan() const {
    assert(instance_ && "ScheduleSolution::computeMakespan(): instance_ is null");

    const int M = instance_->M;
    const int N = instance_->N;
    const auto& p = instance_->p;

    CostType max_time = 0;

    for (int m = 0; m < M; ++m) {
        CostType t = 0;
        const auto& proc_list = assignment_[m];
        for (int job : proc_list) {
            assert(job >= 0 && job < N);
            t += static_cast<CostType>(p[job]);
        }
        if (t > max_time) {
            max_time = t;
        }
    }

    return max_time;
}


std::unique_ptr<ISolution> ScheduleSolution::clone() const {
    // Глубокое копирование assignment_,
    // но ProblemInstance остаётся тем же (не-владеющий указатель).
    auto copy = std::make_unique<ScheduleSolution>(instance_, assignment_);
    copy->cached_cost_ = cached_cost_;
    copy->cost_valid_ = cost_valid_;
    return copy;
}


ScheduleSolution ScheduleSolution::buildGreedy(const ProblemInstance& inst) {
    const int M = inst.M;
    const int N = inst.N;
    const auto& p = inst.p;

    if (M <= 0 || N <= 0) {
        throw std::invalid_argument("buildGreedy: M and N must be positive");
    }
    if (static_cast<int>(p.size()) != N) {
        throw std::invalid_argument("buildGreedy: p.size() != N");
    }

    // assignment[m] - список работ на процессоре m
    Assignment assignment;
    assignment.resize(M);

    // load[m] - текущая суммарная длительность, назначенная процессору m
    std::vector<CostType> load(M, 0);

    // Жадно: каждую новую работу отправляем на процессор с минимальной текущей загрузкой
    for (int job = 0; job < N; ++job) {
        // найти процессор с минимальной загрузкой
        int best_m = 0;
        CostType best_load = load[0];

        for (int m = 1; m < M; ++m) {
            if (load[m] < best_load) {
                best_m = m;
                best_load = load[m];
            }
        }

        assignment[best_m].push_back(job);
        load[best_m] += static_cast<CostType>(p[job]);
    }

    ScheduleSolution sol(&inst, std::move(assignment));
    // Жадное построение гарантированно валидно:
    //  - каждая работа использована ровно 1 раз,
    //  - индексы корректные,
    //  - списки плотные по определению.
    assert(sol.isValid() && "buildGreedy produced invalid schedule");

    return sol;
}

} // namespace sched
