#pragma once
#include <coroutine>
#include <exception>
#include <utility>

namespace coro {

    /**
     * Простейший кооперативный таск без значения результата: task<void>.
     * Запускается/резюмируется вручную через .resume() (или из планировщика).
     */
    class task {
    public:
        struct promise_type {
            task get_return_object() noexcept {
                return task{std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            std::suspend_always initial_suspend() const noexcept { return {}; }
            std::suspend_always final_suspend() const noexcept { return {}; }
            void return_void() const noexcept {}
            void unhandled_exception() { std::terminate(); }
        };

        using handle_type = std::coroutine_handle<promise_type>;

        task() noexcept : h_(nullptr) {}
        explicit task(handle_type h) noexcept : h_(h) {}

        task(const task&) = delete;
        task& operator=(const task&) = delete;

        task(task&& other) noexcept : h_(other.h_) { other.h_ = nullptr; }
        task& operator=(task&& other) noexcept {
            if (this != &other) {
                destroy_();
                h_ = other.h_;
                other.h_ = nullptr;
            }
            return *this;
        }

        ~task() { destroy_(); }

        /// Запустить или продолжить выполнение корутины до следующей точки ожидания/завершения.
        void resume() {
            if (h_ && !h_.done()) h_.resume();
        }

        bool done() const noexcept {
            return !h_ || h_.done();
        }

        handle_type handle() const noexcept { return h_; }

    private:
        void destroy_() noexcept {
            if (h_) {
                h_.destroy();
                h_ = nullptr;
            }
        }

        handle_type h_;
    };

} // namespace coro
