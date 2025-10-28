#pragma once
#include <coroutine>
#include <utility>
#include <iterator>
#include <cstddef>
#include <exception>
#include <type_traits>
#include <memory>   // std::addressof

namespace util {

/**
 * generator<T> — лёгкий «ленивый» генератор значений на корутинах C++20.
 *
 * Пример использования:
 *   util::generator<int> seq(int n) {
 *       for (int i = 0; i < n; ++i) co_yield i;
 *   }
 *   for (int x : seq(3)) { ... }  // 0,1,2
 *
 * Характеристики:
 *   - Однопроходный input-итератор (std::input_iterator_tag).
 *   - Значения передаются по значению (T), чтобы избежать висячих ссылок.
 *   - Исключения внутри корутины приводят к std::terminate().
 */
template <class T>
class generator {
public:
    struct promise_type {
        T current_{};

        generator get_return_object() noexcept {
            return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }
        std::suspend_always final_suspend() const noexcept { return {}; }
        void return_void() const noexcept {}
        void unhandled_exception() { std::terminate(); }

        // Разрешаем co_yield как для rvalue, так и для lvalue
        std::suspend_always yield_value(const T& v) noexcept(std::is_nothrow_copy_assignable_v<T>) {
            current_ = v;
            return {};
        }
        std::suspend_always yield_value(T&& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
            current_ = std::move(v);
            return {};
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    // ctors / dtor
    generator() noexcept : h_(nullptr) {}
    explicit generator(handle_type h) noexcept : h_(h) {}

    generator(generator&& other) noexcept : h_(other.h_) { other.h_ = nullptr; }
    generator& operator=(generator&& other) noexcept {
        if (this != &other) {
            cleanup_();
            h_ = other.h_;
            other.h_ = nullptr;
        }
        return *this;
    }

    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;

    ~generator() { cleanup_(); }

    // итератор input-категории
    class iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::input_iterator_tag;
        using reference = const T&;
        using pointer = const T*;

        iterator() noexcept : h_(nullptr) {}
        explicit iterator(handle_type h) noexcept : h_(h) {}

        iterator& operator++() {
            h_.resume();
            return *this;
        }
        void operator++(int) { (void)operator++(); }

        reference operator*() const noexcept { return h_.promise().current_; }
        pointer operator->() const noexcept { return std::addressof(h_.promise().current_); }

        friend bool operator==(const iterator& it, std::default_sentinel_t) noexcept {
            return !it.h_ || it.h_.done();
        }
        friend bool operator!=(const iterator& it, std::default_sentinel_t s) noexcept {
            return !(it == s);
        }

    private:
        handle_type h_;
    };

    iterator begin() {
        if (h_) h_.resume();   // запустить до первого co_yield
        return iterator{h_};
    }

    std::default_sentinel_t end() const noexcept { return {}; }

private:
    void cleanup_() noexcept {
        if (h_) {
            h_.destroy();
            h_ = nullptr;
        }
    }

    handle_type h_;
};

} // namespace util
