cmake_minimum_required(VERSION 3.20)

project(mafia_sim CXX)

# --- Компилятор / стандарт ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Предупреждения
if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --- Потоки ---
find_package(Threads REQUIRED)

# --- Включаемые директории ---
include_directories(${CMAKE_SOURCE_DIR}/include)

# --- Библиотека ядра ---
add_library(mafia_core STATIC
        # core
        src/core/game_engine.cpp
        src/core/game_engine_coro.cpp       # <-- корутинный движок
        src/core/game_state.cpp
        src/core/moderator.cpp
        src/core/phases.cpp
        src/core/rng.cpp

        # roles
        src/roles/base_player.cpp
        src/roles/citizen.cpp
        src/roles/mafia.cpp
        src/roles/detective.cpp
        src/roles/doctor.cpp
        src/roles/maniac.cpp
        src/roles/human.cpp

        # yaml
        src/yaml/yaml_loader.cpp

        # concepts (compile-time проверки)
        src/concepts/role_checks.cpp
        src/concepts/role_api_checks.cpp
)

target_link_libraries(mafia_core
        PUBLIC Threads::Threads
)

# --- Приложение ---
add_executable(mafia_app
        app/main.cpp
)

target_link_libraries(mafia_app
        PRIVATE mafia_core
)

# --- Вывод бинарников в build/bin ---
set_target_properties(mafia_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Диагностика конфигурации ---
message(STATUS "CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_FLAGS       = ${CMAKE_CXX_FLAGS}")
message(STATUS "Build type            = ${CMAKE_BUILD_TYPE}")
