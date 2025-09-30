#pragma once
#include <coroutine>
#include <vector>
#include <functional>
#include <utility>
#include <cstddef>

#include "coro/task.hpp"

namespace coro {

/**
 * PhaseBarrier — однотредовый (кооперативный) барьер для корутин.
 * Многоразовый: после набора expected участников выполняет on_complete(),
 * затем резюмирует всех ожидающих и сбрасывает состояние (arrived -> 0).
 *
 * Использование в корутине:
 *   co_await day_start.arrive();
 *   // ... действия дня ...
 *   co_await day_end.arrive();
 */
class PhaseBarrier {
public:
    using Callback = std::function<void()>;
    using handle_t = std::coroutine_handle<>;

    explicit PhaseBarrier(std::size_t expected, Callback on_complete = {})
        : expected_(expected), on_complete_(std::move(on_complete)) {}

    void set_expected(std::size_t n) noexcept { expected_ = n; }
    std::size_t expected() const noexcept { return expected_; }

    void set_on_complete(Callback cb) { on_complete_ = std::move(cb); }

    // awaitable для co_await
    struct Awaiter {
        PhaseBarrier& b;
        bool await_ready() const noexcept { return false; }
        void await_suspend(handle_t h) {
            b.on_arrive_(h);
        }
        void await_resume() const noexcept {}
    };

    // Точка входа для корутин
    Awaiter arrive() noexcept { return Awaiter{*this}; }

private:
    void on_arrive_(handle_t h) {
        waiters_.push_back(h);
        ++arrived_;
        if (arrived_ == expected_) {
            arrived_ = 0;

            // Выполнить callback (разрешение фазы), затем разбудить всех
            if (on_complete_) on_complete_();

            auto ws = std::move(waiters_);
            waiters_.clear();
            waiters_.shrink_to_fit(); // чтобы не держать лишнюю память между фазами

            for (auto hh : ws) {
                if (hh && !hh.done()) hh.resume();
            }
        }
    }

    std::size_t expected_{0};
    std::size_t arrived_{0};
    std::vector<handle_t> waiters_;
    Callback on_complete_{};
};

/**
 * Простейший кооперативный «планировщик»: хранит набор корутин-задач и
 * один раз «запускает» их до первой точки ожидания (initial suspend -> first await).
 * Далее корутины будут резюмироваться сами через PhaseBarrier.
 */
class Scheduler {
public:
    void spawn(task&& t) { tasks_.push_back(std::move(t)); }

    // Запустить все корутины до первой точки ожидания
    void start_all() {
        for (auto& t : tasks_) {
            if (!t.done()) t.resume();
        }
    }

    // Все ли задачи завершены
    bool all_done() const {
        for (auto const& t : tasks_) {
            if (!t.done()) return false;
        }
        return true;
    }

    // Принудительный прогон «насоса»: полезен, если где-то остались задачи без барьеров.
    void pump_once() {
        for (auto& t : tasks_) {
            if (!t.done()) t.resume();
        }
    }

    std::size_t size() const noexcept { return tasks_.size(); }

private:
    std::vector<task> tasks_;
};

} // namespace coro
