// apps/research.cpp

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <chrono>
#include <random>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <algorithm>
#include <memory>
#include <stdexcept>

#include "ProblemInstance.hpp"
#include "ScheduleSolution.hpp"
#include "MutationOperators.hpp"
#include "CoolingSchedules.hpp"
#include "SimulatedAnnealing.hpp"
#include "ParallelAnnealerManager.hpp"

using namespace sched;

// ------------------------------------------------------------
// Вспомогательные функции парсинга
// ------------------------------------------------------------

static bool parse_size_t_arg(const char* s, std::size_t& out) {
    if (!s) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<std::size_t>(v);
    return true;
}

static bool parse_int_arg(const char* s, int& out) {
    if (!s) return false;
    char* end = nullptr;
    long long v = std::strtoll(s, &end, 10);
    if (end == s || *end != '\0') return false;
    if (v < static_cast<long long>(std::numeric_limits<int>::min()) ||
        v > static_cast<long long>(std::numeric_limits<int>::max())) {
        return false;
    }
    out = static_cast<int>(v);
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

static bool parse_uint64_arg(const char* s, std::uint64_t& out) {
    if (!s) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

// trim() и split по запятой с тримом
static inline void trim_str(std::string& s) {
    auto not_space = [](unsigned char c){ return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
}

static std::vector<int> parse_int_list(const std::string& s) {
    std::vector<int> res;
    std::string token;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == ',') {
            std::string t = token;
            trim_str(t);
            if (!t.empty()) {
                int val;
                if (!parse_int_arg(t.c_str(), val)) {
                    throw std::runtime_error("Bad integer in list: " + t);
                }
                res.push_back(val);
            }
            token.clear();
        } else {
            token.push_back(s[i]);
        }
    }
    return res;
}

// ------------------------------------------------------------
// Конфигурация охлаждения
// ------------------------------------------------------------

enum class CoolingKind {
    Geometric,
    Linear,
    Cauchy
};

struct CoolingConfig {
    CoolingKind kind;
    double T0;
    double param; // alpha (geom), beta (linear), gamma (cauchy)
};

// Создаёт новый независимый объект охлаждения по заданной конфигурации
static std::unique_ptr<ICoolingSchedule> make_cooling(const CoolingConfig& ccfg) {
    switch (ccfg.kind) {
        case CoolingKind::Geometric:
            return std::make_unique<GeometricCooling>(ccfg.T0, ccfg.param);
        case CoolingKind::Linear:
            return std::make_unique<LinearCooling>(ccfg.T0, ccfg.param);
        case CoolingKind::Cauchy:
            return std::make_unique<CauchyCooling>(ccfg.T0, ccfg.param);
        default:
            throw std::runtime_error("Unknown CoolingKind");
    }
}

// Парсинг cooling из списка аргументов вида:
//   --cooling geom T0 alpha
//   --cooling linear T0 beta
//   --cooling cauchy T0 gamma
static CoolingConfig parse_cooling_from_cli(
    const std::vector<std::string>& args,
    std::size_t& i // на входе args[i] == <mode>, на выходе i указывает на следующий токен после параметров
) {
    if (i >= args.size()) {
        throw std::runtime_error("Missing cooling type after --cooling");
    }

    const std::string mode = args[i++];
    CoolingConfig ccfg{CoolingKind::Geometric, 0.0, 0.0};

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
        ccfg.kind  = CoolingKind::Geometric;
        ccfg.T0    = T0;
        ccfg.param = alpha;
        return ccfg;

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
        ccfg.kind  = CoolingKind::Linear;
        ccfg.T0    = T0;
        ccfg.param = beta;
        return ccfg;

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
        ccfg.kind  = CoolingKind::Cauchy;
        ccfg.T0    = T0;
        ccfg.param = gamma;
        return ccfg;

    } else {
        throw std::runtime_error("Unknown cooling type: " + mode);
    }
}

// ------------------------------------------------------------
// Основная логика измерений
// ------------------------------------------------------------

struct ResearchParams {
    // режим
    std::string mode; // "seq" или "par"

    // списки размеров задач
    std::vector<int> M_list;
    std::vector<int> N_list;

    // генерация длительностей
    int p_min = 1;
    int p_max = 10;

    // число повторов для усреднения
    std::size_t runs = 5;

    // выходной CSV
    std::string out_csv;

    // Параметры SA (внутренние для каждого запуска)
    SAParams sa_params;
    CoolingConfig cooling_cfg;

    // Параметры параллельного режима
    std::size_t n_threads = 4;
    std::size_t outer_no_improve_limit = 10;

    // seed
    bool have_seed = false;
    std::uint64_t seed_val = 0;
};

// Запуск одного последовательного SA на заданном экземпляре задачи.
// Возвращает пару: (лучший cost, время в миллисекундах)
static std::pair<CostType, long long> run_single_seq(
    const ProblemInstance& inst,
    const ResearchParams& R,
    std::mt19937_64& rng
) {
    // Стартовое решение (жадное)
    ScheduleSolution start_sol = ScheduleSolution::buildGreedy(inst);
    if (!start_sol.isValid()) {
        throw std::runtime_error("Greedy initial solution invalid (seq)");
    }

    // cooling (один объект на один запуск)
    auto cooling_ptr = make_cooling(R.cooling_cfg);

    ScheduleMutationMoveOne mutation_op;

    auto t_begin = std::chrono::high_resolution_clock::now();

    SimulatedAnnealing sa(
        start_sol,
        mutation_op,
        *cooling_ptr,
        R.sa_params,
        rng
    );

    std::unique_ptr<ISolution> best_ptr = sa.run();

    auto t_end = std::chrono::high_resolution_clock::now();
    long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();

    const ScheduleSolution* best_sol = dynamic_cast<const ScheduleSolution*>(best_ptr.get());
    if (!best_sol) {
        throw std::runtime_error("Result is not ScheduleSolution in seq");
    }

    return { best_sol->cost(), dur_ms };
}

// Запуск одного параллельного поиска при данных параметрах.
// Возвращает пару: (лучший cost, время в миллисекундах)
static std::pair<CostType, long long> run_single_par(
    const ProblemInstance& inst,
    const ResearchParams& R
) {
    // Стартовое решение (жадное)
    ScheduleSolution start_sol = ScheduleSolution::buildGreedy(inst);
    if (!start_sol.isValid()) {
        throw std::runtime_error("Greedy initial solution invalid (par)");
    }

    // Параметры внутреннего SA
    ParallelSAParams par_params;
    par_params.n_threads = R.n_threads;
    par_params.outer_no_improve_limit = R.outer_no_improve_limit;
    par_params.inner_sa_params = R.sa_params;

    // Мутация (разделяемый объект, логика без состояния)
    ScheduleMutationMoveOne mutation_op;

    // Фабрика охлаждения (каждый поток получит свою копию)
    ParallelAnnealerManager::CoolingFactory cfact = [&R]() -> std::unique_ptr<ICoolingSchedule> {
        return make_cooling(R.cooling_cfg);
    };

    // У менеджера свои собственные ГПСЧ внутри рабочих потоков, сиды берутся
    // от времени и индекса потока, поэтому здесь дополнительный rng не нужен.
    ParallelAnnealerManager manager(
        start_sol,       // initial_solution
        mutation_op,     // mutation_op
        cfact,           // cooling_factory
        par_params       // параметры параллельного поиска
    );

    auto t_begin = std::chrono::high_resolution_clock::now();

    std::unique_ptr<ISolution> best_ptr = manager.run_parallel();

    auto t_end = std::chrono::high_resolution_clock::now();
    long long dur_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_begin).count();

    const ScheduleSolution* best_sol = dynamic_cast<const ScheduleSolution*>(best_ptr.get());
    if (!best_sol) {
        throw std::runtime_error("Result is not ScheduleSolution in par");
    }

    return { best_sol->cost(), dur_ms };
}

// ------------------------------------------------------------
// main()
// ------------------------------------------------------------

/*
Пример использования (последовательный режим):

research \
  --mode seq \
  --M-list 2,4 \
  --N-list 50,100 \
  --p-min 1 \
  --p-max 20 \
  --runs 5 \
  --cooling geom 1000 0.99 \
  --max-no-improve 100 \
  --hard-limit 200000 \
  --csv out_seq.csv \
  --seed 123

Пример использования (параллельный режим):

research \
  --mode par \
  --M-list 4 \
  --N-list 200,400 \
  --p-min 1 \
  --p-max 20 \
  --runs 3 \
  --cooling cauchy 1000 0.01 \
  --max-no-improve 100 \
  --hard-limit 200000 \
  --threads 8 \
  --outer-no-improve 10 \
  --csv out_par.csv

Пояснения по ключам:
  --mode seq|par
  --M-list <comma-separated ints>
  --N-list <comma-separated ints>
  --p-min <int>=минимальная длительность работы
  --p-max <int>=максимальная длительность работы
  --runs <size_t>=сколько повторов на одну пару (M,N)
  --cooling <type> <params...> (см. выше)
  --max-no-improve <size_t>=SAParams.max_no_improve_iters
  --hard-limit <size_t>=SAParams.hard_iter_limit
  --threads <size_t>=число потоков (Только для mode=par)
  --outer-no-improve <size_t>=число внешних итераций без улучшения (Только par)
  --csv <file>=куда писать csv с результатами
  --seed <uint64> (опц.) глобальный сид для последовательного режима; для параллельного только влияет на генерацию экземпляров.
*/
int main(int argc, char** argv) {
    ResearchParams R;
    R.mode = "seq";
    R.sa_params.max_no_improve_iters = 100;
    R.sa_params.hard_iter_limit      = 1'000'000;
    R.n_threads = 4;
    R.outer_no_improve_limit = 10;
    R.runs = 5;
    R.p_min = 1;
    R.p_max = 10;

    bool cooling_given = false;

    std::vector<std::string> args;
    args.reserve(argc);
    for (int k = 1; k < argc; ++k) {
        args.emplace_back(argv[k]);
    }

    for (std::size_t i = 0; i < args.size(); ) {
        const std::string& token = args[i++];
        if (token == "--mode") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --mode\n";
                return 1;
            }
            R.mode = args[i++];
            if (R.mode != "seq" && R.mode != "par") {
                std::cerr << "Unsupported mode: " << R.mode << "\n";
                return 1;
            }
        } else if (token == "--M-list") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --M-list\n";
                return 1;
            }
            R.M_list = parse_int_list(args[i++]);
            if (R.M_list.empty()) {
                std::cerr << "--M-list must not be empty\n";
                return 1;
            }
        } else if (token == "--N-list") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --N-list\n";
                return 1;
            }
            R.N_list = parse_int_list(args[i++]);
            if (R.N_list.empty()) {
                std::cerr << "--N-list must not be empty\n";
                return 1;
            }
        } else if (token == "--p-min") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --p-min\n";
                return 1;
            }
            if (!parse_int_arg(args[i++].c_str(), R.p_min)) {
                std::cerr << "Bad --p-min value\n";
                return 1;
            }
        } else if (token == "--p-max") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --p-max\n";
                return 1;
            }
            if (!parse_int_arg(args[i++].c_str(), R.p_max)) {
                std::cerr << "Bad --p-max value\n";
                return 1;
            }
        } else if (token == "--runs") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --runs\n";
                return 1;
            }
            if (!parse_size_t_arg(args[i++].c_str(), R.runs)) {
                std::cerr << "Bad --runs value\n";
                return 1;
            }
        } else if (token == "--cooling") {
            R.cooling_cfg = parse_cooling_from_cli(args, i);
            cooling_given = true;
        } else if (token == "--max-no-improve") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --max-no-improve\n";
                return 1;
            }
            std::size_t tmp;
            if (!parse_size_t_arg(args[i++].c_str(), tmp)) {
                std::cerr << "Bad --max-no-improve value\n";
                return 1;
            }
            R.sa_params.max_no_improve_iters = tmp;
        } else if (token == "--hard-limit") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --hard-limit\n";
                return 1;
            }
            std::size_t tmp;
            if (!parse_size_t_arg(args[i++].c_str(), tmp)) {
                std::cerr << "Bad --hard-limit value\n";
                return 1;
            }
            R.sa_params.hard_iter_limit = tmp;
        } else if (token == "--threads") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --threads\n";
                return 1;
            }
            if (!parse_size_t_arg(args[i++].c_str(), R.n_threads)) {
                std::cerr << "Bad --threads value\n";
                return 1;
            }
        } else if (token == "--outer-no-improve") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --outer-no-improve\n";
                return 1;
            }
            if (!parse_size_t_arg(args[i++].c_str(), R.outer_no_improve_limit)) {
                std::cerr << "Bad --outer-no-improve value\n";
                return 1;
            }
        } else if (token == "--csv") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --csv\n";
                return 1;
            }
            R.out_csv = args[i++];
        } else if (token == "--seed") {
            if (i >= args.size()) {
                std::cerr << "Missing value after --seed\n";
                return 1;
            }
            if (!parse_uint64_arg(args[i++].c_str(), R.seed_val)) {
                std::cerr << "Bad --seed value\n";
                return 1;
            }
            R.have_seed = true;
        } else {
            std::cerr << "Unknown argument: " << token << "\n";
            return 1;
        }
    }

    // Проверки параметров
    if (!cooling_given) {
        std::cerr << "You must provide --cooling ...\n";
        return 1;
    }
    if (R.M_list.empty()) {
        std::cerr << "You must provide --M-list\n";
        return 1;
    }
    if (R.N_list.empty()) {
        std::cerr << "You must provide --N-list\n";
        return 1;
    }
    if (R.p_min < 1 || R.p_max < R.p_min) {
        std::cerr << "Invalid p-min/p-max\n";
        return 1;
    }
    if (R.runs == 0) {
        std::cerr << "--runs must be >= 1\n";
        return 1;
    }
    if (R.out_csv.empty()) {
        std::cerr << "You must provide --csv <output_file>\n";
        return 1;
    }
    if (R.mode != "seq" && R.mode != "par") {
        std::cerr << "Bad mode (must be seq or par)\n";
        return 1;
    }
    if (R.mode == "par") {
        if (R.n_threads < 1) {
            std::cerr << "For mode=par you must have --threads >= 1\n";
            return 1;
        }
        if (R.outer_no_improve_limit < 1) {
            std::cerr << "For mode=par you must have --outer-no-improve >= 1\n";
            return 1;
        }
    }

    // Открываем CSV выход
    std::ofstream fout(R.out_csv);
    if (!fout.is_open()) {
        std::cerr << "Cannot open output csv: " << R.out_csv << "\n";
        return 1;
    }
    fout << "M,N,avg_time_ms,best_cost\n";

    // RNG для генерации инстансов
    std::mt19937_64 global_rng;
    if (R.have_seed) {
        global_rng.seed(R.seed_val);
    } else {
        std::random_device rd;
        global_rng.seed(((std::uint64_t)rd() << 32) ^ (std::uint64_t)rd());
    }

    // Перебор по M_list x N_list
    for (int M : R.M_list) {
        for (int N : R.N_list) {
            if (M <= 0 || N <= 0) {
                std::cerr << "Skipping invalid pair M=" << M << " N=" << N << "\n";
                continue;
            }

            // Генерируем ОДИН экземпляр для этой пары (M,N).
            // Все прогоны runs будут на одном и том же инстансе — так мы сравним
            // только стохастику SA, не меняя саму задачу.
            ProblemInstance inst = generateRandomInstance(
                M, N, R.p_min, R.p_max, global_rng
            );

            long long total_time_ms = 0;
            CostType global_best_cost = std::numeric_limits<CostType>::max();

            for (std::size_t r = 0; r < R.runs; ++r) {
                // Для последовательного режима создаём rng на основе глобального,
                // чтобы получить разные ситы.
                std::uint64_t seed_local = global_rng();
                std::mt19937_64 local_rng(seed_local);

                std::pair<CostType,long long> result;
                if (R.mode == "seq") {
                    result = run_single_seq(inst, R, local_rng);
                } else {
                    // par режим не использует local_rng внутри непосредственно
                    // (ParallelAnnealerManager сам сидит потоки),
                    // но seed_local всё равно сдвигает global_rng, так что
                    // между повторами состояние хоть как-то меняется.
                    (void)local_rng;
                    result = run_single_par(inst, R);
                }

                CostType best_cost = result.first;
                long long dur_ms   = result.second;

                total_time_ms += dur_ms;
                if (best_cost < global_best_cost) {
                    global_best_cost = best_cost;
                }
            }

            double avg_time_ms =
                static_cast<double>(total_time_ms) / static_cast<double>(R.runs);

            fout << M << ","
                 << N << ","
                 << avg_time_ms << ","
                 << global_best_cost << "\n";

            std::cout << "[M=" << M << " N=" << N
                      << "] avg_time_ms=" << avg_time_ms
                      << " best_cost=" << global_best_cost << "\n";
        }
    }

    fout.close();
    if (!fout.good()) {
        std::cerr << "I/O error while writing CSV\n";
        return 1;
    }

    return 0;
}
