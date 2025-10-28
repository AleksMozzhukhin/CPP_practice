// src/ProblemInstance.cpp

#include "ProblemInstance.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

namespace sched {

namespace {

// Вспомогательная функция: обрезка пробелов по краям строки.
static inline void trim(std::string& s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };

    // left trim
    s.erase(s.begin(),
            std::find_if(s.begin(), s.end(), not_space));
    // right trim
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(),
            s.end());
}

// Разбить строку по запятым на вектор подстрок (с trim у каждой).
static std::vector<std::string> splitCSVLine(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        trim(token);
        result.push_back(token);
    }
    // также поддержим вариант с завершающей запятой, но он даст пустой токен
    // который мы интерпретировать не будем. Лучше просто оставить.
    return result;
}

// Преобразовать строку к целому числу int.
// Возвращает false, если не удалось корректно преобразовать.
static bool parseInt(const std::string& s, int& value) {
    if (s.empty()) return false;
    // Проверим, что строка - потенциально число (допускаем ведущий минус)
    // Но нам нужны только неотрицательные здесь.
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c)) && c != '-') {
            return false;
        }
    }

    try {
        long long tmp = std::stoll(s);
        // Дополнительно проверим на переполнение int:
        if (tmp < static_cast<long long>(std::numeric_limits<int>::min()) ||
            tmp > static_cast<long long>(std::numeric_limits<int>::max())) {
            return false;
        }
        value = static_cast<int>(tmp);
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace


bool loadFromCSV(const std::string& path,
                 ProblemInstance& out,
                 std::string& error_msg)
{
    error_msg.clear();

    std::ifstream fin(path);
    if (!fin.is_open()) {
        error_msg = "Cannot open file: " + path;
        return false;
    }

    std::string line1, line2;

    if (!std::getline(fin, line1)) {
        error_msg = "File is empty or cannot read first line.";
        return false;
    }
    if (!std::getline(fin, line2)) {
        error_msg = "Cannot read second line with processing times.";
        return false;
    }

    // Парсинг первой строки: "M,N"
    auto tokens1 = splitCSVLine(line1);
    if (tokens1.size() != 2) {
        error_msg = "First line must contain exactly 2 comma-separated values: M,N";
        return false;
    }

    int M = 0;
    int N = 0;
    if (!parseInt(tokens1[0], M) || !parseInt(tokens1[1], N)) {
        error_msg = "Failed to parse M or N as integer.";
        return false;
    }

    if (M <= 0) {
        error_msg = "M must be >= 1.";
        return false;
    }
    if (N <= 0) {
        error_msg = "N must be >= 1.";
        return false;
    }

    // Парсинг второй строки: "p0,p1,...,p{N-1}"
    auto tokens2 = splitCSVLine(line2);
    if (static_cast<int>(tokens2.size()) != N) {
        error_msg = "Second line must contain exactly N processing times.";
        return false;
    }

    std::vector<int> p;
    p.reserve(N);

    for (int i = 0; i < N; ++i) {
        int val = 0;
        if (!parseInt(tokens2[i], val)) {
            error_msg = "Failed to parse processing time p[" + std::to_string(i) + "] as integer.";
            return false;
        }
        if (val < 1) {
            error_msg = "Processing time must be >= 1.";
            return false;
        }
        p.push_back(val);
    }

    // Если всё корректно — записываем в out.
    out = ProblemInstance(M, N, std::move(p));
    return true;
}


bool saveToCSV(const ProblemInstance& inst,
               const std::string& path,
               std::string& error_msg)
{
    error_msg.clear();

    if (inst.M <= 0) {
        error_msg = "Invalid instance: M must be >= 1.";
        return false;
    }
    if (inst.N <= 0) {
        error_msg = "Invalid instance: N must be >= 1.";
        return false;
    }
    if (static_cast<int>(inst.p.size()) != inst.N) {
        error_msg = "Invalid instance: size of p does not match N.";
        return false;
    }
    for (int i = 0; i < inst.N; ++i) {
        if (inst.p[i] < 1) {
            error_msg = "Invalid instance: processing time p[i] must be >= 1.";
            return false;
        }
    }

    std::ofstream fout(path);
    if (!fout.is_open()) {
        error_msg = "Cannot open file for writing: " + path;
        return false;
    }

    // Строка 1: "M,N"
    fout << inst.M << "," << inst.N << "\n";

    // Строка 2: "p0,p1,p2,..."
    for (int i = 0; i < inst.N; ++i) {
        fout << inst.p[i];
        if (i + 1 < inst.N) {
            fout << ",";
        }
    }
    fout << "\n";

    if (!fout.good()) {
        error_msg = "Write error (I/O failure).";
        return false;
    }

    return true;
}


ProblemInstance generateRandomInstance(int M,
                                       int N,
                                       int p_min,
                                       int p_max,
                                       std::mt19937_64& rng)
{
    if (M <= 0) {
        throw std::invalid_argument("generateRandomInstance: M must be >= 1");
    }
    if (N <= 0) {
        throw std::invalid_argument("generateRandomInstance: N must be >= 1");
    }
    if (p_min < 1) {
        throw std::invalid_argument("generateRandomInstance: p_min must be >= 1");
    }
    if (p_max < p_min) {
        throw std::invalid_argument("generateRandomInstance: p_max must be >= p_min");
    }

    std::uniform_int_distribution<int> dist(p_min, p_max);

    std::vector<int> p;
    p.reserve(N);
    for (int i = 0; i < N; ++i) {
        p.push_back(dist(rng));
    }

    return ProblemInstance(M, N, std::move(p));
}

} // namespace sched
