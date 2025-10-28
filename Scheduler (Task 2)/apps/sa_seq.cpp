// apps/sa_seq.cpp

#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <memory>
#include <cstdlib>
#include <cstring>

#include "ProblemInstance.hpp"
#include "ScheduleSolution.hpp"
#include "IMutation.hpp"
#include "MutationOperators.hpp"
#include "ICoolingSchedule.hpp"
#include "CoolingSchedules.hpp"
#include "SimulatedAnnealing.hpp"

using namespace sched;

// ------------------------
// Утилиты парсинга аргументов
// ------------------------

static bool parse_int_arg(const char* s, int& out) {
    if (!s) return false;
    char* end = nullptr;
    long long v = std::strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (v < std::numeric_limits<int>::min() || v > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(v);
    return true;
}

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

// ------------------------
// Вывод расписания
// ------------------------

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

// ------------------------
// Выбор режима охлаждения
// ------------------------

/*
 * Поддерживаемые режимы:
 *
 *   --cooling geom T0 alpha
 *   --cooling linear T0 beta
 *   --cooling cauchy T0 gamma
 *
 * Возвращаем unique_ptr<ICoolingSchedule>.
 *
 * В случае некорректных аргументов бросаем std::runtime_error.
 */
static std::unique_ptr<ICoolingSchedule> make_cooling_from_cli(
    const std::vector<std::string>& args,
    std::size_t& i // текущая позиция в args, будет сдвинута
) {
    // args[i] должно быть названием режима
    if (i >= args.size()) {
        throw std::runtime_error("Missing cooling type after --cooling");
    }
    const std::string mode = args[i++];
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
        return std::make_unique<GeometricCooling>(T0, alpha);
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
        return std::make_unique<LinearCooling>(T0, beta);
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
        return std::make_unique<CauchyCooling>(T0, gamma);
    } else {
        throw std::runtime_error("Unknown cooling type: " + mode);
    }
}

// ------------------------
// main()
// ------------------------

/*
 * Пример запуска:
 *
 *   sa_seq \
 *     --input instance.csv \
 *     --cooling geom 1000 0.99 \
 *     --max-no-improve 100 \
 *     --hard-limit 200000
 *
 * Обязательные параметры:
 *   --input <file>
 *   --cooling <type> <params...>   (geom/linear/cauchy)
 *
 * Необязательные:
 *   --max-no-improve <iters>       (по умолчанию 100)
 *   --hard-limit <iters>           (по умолчанию 1e6)
 *   --seed <uint64>                (если не задан, сид берётся от std::random_device)
 */
int main(int argc, char** argv) {
    std::vector<std::string> args;
    args.reserve(argc);
    for (int k = 1; k < argc; ++k) {
        args.emplace_back(argv[k]);
    }

    std::string input_path;
    SAParams sa_params;
    sa_params.max_no_improve_iters = 100;
    sa_params.hard_iter_limit      = 1'000'000;

    bool cooling_given = false;
    std::unique_ptr<ICoolingSchedule> cooling; // позже создадим
    std::uint64_t seed_val = 0;
    bool seed_set = false;

    for (std::size_t i = 0; i < args.size(); ) {
        const std::string& token = args[i++];
        if (token == "--input") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --input\n";
                return 1;
            }
            input_path = args[i++];
        } else if (token == "--cooling") {
            cooling = make_cooling_from_cli(args, i);
            cooling_given = true;
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
            sa_params.max_no_improve_iters = tmp;
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
            sa_params.hard_iter_limit = tmp;
        } else if (token == "--seed") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --seed\n";
                return 1;
            }
            char* end = nullptr;
            unsigned long long s = std::strtoull(args[i].c_str(), &end, 10);
            if (end == args[i].c_str() || *end != '\0') {
                std::cerr << "Bad --seed value\n";
                return 1;
            }
            ++i;
            seed_val = static_cast<std::uint64_t>(s);
            seed_set = true;
        } else {
            std::cerr << "Unknown argument: " << token << "\n";
            return 1;
        }
    }

    if (input_path.empty()) {
        std::cerr << "You must provide --input <file.csv>\n";
        return 1;
    }
    if (!cooling_given || !cooling) {
        std::cerr << "You must provide --cooling <mode ...>\n";
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

    // Строим начальное жадное решение
    ScheduleSolution start_sol = ScheduleSolution::buildGreedy(inst);

    if (!start_sol.isValid()) {
        std::cerr << "Greedy initial solution is invalid. This should never happen.\n";
        return 1;
    }

    // Генератор случайных чисел
    std::mt19937_64 rng;
    if (seed_set) {
        rng.seed(seed_val);
    } else {
        std::random_device rd;
        rng.seed(((std::uint64_t)rd() << 32) ^ (std::uint64_t)rd());
    }

    // Оператор мутации
    ScheduleMutationMoveOne mutation_op;

    // Запускаем SA и замеряем время
    auto t_begin = std::chrono::high_resolution_clock::now();

    SimulatedAnnealing sa(
        start_sol,
        mutation_op,
        *cooling,
        sa_params,
        rng
    );

    std::unique_ptr<ISolution> best_ptr = sa.run();

    auto t_end = std::chrono::high_resolution_clock::now();
    auto dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();

    // Пытаемся привести результат к ScheduleSolution, чтобы получить makespan и само расписание
    const ScheduleSolution* best_sched = dynamic_cast<const ScheduleSolution*>(best_ptr.get());
    if (!best_sched) {
        std::cerr << "Internal error: best solution is not ScheduleSolution\n";
        return 1;
    }

    // Вывод результата

    print_schedule_human_readable(*best_sched);
    std::cout << "=== SA RESULT ===\n";
    std::cout << "Cost (K2): " << best_sched->cost() << "\n";
    std::cout << "Makespan : " << best_sched->computeMakespan() << "\n";
    std::cout << "Time (ms): " << dur_ms << "\n";

    return 0;
}
