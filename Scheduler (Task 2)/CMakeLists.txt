cmake_minimum_required(VERSION 3.20)

project(sched_sa
        VERSION 1.0
        LANGUAGES CXX
)

# --------------------------------------------------
# Общие настройки компилятора
# --------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/W4 /permissive- /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------
# Библиотека schedlib
#
# Содержит реализацию задачи (ProblemInstance, ScheduleSolution).
# Остальные части (мутации, охлаждение, SA, ParallelAnnealerManager)
# оформлены как header-only и подключаются через include/.
# --------------------------------------------------

add_library(schedlib
        src/ProblemInstance.cpp
        src/ScheduleSolution.cpp
)

target_include_directories(schedlib
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# --------------------------------------------------
# Исполняемые файлы
# --------------------------------------------------

# sa_seq: последовательный имитационный отжиг
add_executable(sa_seq
        apps/sa_seq.cpp
)
target_link_libraries(sa_seq
        PRIVATE
        schedlib
)

# sa_par: параллельный имитационный отжиг на потоках
add_executable(sa_par
        apps/sa_par.cpp
)
target_link_libraries(sa_par
        PRIVATE
        schedlib
)

# generator: генератор входных данных
add_executable(generator
        apps/generator.cpp
)
target_link_libraries(generator
        PRIVATE
        schedlib
)

# research: массовые прогоны (seq/par), логирование времени и качества
add_executable(research
        apps/research.cpp
)
target_link_libraries(research
        PRIVATE
        schedlib
)

# --------------------------------------------------
# Потоки (pthread и т.п.)
#
# Параллельные версии (sa_par, research через ParallelAnnealerManager)
# используют std::thread. На Unix требуется линковка с pthread.
# --------------------------------------------------

if (UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(sa_par PRIVATE Threads::Threads)
    target_link_libraries(research PRIVATE Threads::Threads)
endif()

# --------------------------------------------------
# Группировка файлов в IDE (опционально)
# --------------------------------------------------

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/include PREFIX "Header Files" FILES
        include/ProblemInstance.hpp
        include/ScheduleSolution.hpp
        include/ISolution.hpp
        include/IMutation.hpp
        include/ICoolingSchedule.hpp
        include/MutationOperators.hpp
        include/CoolingSchedules.hpp
        include/SimulatedAnnealing.hpp
        include/ParallelAnnealerManager.hpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/src PREFIX "Source Files" FILES
        src/ProblemInstance.cpp
        src/ScheduleSolution.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR}/apps PREFIX "Apps" FILES
        apps/sa_seq.cpp
        apps/sa_par.cpp
        apps/generator.cpp
        apps/research.cpp
)

# --------------------------------------------------
# Инструкция по сборке:
#
#   mkdir build
#   cd build
#   cmake ..
#   cmake --build .
#
# Итоговые бинарники:
#   - sa_seq       (последовательный SA)
#   - sa_par       (параллельный SA)
#   - generator    (генератор экземпляров)
#   - research     (исследование масштабируемости)
# --------------------------------------------------
