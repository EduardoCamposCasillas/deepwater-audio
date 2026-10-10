cmake_minimum_required(VERSION 3.22)

# Suite de plugins de audio (monorepo). Este archivo solo prepara lo común: el estándar de C++, JUCE (una sola vez para
# todos) y los tests; cada librería y cada plugin tienen su propio CMakeLists.txt en su carpeta.
project(DeepwaterAudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- JUCE vía FetchContent -----------------------------------------------------
# Se fija a un tag (no a una rama) para que el build sea reproducible:
# el mismo CMakeLists.txt siempre descarga exactamente el mismo JUCE.
# Se declara aquí y no en cada plugin: todos los plugins de la suite usan la misma versión.
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.2
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- Tests ---------------------------------------------------------------------
# enable_testing() tiene que estar en la raíz para que `ctest --test-dir build` vea los tests de todas las carpetas.
option(DEEPWATER_BUILD_TESTS "Compilar los tests unitarios de la suite" ON)
if(DEEPWATER_BUILD_TESTS)
    enable_testing()
endif()

# --- Plugins -------------------------------------------------------------------
add_subdirectory(plugins/undertow)
