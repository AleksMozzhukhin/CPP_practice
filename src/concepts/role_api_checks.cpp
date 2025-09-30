#include "concepts/role_api.hpp"

// Конкретные роли
#include "roles/citizen.hpp"
#include "roles/mafia.hpp"
#include "roles/detective.hpp"
#include "roles/doctor.hpp"
#include "roles/maniac.hpp"
#include "roles/human.hpp"
#include "roles/executioner.hpp"
#include "roles/journalist.hpp"
#include "roles/eavesdropper.hpp"

namespace role_api_checks {

    // Все роли должны удовлетворять базовому контракту (on_day, vote_day, on_night)
    static_assert(concepts_mafia::BasicRole<roles::Citizen>,      "Citizen must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Mafia>,        "Mafia must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Detective>,    "Detective must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Doctor>,       "Doctor must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Maniac>,       "Maniac must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Human>,        "Human must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Executioner>,  "Executioner must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Journalist>,   "Journalist must satisfy BasicRole");
    static_assert(concepts_mafia::BasicRole<roles::Eavesdropper>, "Eavesdropper must satisfy BasicRole");

    // Дополнительный контракт решающего при ничьей (Палач и Human)
    static_assert(concepts_mafia::ExecutionerRole<roles::Executioner>,
                  "Executioner must satisfy ExecutionerRole (decide_execution)");
    static_assert(concepts_mafia::ExecutionerRole<roles::Human>,
                  "Human must satisfy ExecutionerRole (decide_execution)");

} // namespace role_api_checks
