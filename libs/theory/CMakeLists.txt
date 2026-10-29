# deepwater::theory — teoría musical de la suite (nació con Tidechart, Fase 2).
#
# Sin JUCE y sin GUI: C++20 estándar, valores puros y deterministas (entra una tonalidad o un acorde, sale una lista de
# notas o un texto). La va a reutilizar un servidor MCP para que Claude componga dentro de FL. Lo que puede correr en
# el hilo de audio (escalas, acordes, sus notas MIDI) va en headers y no reserva memoria; en src/ va lo que trabaja con
# texto o con listas (leer y escribir símbolos, reconocer acordes). Se enlaza:
#   target_link_libraries(<Plugin> PRIVATE deepwater::theory)     y se incluye   #include "deepwater/theory/Chord.h"

add_library(deepwater_theory STATIC
    src/ChordFinder.cpp
    src/KeyDetection.cpp
    src/Melody.cpp
    src/Progression.cpp
    src/Text.cpp
)
add_library(deepwater::theory ALIAS deepwater_theory)

target_include_directories(deepwater_theory PUBLIC include)
target_compile_features(deepwater_theory PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(deepwater_theory PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(deepwater_theory PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Tests ---------------------------------------------------------------------
# ctest --test-dir build -C Release --output-on-failure   (o build/libs/theory/Release/DeepwaterTheoryTests.exe)
option(DEEPWATER_THEORY_BUILD_TESTS "Compilar los tests de la librería de teoría" ${DEEPWATER_BUILD_TESTS})

if(DEEPWATER_THEORY_BUILD_TESTS)
    add_executable(DeepwaterTheoryTests tests/TheoryTests.cpp)
    target_link_libraries(DeepwaterTheoryTests PRIVATE deepwater::theory)
    if(MSVC)
        target_compile_options(DeepwaterTheoryTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(DeepwaterTheoryTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME DeepwaterTheoryTests COMMAND DeepwaterTheoryTests)
endif()
