#pragma once
#include <string>
#include "core/types.hpp"

namespace core { class Moderator; }

namespace roles {

    // Команды (фракции)
    enum class Team {
        Town,    // Мирные
        Mafia,   // Мафия
        Maniac   // Маньяк (одиночка)
    };

    // Роли игроков (обязательные + выбранные 3 дополнительные из ТЗ)
    enum class Role {
        Citizen,     // Мирный житель
        Mafia,       // Мафия (рядовой)
        Detective,   // Комиссар (проверяет или стреляет ночью; оба действия в одну ночь недопустимы)
        Doctor,      // Доктор (лечит; ограничения см. ТЗ)
        Maniac,      // Маньяк (одиночка; каждую ночь убивает)

        // Дополнительные роли из ТЗ (выбраны как наиболее простые в реализации):
        Executioner, // Палач — ходит только при ничьей днём: может казнить одного из набравших максимум или воздержаться
        Journalist,  // Журналист — сравнивает двух жителей на «одинаковость» статусов (без раскрытия статусов), себя сравнивать нельзя
        Eavesdropper // Ушастик — подслушивает: узнаёт, было ли этой ночью действие на выбранного персонажа и какое именно (без статусов)
    };

    // Интерфейс игрока
    class IPlayer {
    public:
        virtual ~IPlayer() = default;

        // Базовые свойства
        virtual core::PlayerId id() const noexcept = 0;
        virtual const std::string& name() const noexcept = 0;
        virtual bool is_alive() const noexcept = 0;
        virtual Team team() const noexcept = 0;
        virtual Role role() const noexcept = 0;

        // Смерть игрока
        virtual void kill() noexcept = 0;

        // Хуки фаз
        virtual void on_day(core::Moderator& mod) = 0;
        virtual core::PlayerId vote_day(core::Moderator& mod) = 0;
        virtual void on_night(core::Moderator& mod) = 0;
    };

} // namespace roles
