# deepwater::presets — presets de la suite: formato XML legible, biblioteca (fábrica + carpeta del usuario), preset
# actual en el proyecto del host, y los componentes de la GUI (barra, navegador y panel de guardar).
#
# A diferencia de deepwater::dsp, usa JUCE. Por eso NO es una librería compilada aparte: es INTERFACE con fuentes, y
# cada plugin compila src/ con SU configuración de JUCE (los módulos de JUCE se compilan dentro de cada plugin; una
# librería estática con su propio JUCE duplicaría esos símbolos o los compilaría con otras opciones).
#   target_link_libraries(<Plugin> PRIVATE deepwater::presets)
#   #include "deepwater/presets/PresetManager.h"
# El plugin tiene que enlazar juce_audio_processors y juce_gui_basics (juce_audio_utils ya los trae).

add_library(deepwater_presets INTERFACE)
add_library(deepwater::presets ALIAS deepwater_presets)

target_sources(deepwater_presets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/PresetManager.cpp)
target_include_directories(deepwater_presets INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
