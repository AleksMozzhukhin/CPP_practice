#pragma once
#include <type_traits>

#include "concepts/mafia_concepts.hpp"
#include "concepts/role_checks.hpp"
#include "concepts/state_checks.hpp"
#include "smart/shared_like.hpp"

// Базовый интерфейс игрока
#include "roles/i_player.hpp"

// Все конкретные роли
#include "roles/citizen.hpp"
#include "roles/mafia.hpp"
#include "roles/detective.hpp"
#include "roles/doctor.hpp"
#include "roles/maniac.hpp"
#include "roles/human.hpp"
#include "roles/executioner.hpp"
#include "roles/journalist.hpp"
#include "roles/eavesdropper.hpp"

namespace role_checks {

// --- Каждый класс роли обязан быть унаследован от roles::IPlayer ---
static_assert(concepts_mafia::PlayerLike<roles::Citizen>,      "Citizen must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Mafia>,        "Mafia must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Detective>,    "Detective must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Doctor>,       "Doctor must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Maniac>,       "Maniac must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Human>,        "Human must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Executioner>,  "Executioner must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Journalist>,   "Journalist must derive from IPlayer");
static_assert(concepts_mafia::PlayerLike<roles::Eavesdropper>, "Eavesdropper must derive from IPlayer");

// --- Наш smart::shared_like должен работать как «умный указатель» к IPlayer ---
static_assert(concepts_mafia::SharedLikePlayer<smart::shared_like<roles::IPlayer>>,
              "smart::shared_like<IPlayer> must satisfy SharedLikePlayer concept");

// Проверим также конверсию shared_like<Derived> -> shared_like<Base> на уровне типа:
using SL_I = smart::shared_like<roles::IPlayer>;
using SL_C = smart::shared_like<roles::Citizen>;
static_assert(std::is_constructible_v<SL_I, SL_C>, "shared_like<Derived> must convert to shared_like<Base>");

} // namespace role_checks
