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
   Para separar las capas (Fase 6): clonar el canal de Tidechart en el Channel Rack (clic derecho → Clone: copia el
   estado, con la progresión y las líneas), darle otro Output port y dejar encendida solo su capa (Acordes / Bajo /
   Melodía). Cada clon guarda su copia: lo que se edite después en uno no pasa al otro.
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
build/libs/theory/Release/DeepwaterTheoryTests.exe                 # libs/theory
```
- VST3: `build/plugins/tidechart/Tidechart_artefacts/Release/VST3/Tidechart.vst3`.
- Standalone: `build/plugins/tidechart/Tidechart_artefacts/Release/Standalone/Tidechart.exe` (sin host: reloj libre a
  120 BPM; su salida MIDI se elige en Options).

## Estructura
```
plugins/tidechart/
  CMakeLists.txt             # plugin (IS_SYNTH, MIDI de entrada y salida) y tests; enlaza deepwater::dsp y ::theory
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado, processBlock (MidiBuffer ↔ MidiStage), PlayHead, publicación a la GUI
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    TransportSnapshot.h      # lo que el audio publica para la GUI en cada bloque (TripleBuffer): reloj y notas que suenan
    Lines.h                  # Fase 6: las líneas como datos del proyecto (ajustes + notas), computeBass/computeMelody y
                             # MelodyCapture (cuantizar y doblar sobre la vuelta lo que se tocó); sin JUCE
    MidiExport.h             # Fase 7: el clip de cada capa (acordes, bajo, melodía) = lo que suena en vivo; sin JUCE
    MidiFileExport.h         # Fase 7: el clip como .mid (juce::MidiFile, 960 ticks por negra) y la carpeta temporal
    midi/NoteTracker.h       # mensajes cortos y la cuenta de lo que suena por canal y nota (releaseAll = los note-off que faltan)
    midi/MidiStage.h         # la etapa MIDI sin JUCE: todo lo que sale pasa por emit (contado); apagar y pánico pedidos
                             # desde cualquier hilo y atendidos en la muestra 0 del bloque siguiente; bypass ↔ acordes;
                             # Fase 6: las líneas (bajo y melodía) en el modo progresión, una voz por nota
    midi/ChordTrigger.h      # Fase 3: qué acorde dispara cada tecla (teclas blancas = grados / notas de la escala) y los
                             # acordes que suenan, cada uno con SUS notas, con cuenta de notas compartidas. Fase 4:
                             # cualquier grado escrito (chordForNumeral), voicing (withVoicing), códigos de la paleta
    midi/KeyListener.h       # Fase 5: escucha lo que entra y suma la evidencia de la tonalidad (duraciones, bajo, votos
                             # de "casa" al empezar cada frase), exacta a la muestra y sin reservar memoria
    midi/EventLog.h          # cola lock-free audio → GUI de los eventos que entran y salen (el monitor)
    midi/MidiText.h          # nombres de nota como FL (C5 = 60) y descripción de mensajes para el monitor
    gui/                     # tema, reloj del host (TransportView), paleta + prestados (ChordPaletteView), progresión
                             # (ProgressionView, ProgressionTemplates), teclado de lo que suena, acorde que suena
                             # (ChordView, con libs/theory), teoría en español (TheoryNames), monitor MIDI, detección
                             # de tonalidad (KeyDetectView: escuchar, resultado con su porqué, alternativas, histograma),
                             # melodía y bajo (LinesView: capas, estilo, densidad, rango, capturar, variar, piano roll;
                             # Fase 7: la fila "Al Piano Roll" con un MidiDragButton por capa)
  tests/MidiTests.cpp        # etapa MIDI sin JUCE (contra un receptor modelo independiente)
  tests/ProcessorTests.cpp   # procesador completo con JUCE (passthrough exacto, notas colgadas, bypass, transporte,
                             # progresión, reservas; Fase 7: lo exportado = lo que suena y el .mid leído de vuelta);
                             # --snapshot <png> [full] pinta la GUI (vista simple o completa)
                             # fuera de pantalla para revisarla
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
- Decisiones de la Fase 2 (no cambiarlas sin pensar en lo que depende de ellas):
  - Notas ESCRITAS (letra + alteración), no solo teclas: los intervalos llevan número y semitonos, y así C + 3.ª menor
    = E♭ y F♯7 = F♯ A♯ C♯ E sin tablas de enarmónicos. Lo que llega por MIDI (solo teclas) se escribe con
    `spellInKey` (en la escala, como en ella; fuera, ♭2 ♭3 ♯4 ♭6 ♭7 respecto de la mayor de la tónica) o, sin
    tonalidad, con la escritura de menos alteraciones (empate → sostenidos, como el Piano Roll de FL).
  - Romanos con la alteración medida contra la escala MAYOR de la tónica (convención pop/jazz: "i–♭VI–♭III–♭VII"); la
    mayúscula dice la tercera y el resto del tipo va en un sufijo propio de cada tipo del catálogo (`romanSuffix`).
    Sin inversiones ni dominantes secundarias en el texto romano todavía (la inversión es cosa del voicing, Fase 4).
  - Funciones T/S/D simplificadas por grado: I iii vi ♭III → T; ii IV ♭II ♭VI → S; V vii ♭VII → D.
  - `Chord::midiNotes(rootNote)`: la fundamental en la tecla pulsada, posición cerrada; en una inversión los tonos por
    debajo del bajo suben una octava; un bajo ajeno va debajo de todo. Es el voicing base de la Fase 3.
  - Reconocer acordes: coincidencia exacta de clases de altura o, con 3+ notas, sin la 5.ª justa; se ordena exacta >
    fundamental en el bajo > tipo más común (orden del catálogo). Por eso el orden del catálogo no se toca.

- Decisiones de la Fase 3:
  - Parámetros nuevos (versionHint 2, el orden de sus listas es parte del formato): `keyTonic` (0..11, por defecto 9 =
    La), `keyMode` (orden de `Mode`, por defecto eólico), `chordSize` (tríadas / séptimas), `keyMapping` (teclas blancas =
    grados / notas de la escala), `chordBass`. La tónica es una tecla: la escritura la decide `standardKey` con el modo.
  - Activo = acordes; bypass = passthrough. La tecla que dispara no suena sola (el modo "con la nota" queda para más
    adelante si hace falta); lo que no es nota pasa igual. Una tecla que no dispara nada (negra, fuera de la escala) no
    sale. Cambiar entre bypass y acordes apaga en la muestra 0 lo del modo que se deja (al salir del bypass, las notas que
    pasaron: su note-off llegaría con los acordes activos y ya no saldría).
  - Teclas blancas: la tónica cae entre F♯ de la octava de abajo y F de la octava de la tecla (en La menor, C5 = A4 C5 E5).
    Notas de la escala: la fundamental en la tecla pulsada. Posición cerrada (`Chord::midiNotes`); "+ Bajo" = la
    fundamental una octava abajo. Velocidad y canal, los de la tecla.
  - Cada acorde guarda sus notas (slot = canal × tecla, más uno para la paleta): soltar no recalcula nada, así que
    cambiar parámetros con teclas abajo no cuelga notas. Notas compartidas entre acordes: cuenta de referencias por
    canal y nota; el note-off sale con el último, y un acorde nuevo re-ataca las que ya sonaban (off + on en la misma
    muestra). El instrumento nunca recibe dos note-on seguidos de la misma nota.
  - Los acordes siguen la regla de las notas que pasan: suenan mientras la tecla está abajo y los apaga su note-off (el
    host lo manda al parar, saltar y hacer loop). Pánico, reinicio del audio y cargar el estado apagan todo y olvidan los
    acordes; el note-off tardío de su tecla no manda nada.
  - Paleta: la GUI pide un grado (atómico, −1 = soltar) y el audio lo toca en la muestra 0 del bloque siguiente, canal 1,
    velocidad 100, en la octava de C5. El audio publica los grados que suenan (`heldDegrees`) para encender los pads.
  - El MIDI de salida puede ser más grande que el de entrada (3–5 notas por tecla). Se arma en un buffer propio de
    16 KB; si el del host (2048 bytes en el wrapper VST3 de JUCE) se queda chico crece una vez y conserva su tamaño.

- Decisiones de la Fase 4:
  - `libs/theory`: `Voicing.h` (`voiceChord`: todas las inversiones en 5 octavas; gana la que menos mueve las voces —
    emparejadas de grave a agudo— ×4 más la distancia al centro; el centro = la tríada de tónica y no se aleja más de
    ±7 semitonos; empate → estado fundamental), `Rhythm.h` (9 patrones en semicorcheas; el orden es formato),
    `Progression.h` (pasos = `RomanNumeral` + tiempos 1..16, máximo 8 pasos; `stepAt` desde el principio de la
    canción; `voiceProgression` en dos pasadas para que la vuelta del loop también se mueva poco; `renderProgression`,
    las notas en el tiempo, es el oráculo de los tests y será lo que se arrastre al Piano Roll en la Fase 7) y
    `borrowedChords` en `Harmony.h` (por orden de uso: de una mayor, el eólico primero; de una menor, dórico, frigio,
    armónica; disminuidos y aumentados al final).
  - Parámetros nuevos (versionHint 3, el orden de sus listas es formato): `playMode` (teclas / progresión), `voicing`
    (estado fundamental / conducción de voces / abierto drop 2), `rhythm` (índice de `rhythmPatterns`). Por defecto lo
    de la Fase 3: teclas, estado fundamental, sostenido. Con esos valores la salida es idéntica a la de la Fase 3.
  - Progresión = datos del proyecto, no parámetros: en el procesador (con un lock solo entre la GUI y
    setStateInformation; el audio nunca lo toma) y publicada al audio por `TripleBuffer<Progression>`. En el estado,
    elemento `<Progression><Step chord="bVI" beats="4"/>` (romano ASCII: legible y escribible por presets y el MCP),
    fuera del árbol del APVTS. Proyecto sin progresión (Fase 3) → la de fábrica, i–♭VI–♭III–♭VII de 4 tiempos.
  - Lo que suena es una función de la posición: en cada semicorchea (detectada por muestra con `HostClock::ppqAt`, con
    un margen de 1e-6 para el redondeo del host) se compara lo que debería sonar con lo que suena: se ataca si no suena,
    si cambió el paso, las notas o el canal, o si empieza un golpe; se suelta en un silencio del patrón. Saltos, loops,
    tempo y editar la progresión mientras suena salen solos. La progresión y el patrón se cuentan desde el compás 1.
    Host parado → sin ritmo (sostenido, para escuchar esa posición). Solo se recorren las muestras si algo depende del
    tiempo (si no, una cuenta por evento).
  - Modo progresión: cualquier tecla abajo la hace sonar; canal, octava (C5 = el registro de la paleta) y velocidad de
    la última; suena mientras quede una abajo. Modo teclas: las teclas abajo (hasta 64) se recuerdan para volver a
    atacar su acorde en cada golpe (recalculado con los ajustes de ese momento: el slot guarda sus notas, así que nada
    se cuelga). Cambiar entre teclas y progresión apaga todo y olvida las teclas (su note-off tardío no hace nada).
  - Voicing en el modo teclas y la paleta: desde las voces del último acorde de una tecla o de la paleta. El bajo
    ("+ Bajo") sigue siendo la fundamental una octava abajo; ninguna voz baja hasta él. La progresión se voicea entera
    con su tónica (cache que se recalcula si cambian la progresión, los ajustes o la octava).
  - Paleta: un solo atómico con un código (0 = nada, 1..7 = grado con el tamaño de los ajustes, ≥1000 = cualquier
    `RomanNumeral` empaquetado). Prestados: los 8 primeros de `borrowedChords`; "Agregar con clic" los suma a la
    progresión. Plantillas: progresión + modo (la tónica no cambia).

- Decisiones de la Fase 5:
  - `libs/theory`: `KeyDetection.h` (+ `src/KeyDetection.cpp`: `keyEvidence` de una lista de `NoteEvent`, para el MCP y
    como oráculo de los tests). Krumhansl-Schmuckler: correlación de Pearson entre lo escuchado (12 clases de altura,
    pesadas por duración; la nota más grave de cada tramo suma otra vez) y el perfil de cada una de las 120
    tonalidades. Perfil genérico por papel, medias de Krumhansl-Kessler mayor y menor: tónica 6.34, 5.ª justa 4.97,
    3.ª 4.88, resto de la escala 3.56, fuera 2.55 (sirve para los 10 modos).
  - Relativas (Am F C G hace sonar más el Do que el La): votos de "casa" = la nota más grave de las que empiezan cerca
    del inicio de cada frase de 8 compases (de −1/8 a +1/4 de negra; desde el compás 1, y cada vuelta del loop o cada
    play cuenta como otra frase); sin host reproduciendo, un voto para la más grave de las primeras notas (50 ms). La
    parte de los votos que cae en la tónica suma hasta 0.25. Empates → modo más común (eólico, jónico, dórico, frigio,
    mixolidio, armónica, lidio, frigio dominante, melódica, locrio).
  - Confianza por el margen con la segunda: ≥ 0.035 clara, ≥ 0.015 probable, menos dudosa; menos de 3 clases de altura
    (o menos de 1 s escuchado, en la GUI) = nada. Lo que queda segundo suele ser el modo vecino de la misma tónica (una
    nota distinta: ~0.04 de margen cuando esa nota suena).
  - "Escuchar" no es un parámetro ni se guarda: un atómico del procesador. Mientras escucha el MIDI pasa tal cual (los
    ajustes de la etapa van como en bypass, con las mismas transiciones que apagan lo del otro modo); cada vez que se
    enciende empieza de cero; al apagarlo lo escuchado queda publicado. El audio solo suma (`KeyListener`) y publica la
    evidencia en el snapshot; la GUI ordena las tonalidades (`rankKeys`, 120 × 12, solo si cambió). "Usar" pone
    `keyTonic` y `keyMode` con su gesto.

- Decisiones de la Fase 6:
  - `libs/theory`: `Melody.h` + `src/Melody.cpp`. Una línea = notas en semicorcheas desde el principio de su vuelta
    (`LineNote`: inicio, duración, nota, velocidad; hasta 512, tamaño fijo) que se repite desde el compás 1. La vuelta =
    la progresión repetida hasta tener al menos 4 compases (`lineLength`): con i–♭II de un compás cada uno, 2 vueltas.
  - Escala de cada acorde (`chordScaleMask`): la de la tonalidad; sobre un prestado, la del primer modo paralelo que lo
    contiene en el orden de `parallelModeOrder` (el mismo de `borrowedChords`: ♭VI en Do mayor canta en Do menor); si
    ninguno, cada tono del acorde reemplaza la nota de su letra. `noteRole`: nota del acorde / de la escala / cromática.
  - Melodía (`generateMelody`), 4 estilos (gancho, cantada, arpegio, R&B; el orden es formato). Ritmo: un motivo de un
    compás sorteado con pesos por semicorchea y estilo (cuántas notas, por la densidad), en frases de 4 compases A A' A
    cierre (A' = una nota menos o una más; el cierre = la primera mitad del motivo y una nota larga en el tiempo 3; el
    último compás de la línea siempre cierra). Duración hasta la nota siguiente con un tope por estilo. Alturas: de todas
    las notas del rango que están en la escala del acorde, la de menor costo: apoyo (tiempo 1 o 3, cambio de acorde,
    nota larga) fuera del acorde = tensión o "nota a evitar" (un semitono sobre una nota del acorde), salto (tabla por
    semitonos; más de una octava, prohibido), seguir subiendo tras un salto grande, gravedad al centro del rango, imitar
    el intervalo (en grados) del motivo en la misma semicorchea del compás 1 (secuencia; con el mismo acorde, la misma
    nota), el final de la frase en la tónica o la fundamental, y un ruido con semilla (la variedad). El arpegio recorre
    las notas del acorde en una ventana de ~una octava con una figura sorteada (arriba, abajo, arriba-abajo, 1-5-3-5).
  - Bajo (`generateBass`), 5 estilos (fundamental, dembow, octavas, caminante, pedal). Golpes por compás según estilo y
    densidad (3 niveles); cada cambio de acorde empieza un golpe (salvo el pedal). La fundamental en la octava más
    cercana a la anterior dentro del rango (sin anterior, la más grave). Aproximaciones: un semitono o un grado de la
    escala del acorde que suena hacia la fundamental siguiente (la última de la vuelta, hacia la primera).
  - Variación (`varyMelody`): por nota, con probabilidad cantidad × factor del compás en la frase (0.3, 0.8, 0.6, 1.0),
    una de: vecina, adelantar una semicorchea, partir, unir con la siguiente, invertir el intervalo, nota de paso entre
    una 3.ª. Solo lo que cambió vuelve al rango y, si cae en un apoyo, a una nota del acorde (una apoyatura capturada
    a propósito no se "corrige"). Cantidad 0 = la misma línea.
  - Aleatorio con un generador propio (splitmix64, sin `std::*_distribution` ni funciones trascendentes): misma semilla =
    mismas notas en cualquier compilador (para el MCP).
  - Plugin: `Lines.h` (ajustes + notas). Las NOTAS mandan: el proyecto guarda los ajustes y las notas, y al cargar suenan
    las guardadas (un generador futuro no cambia un proyecto viejo). Se regeneran solo cuando el usuario cambia un ajuste
    o cuando cambian la progresión o la tonalidad (`refreshLines`, temporizador del procesador a 10 Hz en el hilo de
    mensajes; la tonalidad cargada y la progresión se marcan como vistas al cargar). Una melodía fija (capturada o
    fijada) sin variación no se regenera nunca; con variación, la variación se rehace sobre la progresión nueva.
  - En el estado: `<Lines><Bass style="dembow" density low high seed length><Note at length note="A2" velocity/>...`
    y `<Melody style ... variation variationSeed length>` con `<Source>` si es fija. Estilos por nombre (`hook`,
    `walking`...), notas con el nombre de FL: legible y escribible por presets y el MCP. Proyecto sin `<Lines>` → las de
    los ajustes por defecto con su progresión y tonalidad (las capas vienen apagadas: no cambia lo que suena).
  - Parámetros nuevos (versionHint 4, Off / On como `chordBass`): `playChords` (por defecto On), `playBass`,
    `playMelody` (Off). Con los valores por defecto la salida es la de la Fase 5.
  - En vivo (`MidiStage::evaluateLines`): en el modo progresión, con una tecla abajo y el host reproduciendo. En cada
    semicorchea se sueltan las notas que ya no tocan (terminó, otra nota en ese lugar porque la línea cambió, otro canal,
    o la misma nota que empieza otra vez en la vuelta siguiente: `tickSerial`, porque un loop vuelve a la misma
    semicorchea absoluta) y se atacan las que tocan y no suenan (también a mitad de nota tras un salto). Una voz por nota:
    16 slots más en el `ChordTrigger` (2 líneas × 8), así comparten la cuenta de notas con los acordes. Canal = el de la
    tecla; velocidad = la de la nota × la de la tecla / 100. Host parado = las líneas callan (los acordes siguen
    sostenidos). Modo teclas = solo acordes. Las líneas llegan al audio por dos `TripleBuffer<Line>` y un contador: el
    audio solo las copia (8 KB cada una) cuando cambian.
  - Capturar: como Escuchar (atómico, no se guarda; el MIDI pasa tal cual). La GUI recoge del monitor (`LoggedEvent`
    ahora trae `playing`) los note-on/off con su posición; al terminar, `MelodyCapture` cuantiza a semicorcheas, dobla
    sobre la vuelta (la última pasada gana) y descarta lo tocado con FL parado; `captureMelody` la deja como base.

- Decisiones de la Fase 7:
  - Un `.mid` por capa (no uno con todo): en FL cada capa va al Piano Roll de SU instrumento (pad, 808, lead). Formato 1,
    una pista, canal 1, 960 ticks por negra (una semicorchea = 240 exactos), nombre de pista, tempo de FL y 4/4 al
    principio, note-off de verdad (el note-off antes que el note-on si coinciden en un tick) y el fin de pista al final de
    la vuelta (el clip dura sus compases aunque termine en silencio).
  - Qué notas (`exportClip`, sin JUCE): las de `renderProgression` y `renderLine`, que son el oráculo de lo que suena en
    vivo. Acordes con el voicing, ritmo y bajo de los parámetros, la tónica cerca de C5 (como la paleta: en vivo la
    octava la elige la tecla) y velocidad 100 (en vivo, la de la tecla); las líneas con sus notas y velocidades. Las tres
    capas duran la vuelta de las líneas (`lineLength`): con i–♭II de un compás cada uno, los acordes van 2 veces y en la
    Playlist los tres clips se repiten juntos. Se exporta aunque la capa esté apagada. `exportClip` del procesador llama
    antes a `refreshLines` (el temporizador puede ir 100 ms atrás).
  - Arrastre: `MidiDragButton` (se arrastra, no se pulsa; un clic explica qué hacer). Al pasar 5 px de arrastre escribe el
    archivo con lo de ese momento y llama a `DragAndDropContainer::performExternalDragDropOfFiles` (copia, no mover):
    para FL es lo mismo que arrastrar un `.mid` desde el Explorador. Archivos en `%TEMP%/Tidechart/`, uno por capa con
    nombre fijo ("Tidechart Acordes (A minor).mid"), reemplazado entero en cada arrastre: no se acumulan.
  - Descartado: arrastrar la capa al Piano Roll del propio Tidechart (en el modo progresión cada nota volvería a disparar
    la progresión); el tooltip dice que va en el del instrumento.

## GUI: vista simple primero (regla para todas las fases que faltan)
Pedido del usuario (2026-10-28): la GUI debe ser minimalista, con lo necesario para que alguien que empieza pueda usarla
y encontrar acordes, progresiones y melodías nuevas. Hay dos vistas de los MISMOS paneles (`setSimple` en cada vista):
- **Simple (por defecto):** solo lo que hace falta para crear, en el orden en que se usa (tonalidad → acordes →
  progresión → melodía y bajo), botones de descubrir ("Otra idea", "Otra"), y una guía de qué hacer ahora. Sin datos de
  depuración (reloj, monitor MIDI, teclado de lo que suena). Los textos dicen qué hacer, no cómo funciona por dentro;
  el porqué va en tooltips, en la línea de información y en el piano roll.
- **Completa:** todo lo construido (lo que se probó en FL en cada fase), detrás de "Vista completa". Nada se pierde.
- La vista se guarda en el proyecto: propiedad `view` del árbol del APVTS ("simple" o nada / "full"), no es parámetro.
- Cada fase nueva decide qué va en la simple con esta pregunta: ¿lo necesita alguien que empieza para crear algo? Si
  sí, entra en la simple (como mucho un control por idea, con un buen valor por defecto); si es ajuste fino, en la
  completa. Fase 7: arrastrar al Piano Roll es esencial → en la simple, un botón visible por capa. Fase 8: lo que
  reemplace a un control de la simple lo reemplaza ahí (no se suma). Fase 9: presets = la puerta de entrada de la
  simple (barra de presets arriba). Fase 10: revisar las dos vistas con `--snapshot` y `--snapshot <png> full`.

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
- [x] Fase 2 — `libs/theory`: escalas, modos, construcción de acordes (triadas, séptimas, extensiones, sus, add),
      funciones armónicas e inversiones; nombres de notas y acordes; con tests.
- [x] Fase 3 — Paleta de acordes por tonalidad y modo; tocar un acorde completo con una sola tecla.
- [x] Fase 4 — Constructor de progresiones: voicings, inversiones automáticas con buena conducción de voces, patrones
      rítmicos de acordes (incluyendo dembow y variantes), préstamo modal.
- [x] Fase 5 — Detección de tonalidad a partir del MIDI entrante.
- [x] Fase 6 — Generador de melodías y líneas de bajo que siguen la progresión, con control de densidad, rango y
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
Fase 7 HECHA (2026-10-28); falta que el usuario la pruebe en FL Studio (arrastrar cada capa al Piano Roll de un
instrumento y a la Playlist; que lo escrito suene igual que en vivo).
- Plugin: `MidiExport.h` (`exportClip`), `MidiFileExport.h` (`toMidiFile`, `writeMidiFile`, `exportFolder`),
  `gui/MidiDragButton.h`; `exportClip` en el procesador; en `LinesView`, en las dos vistas, la fila "Al Piano Roll:"
  con Acordes / Bajo / Melodía alineada con la de "Suenan" (la línea de información dice cuántas notas y compases se
  arrastran, o que se arrastra y no se pulsa). Revisadas las dos vistas con `--snapshot`.
- Tests: `TidechartProcessorTests` +1: cada capa sola, con un host a 120 BPM y una tecla en C5 durante una vuelta, suena
  exactamente como su clip (inicio, duración, nota y velocidad) en 4 ajustes (por defecto; conducción + dembow + bajo;
  Mi frigio drop 2 + dembow con huecos; Fa♯ dórico en corcheas + bajo) × 2 progresiones (i–♭VI–♭III–♭VII e i–♭II, que
  se repite 2 veces); el `.mid` leído de vuelta: formato 1, una pista, 960 ticks, nombre, tempo, 4/4, cada note-on con
  su note-off en el canal 1, las mismas notas y el fin de pista al final de la vuelta. Nota repetida pegada (note-off
  antes del note-on), progresión vacía (nada que arrastrar) y un archivo más corto que reemplaza a uno largo sin restos.
- Release con 0 warnings; ctest 14/14; pluginval strictness 10 ampliado con `--randomise`: SUCCESS.
- Pendiente para más adelante: arrastrar las tres capas juntas (un `.mid` de 3 pistas para la Playlist), exportar
  varias vueltas o en la octava de la tecla, guardar el `.mid` con un diálogo (si un host no acepta arrastres).

GUI minimalista (entre las Fases 6 y 7, 2026-10-28): vista simple por defecto y vista completa. Probada por el usuario
en FL Studio: funciona.
- Vista simple 1180 × 560: TONALIDAD (tónica, modo y, en la misma fila, Escuchar → resultado → Usar) con los 7 acordes y
  los prestados; PROGRESIÓN (qué tocan las teclas, ritmo, plantillas, "Otra idea", agregar con clic, vaciar, pasos y
  patrón); la guía (qué hacer ahora según bypass, escuchar, modo, progresión vacía, FL parado, sin nota abajo, o qué
  suena y qué probar) y el ruteo; a la derecha MELODÍA Y BAJO (capas, estilo y "Otra" por línea, piano roll con
  leyenda corta y la explicación de cada nota al pasar el ratón).
- Fuera de la simple (en la completa, siguen sonando como estén): tríadas/séptimas, teclas blancas/notas de la escala,
  + Bajo, voicing, texto romano, densidad, rango, capturar, fijar, variación; reloj, SUENA AHORA, ACORDE, monitor MIDI,
  histograma y alternativas de la detección.
- Nuevo en las dos vistas: "Otra idea" (la plantilla siguiente, con su modo) y el nombre de la idea en la línea de
  información de la progresión (ahora de dos líneas). Vista completa 1360 × 960 igual que antes (+ el botón).
- Cada vista: `setSimple` en `ChordPaletteView` (+ `freeTopArea` para la detección), `ProgressionView`,
  `KeyDetectView` (una fila; el porqué en el tooltip) y `LinesView` (si capturaba, la captura termina).
- Release con 0 warnings; `TidechartTests` y `TidechartProcessorTests` pasan; pluginval strictness 10 ampliado con
  `--randomise`: SUCCESS. Revisadas las dos vistas con `--snapshot`.

Fase 6 COMPLETADA (2026-10-28). Probada por el usuario en FL Studio (capas, estilos, densidad, rango, Otra, capturar,
variar y fijar): funciona.
- `libs/theory`: `Melody.h` (`LineNote`, `Line`, `lineLength`, `linePosition`, `chordAt`, `chordChangesAt`, `scaleMask`,
  `chordScaleSource`, `chordScaleMask`, `noteRole`, opciones y estilos) y `src/Melody.cpp` (`generateMelody`,
  `generateBass`, `varyMelody`, `renderLine`); `parallelModeOrder` en `Harmony.h` (lo usa `borrowedChords`, sin cambiar
  su orden). `DeepwaterTheoryTests` +4 grupos: escalas de acorde y papeles (♭VI en Do mayor, ♭II frigio, un aumentado
  cromático), largo de la vuelta; melodías en 7 progresiones × 12 tónicas × 4 modos × 4 estilos × 3 densidades (rango,
  escala del acorde, saltos, apoyos en notas del acorde: 100 % gancho, cantada y arpegio, 84 % R&B; cierre en el acorde,
  deterministas, otra semilla = otra melodía, más densidad = más notas; motivo repetido y compás de cierre); bajos en 5
  progresiones × 12 tónicas × 5 estilos × 3 densidades (fundamental en cada cambio, pedal en la tónica, cromáticas solo
  como aproximación, dembow 0 3 6 8 11 14, A2 → F2 baja una 3.ª); variaciones (cantidad 0 = igual, deterministas, apoyos
  cambiados en notas del acorde, el primer compás conserva el 69 % con cantidad 1, una apoyatura no tocada se queda).
- Plugin: `Lines.h`; `MidiStage` con `setLine`, `evaluateLines`, `linesSounding`; 3 parámetros (versionHint 4); el
  procesador con las líneas en el estado y en dos `TripleBuffer`, `refreshLines` (temporizador), `setCapturing`; GUI
  1360 × 960 con la columna MELODÍA Y BAJO (`gui/LinesView.h`): capas, estilo/densidad/rango/Otra por línea, Capturar,
  Fijar, Variación y Otra variación, piano roll de una vuelta con los acordes y cada nota coloreada por su papel (pasar
  el ratón la explica: "Melodía · E6 · 5 de Am · nota del acorde en un apoyo (compás 2, tiempo 1)"). Revisada con
  `--snapshot` (FL en el compás 2 con dembow, bajo y gancho).
- Tests: `TidechartTests` +3 (líneas en vivo = `renderLine` muestra a muestra con bloques al azar en 2 progresiones × 4
  estilos × con y sin bajo; salto a mitad de nota, loop que re-ataca el pedal, capas, línea que cambia sonando, nota
  compartida con el acorde, parado, soltar, modo teclas, bypass, apagar; captura) y el aleatorio de la Fase 4 ahora con
  capas y líneas cambiando. `TidechartProcessorTests` +2 (líneas con un host = `renderLine` a 4 sample rates × bloques
  1/7/64/512/4096/variable sin reservas; cambiarlas sonando; regenerar al cambiar la progresión y no tocar la capturada;
  capturar deja pasar el MIDI; estado: ajustes y notas viajan legibles, las notas guardadas mandan, proyecto sin líneas).
- Release con 0 warnings; pluginval strictness 10 ampliado con `--randomise`: SUCCESS. ctest: todo pasa salvo `UndertowTests` por su test de CPU del resonador (20,0 % contra un
  límite de < 20 %, depende de la carga de la máquina; Undertow no usa nada de lo que cambió).
- Pendiente para más adelante: canales MIDI por capa (para separar en Patcher sin clonar), editar notas a mano en el
  piano roll (Fase 8), melodías que no sean de 4/4, variar también el bajo.

Fase 5 COMPLETADA (2026-10-28). Probada por el usuario en FL Studio (escuchar acordes y melodías, Usar): funciona.
- `libs/theory`: `KeyDetection.h` (perfiles, `profileCorrelation`, `rankKeys`, `keyConfidence`, `sameNotes`,
  `phraseCueIndex`) y `keyEvidence` en `src/KeyDetection.cpp`. `DeepwaterTheoryTests` +1 grupo: perfiles de los 10
  modos, evidencia de notas (duración, bajo, votos), ventanas de frase, relativas sin votos (empate → La menor, luego
  Do mayor; el reggaetón sin votos sale Do mayor), 8 progresiones del contexto (reggaetón, pop, i–♭II frigio, dórico,
  armónica, mixolidio, frigio dominante, lidio) en las 12 tónicas con al menos confianza "probable", la andaluza y una
  melodía sin votos.
- Plugin: `midi/KeyListener.h`; el procesador con `setListening` (atómico), la evidencia en el snapshot; GUI 900 × 960
  con la fila TONALIDAD (`gui/KeyDetectView.h`) y "ESCUCHANDO" en SUENA AHORA. Revisada con `--snapshot` (después de
  escuchar i–♭II en Mi frigio: E frigio, clara; "lo que las separa es B contra B♭"; alternativas E locrio, E frigio
  dominante, E menor).
- Tests: `TidechartTests` +1 (en vivo = `keyEvidence` con bloques al azar, un voto por vuelta del loop, la primera nota
  con FL parado, el bajo doble, note-off sueltos, CC 123, reset). `TidechartProcessorTests` +1 (escuchando pasa el MIDI
  tal cual y detecta Mi frigio a 4 sample rates × bloques 1/64/512/4096/variable, sin reservas; al dejar de escuchar
  apaga lo que pasó y vuelven los acordes; lo escuchado se conserva; volver a escuchar empieza de cero).
- `chordBass` pasó de `AudioParameterBool` a `AudioParameterChoice` (Off / On): el Bool guarda el valor crudo del host
  (0.6) y el estado lo redondea (1), y pluginval con `--randomise` fallaba en "Bass not restored" (ya pasaba en la
  Fase 4, con otras semillas). En el proyecto se guarda igual (0 / 1). Ebb tiene un Bool igual (`velocity`).
- Release con 0 warnings; ctest 14/14; pluginval strictness 10 ampliado con `--randomise`: SUCCESS (también con las 3
  semillas que fallaban).
- Pendiente para más adelante: detectar cambios de tonalidad dentro de una canción (ventana deslizante) y detectar
  desde lo que hay en la progresión, no solo desde el MIDI que entra.

Fase 4 COMPLETADA (2026-10-28). Probada por el usuario en FL Studio (progresión sincronizada, dembow, conducción
de voces, prestados): funciona.
- `libs/theory`: `Voicing.h`, `Rhythm.h`, `Progression.h` (+ `src/Progression.cpp`: `renderProgression`) y
  `borrowedChords` en `Harmony.h`. `DeepwaterTheoryTests` +4 grupos: I–IV–V–I con las inversiones de libro (C F A,
  B D G), notas comunes quietas, drop 2, piso del bajo, todas las tonalidades × modos × tamaños × estilos (notas del
  acorde, centro en la ventana); integridad de los patrones y el dembow 3+3+2; pasos en el tiempo, voicing de la del
  reggaetón (movimiento total 12 contra >24 en estado fundamental), render con acentos y cambio de paso dentro de un
  golpe; prestados de Do mayor y La menor en orden y en todas las tonalidades (ninguno diatónico, sin repetidos).
- Etapa MIDI: `MidiStage` con el tiempo (`beginBlock (sink, &clock)`, `process`, `endBlock`), modo progresión, ritmo
  en los dos modos, voicing, prestados en la paleta. Sin JUCE. Con los ajustes por defecto, idéntica a la Fase 3 (sus
  tests pasan sin cambios).
- Procesador: 3 parámetros (versionHint 3), progresión en el estado y en un `TripleBuffer`, `previewNumeral`; el
  snapshot publica el paso, si suena y la semicorchea del patrón.
- GUI 900 × 840: PALETA con la fila PRESTADOS (8, con su modo de origen) y PROGRESIÓN (teclas / progresión, ritmo,
  voicing; plantillas, texto romano editable, "Agregar con clic", vaciar; 8 pasos con grado, acorde, función, tiempos
  y "de frigio" si es prestado; mantener = escuchar; clic derecho = duración, tipo, mover, duplicar, quitar; rueda =
  tiempos; el paso de la posición de FL encendido; tira de 16 semicorcheas con el patrón y la actual). Revisada con
  `--snapshot` (FL en el compás 2: ♭VI suena como F/A, el dembow marca su semicorchea).
- Tests: `TidechartTests` +5 (voicing por teclas y paleta, prestados, códigos de la paleta; la progresión en vivo =
  `renderProgression` muestra a muestra en 9 ritmos × 3 voicings × bajo, con bloques al azar; salto, loop, tempo,
  parado, dos teclas, cambiar de modo, editar y vaciar la progresión, apagar; ritmo en el modo teclas; 10 semillas al
  azar con todo cambiando: nunca dos note-on seguidos, silencio al soltar). `TidechartProcessorTests` +1: la
  progresión con un host = `renderProgression` a 4 sample rates × bloques 1/7/64/512/4096/variable sin reservas,
  editarla mientras suena, el snapshot; estado con progresión (legible, no se duplica, proyecto de la Fase 3 → la de
  fábrica).
- Release con 0 warnings; ctest 14/14; pluginval strictness 10 ampliado: SUCCESS.
- Pendiente para más adelante: pedal en el bajo por paso (lo cubrirán las líneas de bajo de la Fase 6), más de 8
  pasos y arrastrar pasos (Fase 8), ritmos para compases que no sean de 4/4.

Fase 3 COMPLETADA (2026-10-28). Probada por el usuario en FL Studio (paleta y acordes con una tecla): funciona.
- Etapa MIDI: `ChordTrigger.h` (`chordForKey`, `chordForDegree`, `chordAtRoot`; slots con sus notas y cuenta de notas
  compartidas) y `MidiStage` con `setChordSettings` (por bloque), `requestPreview` (cualquier hilo) y las transiciones
  bypass ↔ acordes. Sin JUCE.
- Procesador: 5 parámetros nuevos (versionHint 2), `currentChordSettings`, `previewChord`; `heldDegrees` en el snapshot.
- GUI 900 × 740: PALETA bajo el reloj (tónica, modo, tríadas/séptimas, teclas, + Bajo; 7 pads con grado, acorde,
  función con color y tecla; mantener pulsado = escuchar, arrastrar = encadenar; la escala y el carácter del modo).
  ACORDE usa la tonalidad: grado, función y préstamo modal. Revisada con captura del Standalone (pad pulsado: Em, v).
- Tests: `TidechartTests` +3 (acordes por tecla en 12 tónicas × 10 modos × tríada/séptima × 2 modos de tecla × bajo
  × 128 teclas; la etapa con notas compartidas, cambiar ajustes con la tecla abajo, bypass, paleta y apagar; 10
  semillas al azar con todo cambiando: nunca dos note-on seguidos de una nota, silencio al soltar).
  `TidechartProcessorTests`: el passthrough en bypass y los acordes contra una etapa de referencia a 4 sample rates ×
  8 tamaños de bloque × mono/estéreo; apagados en los dos modos; bypass a mitad de un acorde; parámetros (Mi frigio,
  séptimas, bajo, notas de la escala) y paleta; estado del proyecto con los parámetros nuevos.
- Release con 0 warnings; ctest 14/14; pluginval strictness 10 ampliado: SUCCESS.

Fase 2 COMPLETADA (2026-10-28). Probada por el usuario en FL Studio (la fila ACORDE con acordes tocados): funciona.
- `libs/theory` creada (`deepwater::theory`, en el CMake raíz tras `libs/presets`): `Pitch.h`, `Scale.h` (mayor,
  dórico, frigio, lidio, mixolidio, menor, locrio, menor armónica, menor melódica, frigio dominante), `Chord.h`
  (38 tipos: tríadas, sus, 5, séptimas, 6, add9/add11, 9/11/13, alteradas 7♭9 7♯9 7♯5 7♭5 maj7♯5 maj7♯11 y m7♭9),
  `Harmony.h`, `StaticVector.h`; `Text.h` y `ChordFinder.h` con su código en `src/`. Al leer acepta ♭/♯ o b/#, °/o,
  ø/h, Δ/^ y –/-.
- `DeepwaterTheoryTests` (11 grupos): escritura e intervalos; nombres de nota con C5 = 60 y C4 = 60, ida y vuelta de
  las 128 notas; las 12 tónicas × 10 modos (letras consecutivas, la escritura de menos alteraciones); integridad del
  catálogo (alias, sufijos romanos y conjuntos de notas únicos); tonos escritos; ida y vuelta de >3000 símbolos (todos
  los tipos × 21 fundamentales × inversiones y bajo ajeno × ASCII/Unicode); voicings MIDI; grados diatónicos de cada
  modo en tríadas y séptimas; progresiones del contexto (i–♭VI–♭III–♭VII, andaluza, i–♭II, I–♭VI–♭VII–I) e ida y vuelta
  de todos los romanos; funciones y préstamo modal; reconocer acordes (C6 vs Am7 según el bajo, sin 5.ª, cada tipo e
  inversión del catálogo).
- Plugin: `flNoteName` usa la librería (mismo texto). GUI: fila ACORDE bajo SUENA AHORA (`gui/ChordView.h`): nombre del
  acorde que suena (Unicode), inversión con el tono del bajo, cada nota con su función (E♭5 (♭3)), otras lecturas; con
  2 notas, el intervalo en palabras. Corre en la GUI y solo recalcula si cambian las notas.
- Release con 0 warnings; ctest 14/14; pluginval strictness 10 ampliado: SUCCESS.

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

Siguiente: que el usuario pruebe la Fase 7 en FL; después, Fase 8 (GUI: teclado con la escala, círculo de quintas, progresión editable).
