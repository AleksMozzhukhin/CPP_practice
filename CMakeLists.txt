cmake_minimum_required(VERSION 3.20)

project(mafia_sim CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_library(mafia_core STATIC
        src/core/game_engine.cpp
        src/core/game_state.cpp
        src/core/moderator.cpp
        src/core/phases.cpp
        src/core/rng.cpp

        src/roles/base_player.cpp
        src/roles/citizen.cpp
        src/roles/mafia.cpp
        src/roles/detective.cpp
        src/roles/doctor.cpp
        src/roles/maniac.cpp
        src/roles/human.cpp

        src/yaml/yaml_loader.cpp
)

target_link_libraries(mafia_core
        PUBLIC Threads::Threads
)

add_executable(mafia_app
        app/main.cpp
)

target_link_libraries(mafia_app
        PRIVATE mafia_core
)

set_target_properties(mafia_app PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

message(STATUS "CMAKE_CXX_COMPILER_ID = ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_FLAGS       = ${CMAKE_CXX_FLAGS}")
message(STATUS "Build type            = ${CMAKE_BUILD_TYPE}")
