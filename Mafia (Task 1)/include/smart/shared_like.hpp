#pragma once

#include <atomic>       // std::atomic
#include <cstddef>      // std::size_t
#include <type_traits>  // std::convertible_to
#include <utility>      // std::forward, std::swap
#include <new>          // std::nothrow (на будущее)

/**
 * \file
 * \brief Лёгкая реализация «shared-подобного» умного указателя без weak/aliasing — smart::shared_like<T>.
 *
 * Основные особенности:
 *  - Одно выделение под control block + сам объект (через ControlBlock<T>).
 *  - Счётчик ссылок потокобезопасен (std::atomic<size_t>), инкремент/декремент — lock-free.
 *  - Поддерживает конвертацию shared_like<Derived> -> shared_like<Base>, если U* => T* конвертируемо.
 *  - Нет weak-поддержки, нет custom deleter, нет aliasing-конструктора (умышленно для простоты).
 *  - Совместим по интерфейсу с подмножеством std::shared_ptr: get, operator*, operator->, use_count, reset, swap.
 *
 * Ограничения:
 *  - Не поддерживается контроль цикла жизни через weak-ссылки — используйте с осторожностью в графах.
 *  - Нет пользовательских аллокаторов/делитеров (ControlBlock<T> освобождается через delete this).
 *  - Нет конструкторов с «сырого» указателя — объект всегда создаётся через make_shared_like(...).
 */

namespace smart {

/* =================================================================================================
 *                                   Control block (type-erased)
 * ================================================================================================= */

/**
 * \brief Базовый type-erased control block, содержащий счётчик ссылок и виртуальные операции
 *        доступа к «сырым» указателям и уничтожения.
 *
 * Семантика:
 *  - refcnt всегда >= 1 у только что созданного блока, т.к. создаётся первая владеющая ссылка.
 *  - destroy() обязан уничтожить *весь* control block (обычно `delete this`).
 */
struct ControlBlockBase {
    /// Счётчик совместного владения.
    std::atomic<std::size_t> refcnt{1};

    /// Невиртуальный доступ к размещённому значению как к void* (для небезопасного downcast-a в shared_like<T>).
    virtual void*       get_void() noexcept = 0;
    /// Константный доступ к значению.
    virtual const void* get_void() const noexcept = 0;

    /// Уничтожение control block (должно освобождать и объект, и сам блок).
    virtual void        destroy() noexcept = 0;

    virtual ~ControlBlockBase() = default;
};

/**
 * \tparam T тип управляемого объекта.
 * \brief Конкретный control block, который хранит сам объект T inline (без отдельного new для T).
 *
 * Модель памяти:
 *  - Доступ через get_void() безопасен только в рамках инвариантов shared_like.
 *  - Потокобезопасность владения обеспечивается атомиками refcnt на уровне shared_like.
 */
template <class T>
struct ControlBlock final : ControlBlockBase {
    /// Конструирует value на месте, пробрасывая аргументы в T::T(Args...).
    template <class... Args>
    explicit ControlBlock(Args&&... args)
        : value(std::forward<Args>(args)...) {}

    /// Размещаемый объект.
    T value;

    /* Virtual API реализация */
    void*       get_void() noexcept override       { return static_cast<void*>(&value); }
    const void* get_void() const noexcept override { return static_cast<const void*>(&value); }
    void        destroy() noexcept override        { delete this; } // self-delete: освобождает и value, и блок
};

/* =================================================================================================
 *                                         shared_like<T>
 * ================================================================================================= */

/**
 * \tparam T управляемый тип (обычно неполиморфный или полиморфный c виртуальным деструктором).
 * \brief Небольшой аналог std::shared_ptr без weak/aliasing.
 *
 * Гарантии исключений:
 *  - Все операции — noexcept. Потенциальные исключения возникают только при выделении памяти
 *    в make_shared_like (в этом файле оно не помечено noexcept умышленно — пусть исключение поднимется).
 *
 * Потокобезопасность:
 *  - Инкремент/декремент счётчика — атомарны.
 *  - Доступ к самому объекту T не синхронизирован; синхронизация на уровне пользователя.
 */
template <class T>
class shared_like {
public:
    using element_type = T;

    /* ----------------------------------- ctors ----------------------------------- */

    /// Пустой указатель (nullptr).
    constexpr shared_like() noexcept = default;

    /// Копирование того же типа: разделяет владение, ++refcnt.
    shared_like(const shared_like& other) noexcept : ctrl_(other.ctrl_) { add_ref_(); }

    /// Перемещение того же типа: перенос владения, other становится пустым.
    shared_like(shared_like&& other) noexcept : ctrl_(other.ctrl_) { other.ctrl_ = nullptr; }

    /**
     * \brief Копирующий КОНВЕРТИРУЮЩИЙ ctor: shared_like<U> -> shared_like<T>,
     *        если U* неявно конвертируем в T* (например, Derived* -> Base*).
     */
    template <class U>
    requires std::convertible_to<U*, T*>
    shared_like(const shared_like<U>& other) noexcept : ctrl_(other.ctrl_) { add_ref_(); }

    /**
     * \brief Перемещающий КОНВЕРТИРУЮЩИЙ ctor: shared_like<U> -> shared_like<T>,
     *        переносит владение (other обнуляется).
     */
    template <class U>
    requires std::convertible_to<U*, T*>
    shared_like(shared_like<U>&& other) noexcept : ctrl_(other.ctrl_) { other.ctrl_ = nullptr; }

    /* ----------------------------------- dtor ------------------------------------ */

    /// Деструктор: --refcnt и, если это была последняя ссылка, destroy() control block.
    ~shared_like() { release_(); }

    /* --------------------------------- assign ops -------------------------------- */

    /// Копирующее присваивание: освобождает прежнее владение, ++refcnt у нового.
    shared_like& operator=(const shared_like& other) noexcept {
        if (this == &other) return *this;
        release_();
        ctrl_ = other.ctrl_;
        add_ref_();
        return *this;
    }

    /// Перемещающее присваивание: переносит владение (self-safe).
    shared_like& operator=(shared_like&& other) noexcept {
        if (this == &other) return *this;
        release_();
        ctrl_ = other.ctrl_;
        other.ctrl_ = nullptr;
        return *this;
    }

    /* --------------------------------- observers --------------------------------- */

    /// \return сырой указатель на T или nullptr, если пусто.
    T*       get()       noexcept { return ctrl_ ? static_cast<T*>(ctrl_->get_void()) : nullptr; }
    /// \return сырой const-указатель на T или nullptr.
    const T* get() const noexcept { return ctrl_ ? static_cast<const T*>(ctrl_->get_void()) : nullptr; }

    /// Разыменование (предполагается, что get() != nullptr).
    T&       operator*()       noexcept { return *get(); }
    const T& operator*() const noexcept { return *get(); }

    /// Доступ к членам (предполагается, что get() != nullptr).
    T*       operator->()       noexcept { return get(); }
    const T* operator->() const noexcept { return get(); }

    /// Проверка на непустоту.
    explicit operator bool() const noexcept { return ctrl_ != nullptr; }

    /// \return текущее значение счётчика владения (0, если пусто).
    std::size_t use_count() const noexcept {
        return ctrl_ ? ctrl_->refcnt.load(std::memory_order_acquire) : 0;
    }

    /* --------------------------------- modifiers --------------------------------- */

    /// Сброс владения (при необходимости уничтожит control block).
    void reset() noexcept {
        release_();
        ctrl_ = nullptr;
    }
    void swap(shared_like& other) noexcept { std::swap(ctrl_, other.ctrl_); }

    /* сравнение */
    friend bool operator==(const shared_like& a, const shared_like& b) noexcept {
        return a.get() == b.get();
    }
    friend bool operator!=(const shared_like& a, const shared_like& b) noexcept {
        return !(a == b);
    }
    friend bool operator==(const shared_like& a, std::nullptr_t) noexcept {
        return a.get() == nullptr;
    }
    friend bool operator==(std::nullptr_t, const shared_like& b) noexcept {
        return b.get() == nullptr;
    }
    friend bool operator!=(const shared_like& a, std::nullptr_t) noexcept {
        return !(a == nullptr);
    }
    friend bool operator!=(std::nullptr_t, const shared_like& b) noexcept {
        return !(nullptr == b);
    }
    // (опционально) строгий порядок — по адресу control block:
    friend bool operator<(const shared_like& a, const shared_like& b) noexcept {
        return a.ctrl_ < b.ctrl_;
    }

    /* friends */
    template <class U, class... Args>
    friend shared_like<U> make_shared_like(Args&&... args);

    template <class> friend class shared_like;

private:
    /// Внутренний приватный ctor: от готового control block.
    explicit shared_like(ControlBlockBase* cb) noexcept : ctrl_(cb) {}

    /// Увеличить refcnt, если ctrl_ не nullptr.
    void add_ref_() noexcept {
        if (ctrl_) ctrl_->refcnt.fetch_add(1, std::memory_order_acq_rel);
    }

    /// Уменьшить refcnt и уничтожить control block при достижении нуля.
    void release_() noexcept {
        if (!ctrl_) return;
        if (ctrl_->refcnt.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            ctrl_->destroy(); // delete this; внутри ControlBlock<T>
        }
        ctrl_ = nullptr;
    }

private:
    /// Указатель на общий control block; nullptr означает «пустой» shared_like.
    ControlBlockBase* ctrl_{nullptr};
};

/* =================================================================================================
 *                                 make_shared_like<T>(...)
 * ================================================================================================= */

/**
 * \brief Фабричная функция, создающая control block и размещающая в нём объект T.
 *
 * \tparam T    тип создаваемого объекта
 * \tparam Args параметры, пробрасываемые в T::T(Args...)
 * \return      владеющий shared_like<T>
 *
 * Гарантии:
 *  - В случае неудачи выделения памяти пробрасывает std::bad_alloc (как и std::make_shared).
 *  - Нет поддержки custom deleter/allocator: освобождение всегда через delete ControlBlock<T>.
 */
template <class T, class... Args>
shared_like<T> make_shared_like(Args&&... args) {
    auto* cb = new ControlBlock<T>(std::forward<Args>(args)...);
    return shared_like<T>(cb);
}

} // namespace smart
