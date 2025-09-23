#pragma once
#include <atomic>
#include <cstddef>
#include <new>
#include <utility>
#include <type_traits>
#include <cassert>

namespace smart {

    // Запрещаем массивы как параметр T (аналогично std::shared_ptr)
    template <class T>
    inline constexpr bool is_supported_shared_like_v =
        !std::is_array_v<T> && !std::is_void_v<T>;

    // ---- тип-стёртая иерархия control blocks (единая для всех T) ----
    struct ControlBlockBase {
        std::atomic<std::size_t> strong{1};
        virtual void* get_ptr_void() noexcept = 0;
        virtual void  destroy_object() noexcept = 0;
        virtual ~ControlBlockBase() = default;
    };

    template <class U>
    struct HeapControlBlock final : ControlBlockBase {
        U* p{nullptr};
        explicit HeapControlBlock(U* ptr) noexcept : p(ptr) {}
        void* get_ptr_void() noexcept override { return p; }
        void  destroy_object() noexcept override { delete p; p = nullptr; }
    };

    template <class U>
    struct InplaceControlBlock final : ControlBlockBase {
        alignas(U) unsigned char storage[sizeof(U)];
        bool constructed{false};

        template <class... Args>
        explicit InplaceControlBlock(Args&&... args) {
            ::new (storage) U(std::forward<Args>(args)...);
            constructed = true;
        }
        U* get_ptr() noexcept {
            return constructed ? std::launder(reinterpret_cast<U*>(storage)) : nullptr;
        }
        void* get_ptr_void() noexcept override { return get_ptr(); }
        void  destroy_object() noexcept override {
            if (constructed) {
                get_ptr()->~U();
                constructed = false;
            }
        }
    };

    template <class T>
    class shared_like {
        static_assert(is_supported_shared_like_v<T>,
                      "smart::shared_like<T>: T must be a non-void, non-array type");
    public:
        using element_type = T;

        // ----------------- ctors / dtor -----------------
        constexpr shared_like() noexcept = default;

        // Конструктор из «сырого» указателя: shared_like принимает владение
        explicit shared_like(T* ptr) {
            if (ptr) {
                ctrl_ = new HeapControlBlock<T>(ptr);
            }
        }

        // Копирование
        shared_like(const shared_like& other) noexcept : ctrl_(other.ctrl_) { inc_(); }

        // Перемещение
        shared_like(shared_like&& other) noexcept : ctrl_(other.ctrl_) { other.ctrl_ = nullptr; }

        // Ковариантное копирование/перемещение (U* -> T*)
        template <class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        shared_like(const shared_like<U>& other) noexcept : ctrl_(other.ctrl_) { inc_(); }

        template <class U, std::enable_if_t<std::is_convertible_v<U*, T*>, int> = 0>
        shared_like(shared_like<U>&& other) noexcept : ctrl_(other.ctrl_) { other.ctrl_ = nullptr; }

        // Деструктор
        ~shared_like() { release_(); }

        // Присваивание через copy-and-swap
        shared_like& operator=(shared_like rhs) noexcept { swap(rhs); return *this; }

        // ----------------- observers -----------------
        // Как у std::shared_ptr: const-метод get() возвращает T*
        T* get() noexcept { return ctrl_ ? static_cast<T*>(ctrl_->get_ptr_void()) : nullptr; }
        T* get() const noexcept { return ctrl_ ? static_cast<T*>(ctrl_->get_ptr_void()) : nullptr; }

        T& operator*() const noexcept {
            assert(get() && "smart::shared_like: dereferencing null");
            return *get();
        }

        T* operator->() const noexcept {
            assert(get() && "smart::shared_like: dereferencing null");
            return get();
        }

        explicit operator bool() const noexcept { return get() != nullptr; }

        std::size_t use_count() const noexcept {
            return ctrl_ ? ctrl_->strong.load(std::memory_order_acquire) : 0;
        }

        // ----------------- modifiers -----------------
        void reset() noexcept {
            release_();
            ctrl_ = nullptr;
        }

        void reset(T* p) {
            if (get() == p) return;
            release_();
            ctrl_ = (p ? new HeapControlBlock<T>(p) : nullptr);
        }

        void swap(shared_like& other) noexcept {
            using std::swap;
            swap(ctrl_, other.ctrl_);
        }

        // ----------------- comparisons -----------------
        friend bool operator==(const shared_like& a, const shared_like& b) noexcept {
            return a.get() == b.get();
        }
        friend bool operator!=(const shared_like& a, const shared_like& b) noexcept {
            return !(a == b);
        }
        friend bool operator<(const shared_like& a, const shared_like& b) noexcept {
            return a.get() < b.get();
        }

    private:
        template <class U>
        friend class shared_like;

        // Разрешаем фабрике доступ к приватному ctrl_
        template <class U, class... Args>
        friend shared_like<U> make_shared_like(Args&&... args);

        ControlBlockBase* ctrl_{nullptr};

        void inc_() noexcept {
            if (ctrl_) ctrl_->strong.fetch_add(1, std::memory_order_acq_rel);
        }

        void release_() noexcept {
            if (!ctrl_) return;
            if (ctrl_->strong.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                ctrl_->destroy_object();
                delete ctrl_;
            }
            ctrl_ = nullptr;
        }
    };

    // Фабрика «единого выделения» (аналог std::make_shared)
    template <class T, class... Args>
    shared_like<T> make_shared_like(Args&&... args) {
        auto* cb = new InplaceControlBlock<T>(std::forward<Args>(args)...);
        shared_like<T> res;
        res.ctrl_ = cb; // friend позволяет доступ
        return res;
    }

} // namespace smart
