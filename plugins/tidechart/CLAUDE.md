# CLAUDE.md — Tidechart, herramienta MIDI de acordes y melodías por tonalidad

Plugin 5 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio de Tidechart.

Nombre: **Tidechart** (una carta de mareas: el mapa de por dónde se puede mover la armonía). Target `Tidechart`,
`PLUGIN_CODE Tdc1`; fabricante `Undw`, el de toda la suite; `COMPANY_NAME "Deepwater"`. Confirmado con el usuario al
empezar la Fase 1.

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
  con `deepwater::dsp::HostClock` (`libs/dsp`, venía de Ebb; se movió al empezar la Fase 1).
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

## Comandos
Desde la raíz del repositorio (ver también los comandos generales en el `CLAUDE.md` raíz):
```
cmake --build build --config Release --target Tidechart_VST3       # solo el plugin
build/plugins/tidechart/Release/TidechartTests.exe                 # etapa MIDI sin JUCE
build/plugins/tidechart/TidechartProcessorTests_artefacts/Release/TidechartProcessorTests.exe   # procesador completo
```
- VST3: `build/plugins/tidechart/Tidechart_artefacts/Release/VST3/Tidechart.vst3`.
- Standalone: `build/plugins/tidechart/Tidechart_artefacts/Release/Standalone/Tidechart.exe` (sin host: reloj libre a
  120 BPM; su salida MIDI se elige en Options).

## Estructura
```
plugins/tidechart/
  CMakeLists.txt             # plugin (IS_SYNTH, MIDI de entrada y salida) y tests; enlaza deepwater::dsp
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado, processBlock (MidiBuffer ↔ MidiStage), PlayHead, publicación a la GUI
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    TransportSnapshot.h      # lo que el audio publica para la GUI en cada bloque (TripleBuffer): reloj y notas que suenan
    midi/NoteTracker.h       # mensajes cortos y la cuenta de lo que suena por canal y nota (releaseAll = los note-off que faltan)
    midi/MidiStage.h         # la etapa MIDI sin JUCE: todo lo que sale pasa por emit (contado); apagar y pánico pedidos
                             # desde cualquier hilo y atendidos en la muestra 0 del bloque siguiente
    midi/EventLog.h          # cola lock-free audio → GUI de los eventos que entran y salen (el monitor)
    midi/MidiText.h          # nombres de nota como FL (C5 = 60) y descripción de mensajes para el monitor
    gui/                     # tema, reloj del host (TransportView), teclado de lo que suena, monitor MIDI
  tests/MidiTests.cpp        # etapa MIDI sin JUCE (contra un receptor modelo independiente)
  tests/ProcessorTests.cpp   # procesador completo con JUCE (passthrough exacto, notas colgadas, bypass, transporte, reservas)
```

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
- [x] Fase 1 — Base MIDI: `plugins/tidechart/` con su `CMakeLists.txt` (VST3 + Standalone, MIDI de entrada y salida),
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
Fase 1 COMPLETADA (2026-10-28). Probada por el usuario en FL Studio (ruteo por puertos MIDI): funciona.
- Antes de empezar: `HostClock` de Ebb pasó a `libs/dsp` (`deepwater/dsp/HostClock.h`, sus 6 tests a
  `DeepwaterDspTests`; Ebb lo usa con alias). Ebb idéntico: `EbbTests --fingerprint`, 336 hashes iguales.
- Plugin: `IS_SYNTH TRUE`, MIDI de entrada y salida, VST3 `Instrument Tools`; sin entradas de audio, salida mono o
  estéreo en silencio. Parámetro `bypass` (versionHint 1) = el bypass del host; `processBlockBypassed` hace lo mismo.
- `processBlock`: lee el PlayHead (`HostClock`), arma la salida en `outputMidi` (16 KB reservados) pasando cada evento
  por `MidiStage` y la copia al buffer del host (`clear` conserva la capacidad; se descartó `swapWith`: el buffer
  reservado pasaría al host y el nuestro sería el suyo, más chico). Eventos fuera del bloque se ajustan a él.
- Notas colgadas: `NoteTracker` cuenta por canal y nota (dos note-on de la misma tecla = dos note-off; satura en
  255; CC 120/123 vacían el canal). `MidiStage::requestRelease` en `prepareToPlay`, `releaseResources`, `reset` y
  `setStateInformation`; `requestPanic` (botón) = note-off de lo que suena + CC 123 en los 16 canales. Se atienden en
  la muestra 0 del bloque siguiente; solo cuentan (`releaseCount`) si se apagó algo o fue pánico. Decisión: las notas
  que solo PASAN no se cortan al parar, saltar o hacer loop (su note-off lo manda el host, y cortar al parar
  silenciaría una nota tocada en vivo); las que Tidechart genere desde la Fase 3 sí.
- GUI 900 × 600 (funcional; la definitiva es la Fase 8): cabecera con pánico y bypass, reloj del host (como Ebb),
  SUENA AHORA (teclado de 128 notas con lo que suena en el receptor), monitor ENTRADA | SALIDA (posición, canal,
  mensaje; cola `EventLog<1024>`, si se llena se descarta) y el ruteo de FL en el pie. Revisada con captura del
  Standalone.
- Tests: `TidechartTests` (8: mensajes, tracker, passthrough + apagar contra un receptor modelo con 10 semillas,
  tecla repetida, pánico, peticiones desde otro hilo, EventLog en hilos, nombres) y `TidechartProcessorTests` (5:
  passthrough idéntico a 4 sample rates × bloques 1/7/64/256/512/1024/4096/variable × mono/estéreo con audio en
  silencio y sin reservas; note-off en la muestra 0 tras reinicio, cambio de sample rate, reset, cargar estado y
  pánico; bypass y processBlockBypassed; transporte con un PlayHead de prueba; buses, estado y editor).
- Release con 0 warnings; ctest 13/13; pluginval strictness 10 ampliado: SUCCESS.

Siguiente: Fase 2 (`libs/theory`).
