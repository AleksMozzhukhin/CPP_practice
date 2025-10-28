// apps/sa_par.cpp

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "ProblemInstance.hpp"
#include "ScheduleSolution.hpp"
#include "MutationOperators.hpp"
#include "CoolingSchedules.hpp"
#include "SimulatedAnnealing.hpp"
#include "ParallelAnnealerManager.hpp"

using namespace sched;

// ------------------------------------------------------------
// Парсинг аргументов
// ------------------------------------------------------------

static bool parse_size_t_arg(const char* s, std::size_t& out) {
    if (!s) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<std::size_t>(v);
    return true;
}

static bool parse_double_arg(const char* s, double& out) {
    if (!s) return false;
    char* end = nullptr;
    double v = std::strtod(s, &end);
    if (end == s || *end != '\0') return false;
    out = v;
    return true;
}

// ------------------------------------------------------------
// Вывод расписания
// ------------------------------------------------------------

static void print_schedule_human_readable(const ScheduleSolution& sol) {
    const auto& inst = sol.instance();
    const auto& asg  = sol.assignment();

    std::cout << "Final schedule (processor -> job indices):\n";
    for (int m = 0; m < inst.M; ++m) {
        std::cout << "  P" << m << ":";
        for (int job : asg[m]) {
            std::cout << " " << job;
        }
        std::cout << "\n";
    }
}

// ------------------------------------------------------------
// Конфигурация охлаждения
// ------------------------------------------------------------

enum class CoolingKind {
    Geometric,
    Linear,
    Cauchy
};

struct CoolingSetup {
    CoolingKind kind;
    double T0;
    double param; // alpha (geom), beta (linear), gamma (cauchy)
};

// Парсит последовательность аргументов после флага --cooling:
//   --cooling geom   T0 alpha
//   --cooling linear T0 beta
//   --cooling cauchy T0 gamma
//
// Возвращает заполненный CoolingSetup и сдвигает индекс i.
static CoolingSetup parse_cooling_setup(
    const std::vector<std::string>& args,
    std::size_t& i
) {
    if (i >= args.size()) {
        throw std::runtime_error("Missing cooling type after --cooling");
    }

    const std::string mode = args[i++];

    CoolingSetup cfg;
    if (mode == "geom" || mode == "geometric") {
        if (i + 1 >= args.size()) {
            throw std::runtime_error("Usage: --cooling geom <T0> <alpha>");
        }
        double T0, alpha;
        if (!parse_double_arg(args[i].c_str(), T0)) {
            throw std::runtime_error("Bad T0 for geom cooling");
        }
        ++i;
        if (!parse_double_arg(args[i].c_str(), alpha)) {
            throw std::runtime_error("Bad alpha for geom cooling");
        }
        ++i;
        cfg.kind  = CoolingKind::Geometric;
        cfg.T0    = T0;
        cfg.param = alpha;
        return cfg;
    } else if (mode == "linear") {
        if (i + 1 >= args.size()) {
            throw std::runtime_error("Usage: --cooling linear <T0> <beta>");
        }
        double T0, beta;
        if (!parse_double_arg(args[i].c_str(), T0)) {
            throw std::runtime_error("Bad T0 for linear cooling");
        }
        ++i;
        if (!parse_double_arg(args[i].c_str(), beta)) {
            throw std::runtime_error("Bad beta for linear cooling");
        }
        ++i;
        cfg.kind  = CoolingKind::Linear;
        cfg.T0    = T0;
        cfg.param = beta;
        return cfg;
    } else if (mode == "cauchy") {
        if (i + 1 >= args.size()) {
            throw std::runtime_error("Usage: --cooling cauchy <T0> <gamma>");
        }
        double T0, gamma;
        if (!parse_double_arg(args[i].c_str(), T0)) {
            throw std::runtime_error("Bad T0 for cauchy cooling");
        }
        ++i;
        if (!parse_double_arg(args[i].c_str(), gamma)) {
            throw std::runtime_error("Bad gamma for cauchy cooling");
        }
        ++i;
        cfg.kind  = CoolingKind::Cauchy;
        cfg.T0    = T0;
        cfg.param = gamma;
        return cfg;
    } else {
        throw std::runtime_error("Unknown cooling type: " + mode);
    }
}

// Фабрика охлаждения для ParallelAnnealerManager.
// На каждый поток вызывается эта фабрика, чтобы получить новый независимый
// объект ICoolingSchedule со своим внутренним состоянием.
static ParallelAnnealerManager::CoolingFactory make_cooling_factory(const CoolingSetup& cs) {
    return [cs]() -> std::unique_ptr<ICoolingSchedule> {
        switch (cs.kind) {
            case CoolingKind::Geometric:
                return std::make_unique<GeometricCooling>(cs.T0, cs.param);
            case CoolingKind::Linear:
                return std::make_unique<LinearCooling>(cs.T0, cs.param);
            case CoolingKind::Cauchy:
                return std::make_unique<CauchyCooling>(cs.T0, cs.param);
            default:
                throw std::runtime_error("Unknown CoolingKind in factory");
        }
    };
}

// ------------------------------------------------------------
// main()
// ------------------------------------------------------------

/*
 * Пример запуска:
 *
 *   sa_par \
 *     --input instance.csv \
 *     --cooling geom 1000 0.99 \
 *     --threads 8 \
 *     --outer-no-improve 10 \
 *     --max-no-improve 100 \
 *     --hard-limit 200000
 *
 * Обязательные аргументы:
 *   --input <file.csv>
 *   --cooling <mode> <...>
 *   --threads <N>
 *   --outer-no-improve <K>
 *
 * Необязательные:
 *   --max-no-improve <iters>   (по умолчанию 100)
 *   --hard-limit <iters>       (по умолчанию 1e6)
 *
 * Поведение:
 *   - Загружаем экземпляр задачи.
 *   - Строим жадное начальное решение.
 *   - Инициализируем ParallelAnnealerManager.
 *   - Запускаем run_parallel().
 *   - Печатаем лучшее найденное расписание.
 */
int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc);
    for (int k = 1; k < argc; ++k) {
        args.emplace_back(argv[k]);
    }

    std::string input_path;

    // Внутренние параметры SA для каждого потока
    SAParams inner_sa_params;
    inner_sa_params.max_no_improve_iters = 100;
    inner_sa_params.hard_iter_limit      = 1'000'000;

    // Внешние параметры параллельного менеджера
    std::size_t n_threads = 0;
    std::size_t outer_no_improve_limit = 0;

    // Охлаждение
    bool cooling_given = false;
    CoolingSetup cooling_setup{CoolingKind::Geometric, 1000.0, 0.99};

    // Парсим аргументы
    for (std::size_t i = 0; i < args.size(); ) {
        const std::string& token = args[i++];
        if (token == "--input") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --input\n";
                return 1;
            }
            input_path = args[i++];
        } else if (token == "--cooling") {
            try {
                cooling_setup = parse_cooling_setup(args, i);
                cooling_given = true;
            } catch (const std::exception& e) {
                std::cerr << "Error parsing --cooling: " << e.what() << "\n";
                return 1;
            }
        } else if (token == "--threads") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --threads\n";
                return 1;
            }
            if (!parse_size_t_arg(args[i].c_str(), n_threads)) {
                std::cerr << "Bad --threads value\n";
                return 1;
            }
            ++i;
        } else if (token == "--outer-no-improve") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --outer-no-improve\n";
                return 1;
            }
            if (!parse_size_t_arg(args[i].c_str(), outer_no_improve_limit)) {
                std::cerr << "Bad --outer-no-improve value\n";
                return 1;
            }
            ++i;
        } else if (token == "--max-no-improve") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --max-no-improve\n";
                return 1;
            }
            std::size_t tmp;
            if (!parse_size_t_arg(args[i].c_str(), tmp)) {
                std::cerr << "Bad --max-no-improve value\n";
                return 1;
            }
            ++i;
            inner_sa_params.max_no_improve_iters = tmp;
        } else if (token == "--hard-limit") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --hard-limit\n";
                return 1;
            }
            std::size_t tmp;
            if (!parse_size_t_arg(args[i].c_str(), tmp)) {
                std::cerr << "Bad --hard-limit value\n";
                return 1;
            }
            ++i;
            inner_sa_params.hard_iter_limit = tmp;
        } else {
            std::cerr << "Unknown argument: " << token << "\n";
            return 1;
        }
    }

    // Проверка обязательных параметров
    if (input_path.empty()) {
        std::cerr << "You must provide --input <file.csv>\n";
        return 1;
    }
    if (!cooling_given) {
        std::cerr << "You must provide --cooling <mode ...>\n";
        return 1;
    }
    if (n_threads < 1) {
        std::cerr << "You must provide --threads <N>=1\n";
        return 1;
    }
    if (outer_no_improve_limit < 1) {
        std::cerr << "You must provide --outer-no-improve <K>=1\n";
        return 1;
    }

    // Загружаем экземпляр задачи
    ProblemInstance inst;
    {
        std::string err;
        if (!loadFromCSV(input_path, inst, err)) {
            std::cerr << "Error loading instance from " << input_path << ": " << err << "\n";
            return 1;
        }
    }

    // Готовим параметры параллельного менеджера
    ParallelSAParams par_params;
    par_params.n_threads = n_threads;
    par_params.outer_no_improve_limit = outer_no_improve_limit;
    par_params.inner_sa_params = inner_sa_params;

    // Построим жадное начальное решение, проверим валидность
    ScheduleSolution start_sol = ScheduleSolution::buildGreedy(inst);
    if (!start_sol.isValid()) {
        std::cerr << "Greedy initial solution invalid\n";
        return 1;
    }

    // Оператор мутации (stateless, можно шарить)
    ScheduleMutationMoveOne mutation_op;

    // Фабрика охлаждения (каждый поток получит новый объект ICoolingSchedule)
    auto cooling_factory = make_cooling_factory(cooling_setup);

    // Создаём менеджер
    ParallelAnnealerManager manager(
        start_sol,        // initial_solution
        mutation_op,      // mutation_op
        cooling_factory,  // cooling_factory
        par_params        // parallel params
    );

    // Запускаем и измеряем время
    auto t_begin = std::chrono::high_resolution_clock::now();

    std::unique_ptr<ISolution> best_ptr = manager.run_parallel();

    auto t_end = std::chrono::high_resolution_clock::now();
    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();

    // Приводим к ScheduleSolution для вывода
    const ScheduleSolution* best_sched = dynamic_cast<const ScheduleSolution*>(best_ptr.get());
    if (!best_sched) {
        std::cerr << "Internal error: best solution is not ScheduleSolution\n";
        return 1;
    }

    // Итоговый вывод
    std::cout << "=== PARALLEL SA RESULT ===\n";
    std::cout << "Threads              : " << n_threads << "\n";
    std::cout << "Outer no improve lim : " << outer_no_improve_limit << "\n";
    std::cout << "Inner max_no_improve : " << inner_sa_params.max_no_improve_iters << "\n";
    std::cout << "Inner hard_limit     : " << inner_sa_params.hard_iter_limit << "\n";
    std::cout << "Cost (K2)            : " << best_sched->cost() << "\n";
    std::cout << "Makespan             : " << best_sched->computeMakespan() << "\n";
    std::cout << "Wall time (ms)       : " << dur_ms << "\n";

    print_schedule_human_readable(*best_sched);

    return 0;
}
