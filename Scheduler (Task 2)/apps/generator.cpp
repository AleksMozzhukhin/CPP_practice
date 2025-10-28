// apps/generator.cpp

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "ProblemInstance.hpp"

using namespace sched;

// ------------------------
// Парсинг аргументов
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

static bool parse_uint64_arg(const char* s, std::uint64_t& out) {
    if (!s) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

// ------------------------
// main()
// ------------------------

/*
 * Пример запуска:
 *
 *   generator \
 *     --M 4 \
 *     --N 30 \
 *     --p-min 1 \
 *     --p-max 20 \
 *     --out inst_4x30.csv
 *
 * Необязательный параметр:
 *     --seed <uint64>
 * Если не указан, seed берётся из std::random_device.
 *
 * Формат выходного файла совместим с loadFromCSV():
 *   Строка 1: "M,N"
 *   Строка 2: "p0,p1,...,p{N-1}"
 */
int main(int argc, char** argv) {
    int M = -1;
    int N = -1;
    int pmin = -1;
    int pmax = -1;
    std::string out_path;

    bool have_seed = false;
    std::uint64_t seed_val = 0;

    // Разбираем аргументы
    for (int i = 1; i < argc; ) {
        std::string opt = argv[i++];
        if (opt == "--M") {
            if (i >= argc) {
                std::cerr << "Missing value after --M\n";
                return 1;
            }
            if (!parse_int_arg(argv[i++], M)) {
                std::cerr << "Bad --M value\n";
                return 1;
            }
        } else if (opt == "--N") {
            if (i >= argc) {
                std::cerr << "Missing value after --N\n";
                return 1;
            }
            if (!parse_int_arg(argv[i++], N)) {
                std::cerr << "Bad --N value\n";
                return 1;
            }
        } else if (opt == "--p-min") {
            if (i >= argc) {
                std::cerr << "Missing value after --p-min\n";
                return 1;
            }
            if (!parse_int_arg(argv[i++], pmin)) {
                std::cerr << "Bad --p-min value\n";
                return 1;
            }
        } else if (opt == "--p-max") {
            if (i >= argc) {
                std::cerr << "Missing value after --p-max\n";
                return 1;
            }
            if (!parse_int_arg(argv[i++], pmax)) {
                std::cerr << "Bad --p-max value\n";
                return 1;
            }
        } else if (opt == "--out") {
            if (i >= argc) {
                std::cerr << "Missing value after --out\n";
                return 1;
            }
            out_path = argv[i++];
        } else if (opt == "--seed") {
            if (i >= argc) {
                std::cerr << "Missing value after --seed\n";
                return 1;
            }
            if (!parse_uint64_arg(argv[i++], seed_val)) {
                std::cerr << "Bad --seed value\n";
                return 1;
            }
            have_seed = true;
        } else {
            std::cerr << "Unknown argument: " << opt << "\n";
            return 1;
        }
    }

    // Проверка обязательных аргументов
    if (M <= 0) {
        std::cerr << "You must specify --M >= 1\n";
        return 1;
    }
    if (N <= 0) {
        std::cerr << "You must specify --N >= 1\n";
        return 1;
    }
    if (pmin < 1) {
        std::cerr << "You must specify --p-min >= 1\n";
        return 1;
    }
    if (pmax < pmin) {
        std::cerr << "You must satisfy p-max >= p-min\n";
        return 1;
    }
    if (out_path.empty()) {
        std::cerr << "You must specify --out <file.csv>\n";
        return 1;
    }

    // Инициализируем ГПСЧ
    std::mt19937_64 rng;
    if (have_seed) {
        rng.seed(seed_val);
    } else {
        std::random_device rd;
        rng.seed(((std::uint64_t)rd() << 32) ^ (std::uint64_t)rd());
    }

    // Генерируем экземпляр задачи
    ProblemInstance inst = generateRandomInstance(M, N, pmin, pmax, rng);

    // Сохраняем в CSV
    std::string err;
    if (!saveToCSV(inst, out_path, err)) {
        std::cerr << "Error saving to " << out_path << ": " << err << "\n";
        return 1;
    }

    // Для удобства печатаем краткую сводку в stdout
    std::cout << "Generated instance:\n";
    std::cout << "  M = " << inst.M << "\n";
    std::cout << "  N = " << inst.N << "\n";
    std::cout << "  p range = [" << pmin << ", " << pmax << "]\n";
    std::cout << "Saved to " << out_path << "\n";

    return 0;
}
