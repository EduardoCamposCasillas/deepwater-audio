# deepwater::dsp — librería DSP compartida por todos los plugins de la suite.
#
# Sin JUCE y sin GUI: C++20 estándar. Casi todo son headers (las piezas que corren en el hilo de audio tienen que
# poder inlinearse dentro del bucle de cada plugin); en src/ va lo que se usa fuera del audio. Cada plugin la enlaza
# estáticamente:   target_link_libraries(<Plugin> PRIVATE deepwater::dsp)
# y la incluye como   #include "deepwater/dsp/Svf.h"

add_library(deepwater_dsp STATIC
    src/Fft.cpp
    src/SpectrumAnalyzer.cpp
)
add_library(deepwater::dsp ALIAS deepwater_dsp)

target_include_directories(deepwater_dsp PUBLIC include)
target_compile_features(deepwater_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(deepwater_dsp PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(deepwater_dsp PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --- Tests ---------------------------------------------------------------------
# ctest --test-dir build -C Release --output-on-failure   (o build/libs/dsp/Release/DeepwaterDspTests.exe)
option(DEEPWATER_DSP_BUILD_TESTS "Compilar los tests de la librería DSP" ${DEEPWATER_BUILD_TESTS})

if(DEEPWATER_DSP_BUILD_TESTS)
    add_executable(DeepwaterDspTests tests/DspLibraryTests.cpp)
    target_link_libraries(DeepwaterDspTests PRIVATE deepwater::dsp)
    if(MSVC)
        target_compile_options(DeepwaterDspTests PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(DeepwaterDspTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME DeepwaterDspTests COMMAND DeepwaterDspTests)
endif()
