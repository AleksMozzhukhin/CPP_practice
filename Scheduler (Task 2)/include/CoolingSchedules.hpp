// include/CoolingSchedules.hpp
#pragma once

#include "ICoolingSchedule.hpp"

#include <algorithm>
#include <stdexcept>
#include <cmath>
#include <cstddef>

namespace sched {

/**
 * @brief Базовый вспомогательный класс с минимальной защитой от вырождения температуры.
 *
 * Во всех конкретных реализациях температура не должна становиться неположительной.
 * Мы будем зажимать её снизу константой kMinT.
 */
class CoolingScheduleBase : public ICoolingSchedule {
protected:
    static constexpr double kMinT = 1e-12; // жёсткий нижний предел температуры

    double T0_;          // начальная температура
    double Tcurr_;       // текущая температура
    std::size_t step_;   // номер шага охлаждения (итерации), начиная с 0

    CoolingScheduleBase(double T0)
        : T0_(T0),
          Tcurr_(T0),
          step_(0)
    {
        if (!(T0 > 0.0)) {
            throw std::invalid_argument("CoolingSchedule: initial temperature T0 must be > 0");
        }
        if (!(Tcurr_ > 0.0)) {
            Tcurr_ = kMinT;
        }
    }

public:
    double current_temperature() const override {
        return Tcurr_;
    }

    void reset() override {
        Tcurr_ = T0_;
        step_ = 0;
        if (!(Tcurr_ > 0.0)) {
            Tcurr_ = kMinT;
        }
    }

protected:
    // Вспомогательная функция для "зажатия" температуры.
    static double clamp_positive(double t) {
        if (!(t > 0.0)) {
            return kMinT;
        }
        if (t < kMinT) {
            return kMinT;
        }
        return t;
    }
};

/**
 * @brief Геометрическое охлаждение:
 *
 *   T_{k+1} = alpha * T_k,
 *   где 0 < alpha < 1.
 *
 * Параметры конструктора:
 *   - T0     : начальная температура
 *   - alpha  : коэффициент геометрического спада
 *
 * Требования:
 *   - T0 > 0
 *   - 0 < alpha < 1
 */
class GeometricCooling final : public CoolingScheduleBase {
private:
    double alpha_;

public:
    GeometricCooling(double T0, double alpha)
        : CoolingScheduleBase(T0),
          alpha_(alpha)
    {
        if (!(alpha_ > 0.0 && alpha_ < 1.0)) {
            throw std::invalid_argument("GeometricCooling: alpha must be in (0,1)");
        }
    }

    void next_step() override {
        // T_{k+1} = alpha * T_k
        Tcurr_ = clamp_positive(Tcurr_ * alpha_);
        ++step_;
    }
};

/**
 * @brief Линейное (арифметическое) охлаждение:
 *
 *   T_{k+1} = T_k - beta,
 *   где beta > 0.
 *
 * Температура не должна упасть ниже положительного минимума.
 *
 * Параметры конструктора:
 *   - T0    : начальная температура
 *   - beta  : шаг убывания
 *
 * Требования:
 *   - T0 > 0
 *   - beta > 0
 *   - beta < T0 (иначе температура моментально провалится почти в ноль)
 *
 * Замечание:
 *   Если T_k - beta <= 0, мы зажимаем температуру в kMinT,
 *   чтобы exp(-Δ/T) не схлопнулась в числовую сингулярность.
 */
class LinearCooling final : public CoolingScheduleBase {
private:
    double beta_;

public:
    LinearCooling(double T0, double beta)
        : CoolingScheduleBase(T0),
          beta_(beta)
    {
        if (!(beta_ > 0.0)) {
            throw std::invalid_argument("LinearCooling: beta must be > 0");
        }
        if (!(T0_ > beta_)) {
            // Не запрещаем жёстко, но предупреждаем через исключение,
            // чтобы пользователь осознанно выбрал параметры.
            // Можно было бы просто допустить, но тогда температура очень быстро
            // упадёт к минимуму kMinT.
            throw std::invalid_argument("LinearCooling: expected T0 > beta for smoother cooling");
        }
    }

    void next_step() override {
        // T_{k+1} = T_k - beta
        Tcurr_ = clamp_positive(Tcurr_ - beta_);
        ++step_;
    }
};

/**
 * @brief Охлаждение типа "Коши"/рациональное/логарифмическое:
 *
 * Стандартный вид:
 *   T_k = T0 / (1 + gamma * k)
 *
 * Где:
 *   - T0 > 0
 *   - gamma > 0
 *   - k = 0,1,2,...
 *
 * Такое охлаждение замедляется с ростом k, температура стремится к 0 асимптотически.
 *
 * Параметры конструктора:
 *   - T0     : начальная температура
 *   - gamma  : скорость убывания
 *
 * Требования:
 *   - T0 > 0
 *   - gamma > 0
 */
class CauchyCooling final : public CoolingScheduleBase {
private:
    double gamma_;

public:
    CauchyCooling(double T0, double gamma)
        : CoolingScheduleBase(T0),
          gamma_(gamma)
    {
        if (!(gamma_ > 0.0)) {
            throw std::invalid_argument("CauchyCooling: gamma must be > 0");
        }
        // Пересчёт Tcurr_ на шаге 0 не требуется: Tcurr_ уже T0.
    }

    void next_step() override {
        // step_ будет увеличен после вычисления, но формула использует (step_+1)
        // чтобы фактически дать T_{k+1}.
        std::size_t k_next = step_ + 1;
        double denom = 1.0 + gamma_ * static_cast<double>(k_next);
        double newT = T0_ / denom;
        Tcurr_ = clamp_positive(newT);
        step_ = k_next;
    }
};

} // namespace sched
