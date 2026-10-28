# CLAUDE.md — Tidechart, herramienta MIDI de acordes y melodías por tonalidad

Plugin 5 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio de Tidechart.

Nombre de trabajo: **Tidechart** (una carta de mareas: el mapa de por dónde se puede mover la armonía). Se confirma
con el usuario al empezar la Fase 1, junto con su `PLUGIN_CODE` (propuesta: `Tdc1`); fabricante `Undw`, el de toda la
suite; `COMPANY_NAME "Deepwater"`.

## Objetivo del proyecto
Construir una herramienta MIDI de composición por tonalidad (referencia de nivel: Scaler 3 y Captain Chords), con
nombre, diseño y código propios: paleta de acordes de una tonalidad y modo, constructor de progresiones con buena
conducción de voces, patrones rítmicos, detección de tonalidad, generador de melodías y líneas de bajo, y arrastrar el
resultado al Piano Roll de FL Studio. No genera audio: recibe y emite MIDI, y lo que suena es el instrumento que
reciba su salida (Undertow u otro).

Es un plugin sin DSP: sirve como cambio de ritmo entre efectos. Lo difícil aquí es la teoría musical bien modelada,
la generación con criterio musical (no al azar) y una GUI que enseñe mientras se usa.

**Contexto de diseño clave:** el dueño del proyecto produce reggaetón experimental. Las progresiones pop típicas
(I–V–vi–IV y sus rotaciones) son el punto de partida, no el objetivo: el plugin tiene que llevar con naturalidad a
lo que está más allá — menor natural y frigio (el ♭II), cadencia andaluza, préstamo modal (♭VI, ♭VII, iv en mayor),
acordes sus y add, séptimas y novenas de R&B, mediantes cromáticas, pedales en el bajo, progresiones de 2 acordes que
se sostienen con el ritmo. Y el ritmo importa tanto como la armonía: el dembow (3+3+2, el "boom-ch-boom-chick") y sus
variantes desplazadas, a contratiempo o con huecos deben ser patrones de primera clase, no un extra. Los ejemplos, los
tests de estilo y los presets usan estos casos.

## Plugin MIDI en JUCE
- **Configuración en CMake** (`juce_add_plugin`): `NEEDS_MIDI_INPUT TRUE`, `NEEDS_MIDI_OUTPUT TRUE`,
  `IS_SYNTH TRUE` (para que FL lo cargue como generador en el Channel Rack y reciba las notas del Piano Roll) e
  `IS_MIDI_EFFECT FALSE`. `IS_MIDI_EFFECT TRUE` sirve en AU (Logic) pero en VST3 no hay una categoría equivalente y
  FL lo trata peor; se descarta. Formatos VST3 + Standalone como el resto.
- **Buses de audio:** una salida estéreo que siempre escribe silencio (algunos hosts, FL incluido, esperan al menos una
  salida en un generador) y ninguna entrada. Comprobarlo en la Fase 1 con FL y pluginval.
- **`processBlock`:** recibe el `MidiBuffer` con las notas de entrada y lo reemplaza por las de salida (passthrough,
  acordes, patrones). Mismas reglas del hilo de audio que en los efectos: nada de reservar memoria (los eventos que se
  generan se calculan en estructuras de tamaño fijo reservadas en `prepareToPlay`), nada de locks. Todo exacto a la
  muestra dentro del bloque, con cualquier tamaño de buffer.
- **Tiempo:** posición y tempo del host con el `PlayHead` (BPM, compás, posición en negras, reproduciendo o no, loop),
  igual que Ebb (`plugins/ebb/Source/dsp/HostClock.h` es el modelo; si se reutiliza, moverlo a `libs/dsp` primero).
- **Notas colgadas (lo más importante del plugin):** cada nota que el plugin emite debe tener su note-off pase lo que
  pase: soltar la tecla, cambiar de acorde o de patrón en medio de una nota, parar el transporte, saltar o hacer loop
  en la línea de tiempo, cambiar un parámetro, bypass, `releaseResources`. Se lleva la cuenta de las notas que suenan
  (por canal y número) y se apagan explícitamente; tests para cada caso.
- **Bypass:** el MIDI de entrada pasa tal cual y las notas generadas que suenan reciben su note-off. Latencia 0.
- **Estado:** parámetros con `AudioProcessorValueTreeState` (tonalidad, modo, patrón...); la progresión y la melodía
  (datos, no parámetros) como `ValueTree` en el estado del proyecto. La GUI nunca toca el audio directamente: el audio
  lee una copia publicada de la progresión (buzón lock-free, `TripleBuffer.h` de `libs/dsp`).

## Ruteo en FL Studio
1. Cargar Tidechart en el Channel Rack como cualquier instrumento. Las notas del Piano Roll (o de un teclado MIDI) le
   llegan a él.
2. En Tidechart: botón de engranaje del wrapper (**Plugin options / Settings**) → **MIDI → Output port**: elegir un
   número de puerto (p. ej. 1).
3. En el instrumento que debe sonar (Undertow, FLEX, un sampler): **Settings → MIDI → Input port**: el mismo número.
   Varios instrumentos pueden escuchar el mismo puerto (acordes a un pad y a un pluck a la vez); con puertos distintos
   y varias instancias se separan acordes, bajo y melodía.
4. Alternativa: dentro de **Patcher**, conectar la salida MIDI de Tidechart a la entrada del instrumento.
5. En el Piano Roll o en los patrones va el canal de Tidechart, no el del instrumento.
- Verificar en la Fase 1 que FL recibe notas, velocidades y note-offs por VST3 (los CC por VST3 van como
  `LegacyMIDICCOutEvent`; JUCE lo soporta, pero no se depende de ellos). El Standalone puede mandar su salida a un
  puerto MIDI del sistema.
- La Fase 7 (arrastrar al Piano Roll) es la otra vía: en vez de tocar en vivo, se escribe el MIDI en la canción.

## Librería de teoría musical (`libs/theory`, namespace `deepwater::theory`)
- Nueva librería de la suite, hermana de `libs/dsp`: C++20 estándar, sin JUCE ni GUI, con tests
  (`DeepwaterTheoryTests` en ctest). Headers en `libs/theory/include/deepwater/theory/`.
- Contiene el motor completo: notas y clases de altura (con nombres enarmónicos correctos según la tonalidad: en Fa
  mayor es Si♭, no La♯), intervalos, escalas y modos, construcción de acordes (triadas, séptimas, extensiones, sus,
  add, inversiones), grados y funciones armónicas, conducción de voces, patrones rítmicos, detección de tonalidad y
  generación de melodías y bajos. El plugin solo agrega MIDI, tiempo y GUI encima.
- **Pensada para reutilizarse en un servidor MCP** (para que Claude componga dentro de FL), así que:
  - API de valores puros y deterministas: entra una tonalidad, un acorde o una progresión, sale una lista de notas
    (número MIDI, inicio y duración en negras, velocidad). Nada de estado global ni de tiempo real.
  - Todo lo aleatorio recibe una semilla explícita: misma semilla = mismo resultado (tests reproducibles y "otra
    variación" bajo control).
  - Texto ida y vuelta: leer y escribir símbolos de acordes ("Cm7♭5", "F/A", "Bbadd9") y grados en números romanos
    ("i–♭VI–♭III–♭VII"), con tests de ida y vuelta.
  - Nombres de nota como FL (C5 = MIDI 60, A5 = 440 Hz), configurable.
- Lo que corre en el audio (consultar la progresión publicada, el patrón) va en headers y no reserva memoria; lo pesado
  (generar una melodía, detectar la tonalidad de muchos compases) corre fuera del audio o en pasos acotados.

## Requisitos propios (además de los del raíz)
- Ninguna nota colgada, en ningún caso (ver arriba).
- Un acorde por tecla: la tecla que dispara un acorde no suena además sola, salvo que se pida (modo "con la nota").
- Las progresiones siguen al host: al saltar, hacer loop o cambiar el tempo, el acorde que suena es el de esa posición.
- La detección de tonalidad y la generación nunca bloquean el audio.
- La teoría se explica en la GUI: grado, función (tónica / subdominante / dominante), de qué modo se tomó prestado un
  acorde.

## Hoja de ruta
- [ ] Fase 1 — Base MIDI: `plugins/tidechart/` con su `CMakeLists.txt` (VST3 + Standalone, MIDI de entrada y salida),
      añadido al CMake raíz. Passthrough del MIDI, lectura del tempo y la posición del host mostrados en una GUI
      funcional, sin notas colgadas. Ruteo en FL Studio comprobado. Tests y pluginval. Decidir antes: el nombre y su
      `PLUGIN_CODE`.
- [ ] Fase 2 — `libs/theory`: escalas, modos, construcción de acordes (triadas, séptimas, extensiones, sus, add),
      funciones armónicas e inversiones; nombres de notas y acordes; con tests.
- [ ] Fase 3 — Paleta de acordes por tonalidad y modo; tocar un acorde completo con una sola tecla.
- [ ] Fase 4 — Constructor de progresiones: voicings, inversiones automáticas con buena conducción de voces, patrones
      rítmicos de acordes (incluyendo dembow y variantes), préstamo modal.
- [ ] Fase 5 — Detección de tonalidad a partir del MIDI entrante.
- [ ] Fase 6 — Generador de melodías y líneas de bajo que siguen la progresión, con control de densidad, rango y
      estilo; variaciones a partir de una melodía existente.
- [ ] Fase 7 — Arrastrar y soltar el MIDI generado al Piano Roll de FL Studio (archivo `.mid` temporal escrito con
      `juce::MidiFile` y `DragAndDropContainer::performExternalDragDropOfFiles`).
- [ ] Fase 8 — GUI: teclado con la escala resaltada, círculo de quintas, línea de progresión editable.
- [ ] Fase 9 — Presets por género y estado de ánimo (reggaetón, oscuro/experimental, R&B, trap) con notas que
      expliquen por qué funciona cada progresión (formato de `libs/presets`; la progresión como "estado extra").
- [ ] Fase 10 — Pulido y pluginval.

## Aprendizaje de teoría musical y composición (igual de importante que el código)
Además de los puntos del raíz (los que aplican a un instrumento: qué hace cada control, ejercicio en FL, receta, reto,
vocabulario), el `APRENDIZAJE.md` de esta carpeta se completa al final de cada fase con:
- **Teoría aplicada a lo construido:** por qué un acorde suena triste u oscuro (tercera menor, ♭6, ♭2 del frigio),
  tensión y resolución (el tritono de la dominante, la sensible), préstamo modal, cómo una melodía se apoya en las
  notas del acorde (notas de acorde en los tiempos fuertes, de paso y de adorno entre medio).
- **Ejercicios en FL Studio:** tocar y escuchar la diferencia (misma progresión en eólico y en frigio, en estado
  fundamental y con buena conducción de voces), mirando el Piano Roll.
- **Retos de composición:** escribir una progresión o una melodía a mano en el Piano Roll ANTES de pedírsela al
  plugin, y luego comparar.
- **Análisis de canciones reales** de reggaetón y géneros cercanos (dembow dominicano, R&B, trap latino, perreo
  oscuro): grados, modo, ritmo de acordes y qué las hace funcionar.

## Estado actual
Pendiente: el plugin está en el plan de la suite pero no se empezó. Antes de la Fase 1: confirmar el nombre y el
`PLUGIN_CODE`, y que el plugin anterior en curso esté terminado.
