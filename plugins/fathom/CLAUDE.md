# CLAUDE.md — Fathom, EQ paramétrico con analizador

Plugin 4 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del EQ.

Nombre: **Fathom** ("sondear la profundidad": medir antes de tocar, como un EQ que se usa mirando el analizador).
Target `Fathom`, `PLUGIN_CODE Fth1`; fabricante `Undw`, el de toda la suite; `COMPANY_NAME "Deepwater"`. Decidido con
el usuario al empezar la Fase 1 (otras ideas que se consideraron: Isobath, Shoal, Reef).

## Objetivo del proyecto
Construir un EQ paramétrico al nivel de FabFilter Pro-Q 4 (pedido explícito del usuario: "como Pro-Q 4"), con
nombre, diseño y código propios: 24 bandas, todas las formas de filtro, pendientes de 6 a 96 dB/oct, edición directa
sobre el analizador, respuesta analógica sin "aplastamiento" cerca de Nyquist, estéreo por banda (L/R/Mid/Side), EQ
dinámico, modos de fase natural y lineal, dinámica espectral, igualación con una referencia, dibujo a mano de la
curva, color analógico y vista de las demás instancias. Se usa en FL Studio sobre Windows, en todo: voces, 808,
kicks, buses, master.

Pro-Q es la REFERENCIA DE NIVEL, no un plano: nada de copiar su interfaz al píxel ni replicar sus algoritmos
propietarios (su "Natural Phase", su dinámica espectral). Cada función se construye desde ideas publicadas (papers,
libros, blogs técnicos) y se explica antes de implementarla.

## Requisitos propios (además de las reglas del hilo de audio del raíz)
- Mover cualquier control en plena reproducción (frecuencia, ganancia, Q, forma, pendiente, encender una banda,
  cambiar el modo de fase) no causa clics ni zipper noise, ni con automatización rápida.
- Una banda en 0 dB o apagada deja la señal intacta (bit a bit cuando sea posible). Con todo plano, el EQ es
  transparente en todos los modos.
- La curva dibujada es la respuesta REAL del audio (calculada con los mismos coeficientes), no una aproximación.
- Estable con cualquier combinación de parámetros a 44.1, 48, 88.2 y 96 kHz (frecuencias cerca de Nyquist, Q extremos,
  cortes de 96 dB/oct apilados).
- Latencia: 0 en el modo normal; la de los modos de fase lineal/natural se informa al host y el bypass la mantiene.
- Es un efecto: mono y estéreo. El sidechain externo (EQ dinámico, igualación) es opcional.

## Librería DSP compartida
Lo que Fathom va a usar de `libs/dsp` (`deepwater::dsp`):
- `Svf.h`: base de los filtros de las bandas (TPT/ZDF: estable al modular).
- `EqFilter.h` (nació en la Fase 2, completo en la Fase 3): `EqCoefficients` (mezcla de la entrada con las salidas
  LP/BP/HP del SVF: campana, shelves, tilt shelf, notch, pasa banda, all-pass, LP/HP de 1 y 2 polos) con
  `response()`/`magnitudeDb()` exactas para esos coeficientes; `EqCascade` (hasta 8 etapas en serie: Butterworth de
  1–16 polos con resonancia) y `FlatTiltDesign` (pendiente constante en dB/oct); `EqFilter` y `EqCascadeFilter`.
- `SpectrumAnalyzer.h` + `AnalyzerTap.h` (vienen de Riptide): el analizador y la toma audio → GUI. En la Fase 5
  ganó `peak()` (el pico de una zona afinado con una parábola por los bins: < 0.05 bin).
- `Smoothing.h` (`LinearRamp`, `EffectSwitch`), `Decibels.h`, `Pitch.h`, `LevelMeter.h`.
- `Fft.h`: fase lineal y dinámica espectral (Fases 10–11).
- `EnvelopeFollower.h`: detector del EQ dinámico (Fase 9).
- `Oversampler.h`, `FastMath.h`: el color analógico (Fase 13).
- `libs/presets` (`deepwater::presets`): presets, navegador y panel de guardar (Fase 15).

De Ebb no hizo falta mover nada al empezar: lo reutilizable (`TripleBuffer`, `BreakpointCurve`, `TransientDetector`)
ya está en la librería. Lo nuevo que sirva a otros plugins va a la librería con tests: los filtros de EQ (campana,
shelves, cortes de orden alto) y su respuesta en frecuencia le sirven al compresor (filtro del detector), al delay y a
la reverb (filtros del feedback); la convolución por FFT, a la reverb.

## Comandos
Desde la raíz del repositorio (ver también los comandos generales en el `CLAUDE.md` raíz):
```
cmake --build build --config Release --target Fathom_VST3      # solo el plugin
build/plugins/fathom/Release/FathomTests.exe                    # tests del motor (sin JUCE)
```
- VST3: `build/plugins/fathom/Fathom_artefacts/Release/VST3/Fathom.vst3`.
- Standalone: `build/plugins/fathom/Fathom_artefacts/Release/Standalone/Fathom.exe`.

## Estructura
```
plugins/fathom/
  CMakeLists.txt           # plugin y tests; enlaza deepwater::dsp
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado (con la vista del analizador y el otro lado del A/B), processBlock,
                             # el bus de sidechain, las tomas del analizador, el historial de deshacer y el solo
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    dsp/Engine.h             # cadena de audio completa, sin JUCE: 24 bandas en serie, escala de ganancia, salida,
                             # auto gain, balance, polaridad, solo y bypass
    dsp/AutoGain.h           # cuánto cambia la sonoridad la curva (ruido rosa con peso K de BS.1770), por banda
    dsp/Solo.h               # la zona que se escucha en solo según la forma de la banda
    dsp/Band.h               # una banda: formas (Shape), pendientes, ajustes (BandSettings), parámetros suavizados,
                             # fundido de encendido/cambio de forma y coeficientes para la GUI
    edit/UndoStack.h         # pila de deshacer/rehacer (sin JUCE): pasos con solo los parámetros que cambiaron
    edit/EditHistory.h       # deshacer con JUCE: escucha los gestos de los parámetros (no la automatización del host);
                             # fotos y carga de todos los parámetros para el A/B
    edit/NodeEditing.h       # cuentas de la edición sin JUCE: arrastrar nodos, rueda = Q, banda libre, notas
    analysis/SpectrumView.h  # cuentas del analizador sin JUCE: inclinación, rango, resolución, balística, retención
                             # de picos, ajustes de la vista (ViewSettings) y búsqueda de picos del espectro
    gui/                     # tema (con el color de cada banda), LookAndFeel, formato de valores, atajos (Keys),
                             # visor (EqDisplay: cuadrícula, analizador, curvas, nodos editables, teclado de piano,
                             # agarrar picos), BandEdits (cambiar parámetros con gestos), panel de la banda elegida
                             # (BandPanel, con SOLO), ajustes del analizador (AnalyzerPanel), columna de salida
                             # (OutputPanel: escala, auto gain, balance, polaridad, ganancia)
  tests/EngineTests.cpp    # tests del motor (con auto gain y solo), de la edición y del analizador (sin JUCE)
```

## Aprendizaje de mezcla (igual de importante que el código)
Además de los 6 puntos del raíz (en efectos, siempre con el volumen igualado), este plugin se enfoca en:
- **El mapa de frecuencias**: sub (20–60 Hz), graves (60–250), medios-graves / "barro" (250–500), medios / "caja"
  (500 Hz–2 kHz), presencia (2–5 kHz), brillo (5–10 kHz) y aire (10–20 kHz); qué instrumento vive en cada zona y cómo
  se oye cada zona de más o de menos. Frecuencia ↔ nota (FL: A5 = 440 Hz).
- **Restar antes que sumar**: cortes angostos para quitar resonancias, subidas anchas y suaves para dar carácter.
- **Q y ancho de banda** (octavas), **pendientes** (por qué un corte de 96 dB/oct suena distinto de uno de 12).
- **Enmascaramiento**: dos instrumentos en la misma zona se tapan (kick y 808, voz y acordes); tallar espacio.
- **Fase**: qué cambia un EQ además del volumen, cuándo importa (graves de un kick, capas en paralelo, buses) y qué
  cuesta la fase lineal (pre-eco, latencia).
- **EQ dinámico vs. estático vs. compresor multibanda**; **Mid/Side** (ensanchar sin enturbiar el centro).
- **El sesgo del volumen**: más fuerte parece "mejor"; por eso se compara siempre con el volumen igualado.
- Contexto del usuario: reggaetón experimental. Ejercicios sobre 808 + kick del dembow, voces con autotune, percusión
  y buses; el master al final.

Actualizar el archivo `APRENDIZAJE.md` de esta carpeta (`plugins/fathom/`) al final de cada fase. Los presets de
fábrica incluyen notas de para qué sirven y por qué.

## Hoja de ruta
- [x] Fase 1 — Proyecto base y lienzo: `plugins/fathom/` (VST3 + Standalone, efecto mono/estéreo) en el CMake raíz.
      Passthrough, ganancia de salida y bypass sin clics. El visor que será el "lienzo" del EQ: eje de frecuencias
      logarítmico (20 Hz–20 kHz), escala en dB, lectura de frecuencia + nota bajo el mouse y el analizador de entrada
      y salida (`SpectrumAnalyzer` de la librería). Tests y pluginval.
- [x] Fase 2 — La campana: una banda peaking (frecuencia, ganancia, Q) construida desde el SVF; filtro de EQ en la
      librería con su respuesta en frecuencia analítica (para dibujar la curva real) y tests contra la teoría.
      Coeficientes suavizados sin clics. La curva dibujada sobre el analizador.
- [x] Fase 3 — Todas las formas y 24 bandas: shelves grave/agudo, tilt shelf, flat tilt, notch, band-pass, all-pass,
      cortes grave/agudo de 6 a 96 dB/oct (Butterworth en cascada, con resonancia). Los parámetros de las 24 bandas
      (IDs definitivos), apagadas por defecto.
- [x] Fase 4 — Edición sobre el visor: nodos arrastrables (doble clic crea una banda donde se clica, arrastrar mueve
      frecuencia/ganancia, rueda = Q, modificadores para ajuste fino y para un solo eje), panel de la banda elegida,
      selección múltiple, deshacer/rehacer y teclado de piano en el eje.
- [x] Fase 5 — Analizador profesional: inclinación de 4.5 dB/oct, rango, resolución, velocidad, retención de picos,
      congelar; ver entrada/salida/sidechain; "agarrar" un pico del espectro para crear o mover una banda.
- [x] Fase 6 — Escuchar y comparar: solo de banda (escuchar la zona que toca), auto gain (volumen igualado al
      comparar), escala de ganancia global, ganancia y paneo de salida, invertir la fase, comparación A/B.
- [ ] Fase 7 — Respuesta analógica: corregir el aplastamiento de campanas y shelves agudos cerca de Nyquist
      (bilineal "cramping") sin latencia. Opciones: sobremuestreo vs. diseño "matched" (Vicanek); medir contra el
      prototipo analógico.
- [ ] Fase 8 — Estéreo por banda: Estéreo / L / R / Mid / Side; analizador M/S.
- [ ] Fase 9 — EQ dinámico: por banda umbral, rango (corta o sube), ataque/liberación y umbral automático; detector
      con la forma de la banda o con el sidechain externo; la curva se mueve en vivo en el visor.
- [ ] Fase 10 — Modos de fase: fase lineal (convolución por FFT particionada, varias calidades, latencia informada)
      y fase "natural" propia (magnitud y fase del analógico); cambio de modo sin clics.
- [ ] Fase 11 — Dinámica espectral: una banda dinámica que actúa por bin de la FFT (solo donde hay exceso, no en toda
      la zona): domar resonancias que cambian con las notas.
- [ ] Fase 12 — Igualar y dibujar: igualar el espectro a una referencia (sidechain) creando bandas; dibujar la curva a
      mano y convertirla en bandas.
- [ ] Fase 13 — Color analógico: modos limpio / sutil / cálido (saturación suave con sobremuestreo, con lo aprendido
      en Riptide).
- [ ] Fase 14 — Otras instancias: ver el espectro de las demás instancias de Fathom del proyecto y marcar dónde chocan
      (enmascaramiento); editarlas desde aquí.
- [ ] Fase 15 — Presets con notas educativas (voces, 808 y kick del dembow, percusión, buses, master).
- [ ] Fase 16 — Pulido: CPU (24 bandas con SIMD, coeficientes), GUI escalable, pluginval estricto, pruebas de
      automatización extrema.

## Estado actual
Fase 6 COMPLETADA (2026-10-24), pendiente de probar en FL Studio.
- `dsp/Engine.h`: después de las bandas, un multiplicador por canal = salida × auto gain × balance × polaridad, todo con
  `LinearRamp` de 20 ms (si todo vale 1, bit a bit); después el solo (fundido de 20 ms entre la mezcla y la zona) y al
  final el bypass. `setGainScale` (0–2: multiplica `gainDb` de cada banda en `setBand`, incluido el flat tilt; las bandas
  ya suavizan los dB), `setPan` (balance: L = min(1, 1 − p), R = min(1, 1 + p); en mono no hace nada),
  `setPhaseInverted` (rampa +1 → −1, pasa por 0), `setAutoGain`, `setSolo (banda o −1)`, `getAutoGainDb`, `isSoloing`.
  Los ajustes pedidos antes de `prepare` arrancan sin rampa. La firma de `prepare` no cambió.
- `dsp/AutoGain.h`: 60 puntos (cada 1/6 de octava desde 20 Hz, tope 0.45·fs), peso = |K|² del filtro K de BS.1770
  (high-pass 2 polos 38.13 Hz Q 0.5 + shelf +4 dB 1681.97 Hz) normalizado; con puntos equiespaciados en octavas el ruido
  rosa pesa igual en todos. Compensación = −10·log10(Σ w·Π|H_b|²), límite ±24 dB. Cada banda guarda su |H|² y solo se
  recalcula si cambian sus ajustes (dos bandas apagadas = iguales); la suma solo si algo cambió. En el motor se actualiza
  una vez por bloque y solo con el auto gain encendido. Valores (48 kHz): shelf agudos +3 dB 1 kHz → −2.25; shelf graves
  +3 dB 200 Hz → −0.49; campana +2 dB 3 kHz Q 0.7 → −0.72; campana +6 dB Q 8 → −0.23; corte 30 Hz → +0.03; corte
  100 Hz 24 dB/oct → +0.29. La curva del visor NO incluye el auto gain (se muestra el número en la columna de salida).
- `dsp/Solo.h`: `soloZoneFor` = campana/notch/pasa banda/all-pass → pasa banda con su frecuencia y su Q; shelf y corte de
  graves → pasa bajos (el corte con su misma pendiente, el shelf 12 dB/oct, Q 0.71); de agudos → pasa altos; tilts →
  sin zona (`canSolo`). En el motor la zona es otra `Band` sobre la señal ORIGINAL (0 dB en el centro); la misma banda y
  la misma clase de filtro = sus rampas; otra banda u otra clase = fundido a la mezcla, `prepare` de la zona nueva en
  silencio y fundido al solo. Funciona aunque la banda esté apagada. El bypass manda sobre el solo.
- Parámetros nuevos (versionHint 6): `gainScale` (0–200 %, 100), `outputPan` (−100…100, "C" / "L n" / "R n"),
  `phaseInvert` (off), `autoGain` (off). Un proyecto de las Fases 1-5 suena igual.
- Procesador: `setSoloBand`/`getSoloBand` (atómico, no es parámetro: no se guarda; el editor lo pone en −1 al cerrarse),
  `getAutoGainDb` (atómico desde el audio). A/B: hijo `Compare` del estado del APVTS con `side` (0 = A, 1 = B) y un hijo
  `Other` con un atributo por parámetro (ID = valor en sus unidades; lo que falta = valor por defecto). `switchCompareSide`
  (solo sin ediciones a medias): foto del activo → `Other`, carga del otro (la primera vez, copia del activo) con
  `EditHistory::load` (con gestos, sin paso de deshacer) y `swapStack` con `otherSideHistory`: cada lado tiene su propio
  historial. `copyToOtherSide`. Cubre los mismos parámetros que el deshacer (todos menos el bypass).
- GUI: ventana de 1082 px (antes 960): columna nueva `gui/OutputPanel.h` a la derecha del analizador (ESCALA, AUTO GAIN
  con lo que compensa, BALANCE, Ø POLARIDAD, GANANCIA; la perilla de salida salió de la columna del analizador).
  Cabecera: A / B / "A → B" (o "B → A") a la izquierda de DESHACER. Panel de la banda: SOLO (amarillo, `theme::solo`)
  junto a ON; sigue a la banda del panel; atenuado en los tilts. Visor: "Escuchar su zona (SOLO)" en el menú del nodo,
  aviso "SOLO: BANDA n" (o que el tilt no tiene zona); curvas y nodos con la escala aplicada; arrastrar y crear dividen
  por la escala (mínimo 0.25) para que el nodo siga al mouse; la etiqueta muestra "+6.0 dB → +3.0 dB" con escala ≠ 100 %.
- Tests nuevos `FathomTests`: escala (50 % = la curva a la mitad < 0.01 dB; 0 % bit a bit), balance y polaridad
  (valores exactos, rampa sin saltos, mono, invertir y volver = bit a bit), auto gain (plano = 0 dB y bit a bit; todo
  +6 dB → −6 ± 0.05; angosto y extremos ≈ 0; all-pass 0; todo cortado = límite; incremental = desde cero; tono al mismo
  nivel ±0.1 dB de punta a punta; encenderlo con la banda saltando cada bloque sin clic), solo (zona medida = curva
  < 0.01 dB, centro > −1 dB, lejos < −24 dB, tilt sin solo, bypass manda) y cambiar de solo entre bandas y clases de
  filtro sin saltos (con el fundido en 10 µs el test falla: salto 0.5 contra cota 0.14) y bit a bit al soltarlo.
- Revisada con clics automatizados en el Standalone (ON, AUTO GAIN, SOLO y su aviso, A → B con cambios en B y vuelta a
  A, cada lado con su DESHACER). OJO capturas: PowerShell debe llamar a `SetProcessDPIAware` (si no, la ventana sale
  recortada) y los clics necesitan una pausa entre bajar y soltar.
- Sin cambios: las bandas, sus parámetros e IDs, el analizador, latencia 0.
- Release con 0 warnings; ctest 8/8 (UndertowTests falló una vez por su test de CPU, 20.7 % contra 20 %, sin cambios en
  Undertow; al repetirlo pasó: 19.2 %); pluginval strictness 10 ampliado: SUCCESS.

### Lo que quedó de la Fase 5
- `analysis/SpectrumView.h` (sin JUCE): inclinación k·log2(f / 1 kHz) con 0 / 3 / 4.5 / 6 dB/oct (4.5 por defecto);
  rango 48 / 72 / 96 / 120 dB (múltiplos de 8: la cuadrícula cae en números redondos; 96 por defecto); resolución =
  FFT de 2048 a 16384 a 44.1/48 kHz (el doble a 88.2/96: misma duración; media = 4096, la de antes); velocidad =
  constante de tiempo de subida + caída en dB/s (`follow`, independiente de los cuadros por segundo; media = 40 dB/s, la
  de antes); retención de picos No / 2 s (después cae a 20 dB/s) / Siempre (`hold`); `ViewSettings`; `findPeaks`
  (máximo de su vecindario de ±1/6 de octava y prominencia ≥ 6 dB sobre el valle más alto de los dos lados; en una
  meseta, uno solo) y `nearestPeak`.
- Librería: `SpectrumAnalyzer::peak (lowHz, highHz, sr)`: el bin más fuerte de la zona + parábola en dB por él y sus
  vecinos (< 0.05 bin y < 0.4 dB con Hann; si la zona no tiene un máximo local, el bin). Aditivo: no cambia nada de lo
  que ya usaban Riptide y Fathom.
- Procesador: bus de entrada "Sidechain" (estéreo, activo por defecto como en Ebb; acepta mono, estéreo o
  desactivado); el motor procesa solo los canales del bus principal (`getMainBusNumInputChannels`). Tercera toma
  `sidechainTap` (solo si el bus está conectado) e `isSidechainConnected()`. Tomas de 32768 muestras (la FFT máxima a
  96 kHz). La vista del analizador se guarda en el proyecto como hijo `AnalyzerView` del estado del APVTS (no son
  parámetros ni entran al deshacer; lo que falta = valor por defecto: los proyectos de las Fases 1-4 se ven igual que
  antes, salvo la inclinación).
- GUI: `gui/AnalyzerPanel.h` en la columna derecha (sobre SALIDA): ENTRADA / SALIDA / SIDECHAIN, INCLINACIÓN, RANGO,
  RESOLUCIÓN, VELOCIDAD, PICOS, BORRAR PICOS y CONGELAR (no se guarda). El editor guarda la vista al cambiarla y en
  cada cuadro compara con la del proyecto (cargar un proyecto con la ventana abierta). Visor: sidechain en ámbar,
  retención como línea fina sobre cada espectro visible, avisos "CONGELADO" y "SIDECHAIN SIN CONECTAR"; los botones
  ENTRADA/SALIDA salieron de la cabecera del visor.
- Agarrar picos (`EqDisplay`): con el mouse en el visor se buscan los picos del espectro de la SALIDA (si no se ve, la
  entrada; si no, el sidechain). A menos de 14 px en horizontal y 40 px de la línea aparece una marca con Hz y nota
  (afinados con `peak()`); clic y arrastrar = campana nueva en la primera banda libre, encendida, 0 dB, Q 4, justo en
  el pico, y se arrastra como un nodo (crear + arrastrar = un paso de deshacer); mientras tanto el espectro se congela.
  El segundo clic de un doble clic no agarra otra vez ni crea otra banda. Ctrl mientras se arrastra UN nodo = imán al
  pico más cercano (24 px); al alejarse vuelve a seguir al mouse. Los nodos tienen prioridad sobre los picos.
- Arreglado de paso: el tooltip de DESHACER tenía la "ú" codificada dos veces ("Ãº").
- Tests nuevos: `DeepwaterDspTests` (`peak()` entre bins a 4 frecuencias de muestreo y una zona sin máximo local);
  `FathomTests` (inclinación, tamaños de FFT, valores por defecto = los de antes, balística igual a 30 y 60 cuadros por
  segundo, constante de tiempo, retención en sus tres modos, picos con prominencia/meseta/piso/el más cercano, y de
  punta a punta: un seno de 87.31 Hz y otro de 2.5 kHz se agarran a < 0.5 Hz a 44.1 y 96 kHz).
- Revisada con clics automatizados en el Standalone (panel, SIDECHAIN sin conectar, CONGELAR, rango 120 dB, PICOS,
  la vista guardada al cerrar y abrir). Agarrar picos e imán NO se pudieron probar ahí (el Standalone silencia la
  entrada): solo con los tests de punta a punta; probarlos en FL.
- Sin cambios: el audio (motor, bandas, parámetros e IDs), latencia 0.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

### Lo que quedó de la Fase 4
- Edición sobre el visor (`gui/EqDisplay.h`): doble clic en el vacío = crear en la primera banda LIBRE (apagada y con
  sus valores por defecto; `edit::freeBand`), campana con la ganancia de la altura del clic; bajo 25 Hz corte de graves,
  sobre 16 kHz corte de agudos (shelves y cortes nacen con Q 0.71). Doble clic en un nodo = apagar/encender. Supr =
  borrar las elegidas (vuelven a sus valores por defecto: libres). Arrastrar = frecuencia en octavas + ganancia en dB
  (`edit::dragged`; shelves × 2 porque su nodo está en el punto medio; formas sin ganancia solo en horizontal; límite
  20 Hz–20 kHz), por diferencias entre eventos: Shift = fino (× 0.1) a mitad del arrastre sin saltos, Alt = solo el eje
  que más se movió. Rueda sobre un nodo = Q (Q × 2^deltaY: ≈ × 1.18 por muesca; Shift fino); el paso de deshacer de la
  rueda se cierra tras 0.5 s quieta. Selección múltiple: Ctrl+clic, rectángulo en el vacío (Ctrl = agrega), Ctrl+A =
  las que están en uso, Esc = solo la del panel; la del panel (`focusedBand`) siempre está entre las elegidas.
  Clic derecho en un nodo: encender/apagar, forma, pendiente (solo cortes), afinar a la nota más cercana, borrar (para
  todas las elegidas); en el vacío: crear con cualquier forma, elegir todas. Etiqueta con banda/forma/Hz/nota/dB/Q
  junto al nodo bajo el mouse o arrastrado. Nodos: llenos si suenan, huecos si están apagadas pero en uso (o es la del
  panel), anillo blanco = elegidas. Ayudas en los tooltips y en el pie del editor.
- Teclado de piano bajo el visor (16 px, entre la curva y las etiquetas de Hz): un semitono = una columna (eje log),
  negras arriba, cada Do con su nombre de FL (C5 = MIDI 60); la tecla de la banda del panel en su color. Clic en una
  tecla = la banda del panel justo a esa nota (y encendida).
- `gui/BandEdits.h`: cambia parámetros como una perilla (abre el gesto de cada parámetro la primera vez que lo toca y
  los cierra todos en `end()`); `setSettings`, `reset` (valores por defecto).
- Deshacer/rehacer (`edit/EditHistory.h` en el procesador: sobrevive a cerrar la ventana): escucha
  `parameterGestureChanged` de todos los parámetros menos el bypass; foto "antes" al abrirse el primer gesto, "después"
  al cerrarse el último (+ `beginTransaction`/`endTransaction` para agrupar), y `UndoStack` guarda solo los parámetros
  que cambiaron (deshacer no pisa la automatización del host, que no manda gestos). Aplica con gestos (el host graba).
  Límite 200 pasos. Cubre visor, panel y salida. Se eligió frente al `UndoManager` del APVTS porque ese registra al
  copiar el estado al ValueTree (con temporizador, mezclando la automatización) y no por gesto.
- Atajos (`gui/Keys.h`, `commandLetter`): JUCE en Windows entrega Ctrl+letra como el carácter de control (Ctrl+Z =
  código 26), así que comparar con `KeyPress ('z', ctrl, 0)` fallaba. Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z en el editor
  (botones DESHACER/REHACER en la cabecera también); Supr/Ctrl+A/Esc en el visor (que toma el foco al hacer clic).
- El procesador expone `getDefaultBandSettings (b)` (leídos de los parámetros) y `getEditHistory()`. Editor de 640 px
  de alto (antes 620) por el teclado.
- Tests `FathomTests` nuevos: `UndoStack` (solo lo que cambió, orden, rehacer se borra con un cambio nuevo, límite),
  arrastre (octavas/dB, el nodo del shelf sigue al mouse medido con la curva real, cortes solo en horizontal, límites,
  Q de la rueda), banda libre, forma según la posición, notas exactas y teclas negras.
- Revisada con clics automatizados en el Standalone (crear, arrastrar, Shift en grupo, Alt, rueda, rectángulo,
  Ctrl+clic, menú, Supr, Ctrl+Z, botón deshacer, teclado de piano).
- Sin cambios: el audio (motor, bandas, parámetros e IDs), el analizador y las escalas.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

### Lo que quedó de la Fase 3
- Librería (`EqFilter.h`): `EqCoefficients` ganó la salida high-pass del SVF (`highPass`; con 0 la campana suena
  igual) y las formas: `lowShelf`/`highShelf` (Cookbook sobre el SVF a la Simper: corte del SVF ÷/× √A, la frecuencia
  es el punto medio de la subida; Q 0.71 = sin rebote), `tiltShelf` (shelf de agudos × 1/A: graves −g/2, agudos +g/2,
  0 dB en la frecuencia), `notch` (1 − k·BP), `bandPassFilter` (k·BP, 0 dB en el centro), `allPass` (1 − 2k·BP),
  `lowPass2`/`highPass2` y `lowPass1`/`highPass1` (1 polo con el SVF: k = 2 y un cero que cancela un polo). `EqCascade`:
  hasta 8 etapas en serie, `butterworthHighPass/LowPass (hz, orden 1-16, q)`: Q de cada etapa 1/(2·sin((2i−1)π/2n)),
  la resonancia multiplica la etapa más resonante por q·√2 (q 0.71 = Butterworth puro, −3 dB en el corte).
  `FlatTiltDesign`: 16 pares polo-cero de 1er orden, uno por octava desde 0.4·fs hacia abajo (8 etapas de SVF con polos
  reales), razón aplicada en el eje deformado (sin tan() por muestra al moverla), normalizado a 0 dB exactos en el
  pivote; error < 0.012 dB por dB/oct hasta 1 kHz y < 0.2 dB por dB/oct a 20 kHz (la bilineal curva el final).
  Tests nuevos en `DeepwaterDspTests`: shelves (mesetas, punto medio exacto, +x/−x, Q 0.71 monótono, 0 dB bit a bit),
  notch/pasa banda/all-pass (hueco < −100 dB, ancho −3 dB, all-pass plano < 1e-5 dB también medido), Butterworth 1–16
  polos = |H|² = 1/(1 + (Ωc/Ω)^2n) exacto (< 0.00001 dB) y medido, resonancia; flat tilt (recta, pivote, bit a bit
  con 0); todas las formas estables con todo cambiando en cada muestra.
- `dsp/Band.h`: `Shape` (orden del parámetro: campana, shelf graves, shelf agudos, tilt shelf, flat tilt, corte graves,
  corte agudos, notch, pasa banda, all-pass; agregar solo al final), `slopes` {6, 12, 18, 24, 30, 36, 48, 72, 96},
  `usesGain/usesQ/usesSlope`. Flat tilt: ganancia × 0.1 = dB/oct (±3 dB/oct). Rampas de 20 ms en octavas, dB y log Q
  como en la Fase 2, más un fundido `mix` de 20 ms (salida = x + mix·(filtrada − x)): encender/apagar, y cambiar de
  forma o de pendiente = fundido a 0, cambio con el filtro en limpio (reset) y fundido a 1. Apagada y con el fundido en
  0 la banda está "idle": no procesa (cambio respecto de la Fase 2, donde el SVF corría siempre).
- `dsp/Engine.h`: 24 bandas en serie, procesadas por tramos de 64 muestras banda por banda (buffer fijo para la señal
  original del bypass); `BandArray`, `setBand (índice, ajustes)`, `getBand (índice)`.
- Parámetros (IDs definitivos, `params::band (b, ajuste)` = "band<b+1><Ajuste>"): por banda `Enabled` (off),
  `Frequency` (cada banda en la suya: 30, 40, 50, 65, 85, 115, 150, 200, 260, 350, 460, 600, 800, 1000, 1400, 1800,
  2400, 3200, 4200, 5500, 7300, 9600, 12500, 16000 Hz), `Gain`, `Q`, `Shape` (choice, Bell por defecto: los proyectos de
  la Fase 2 suenan igual) y `Slope` (choice, 12 dB/oct). La banda 1 conserva `versionHintPhase2` en sus 4 ajustes de la
  Fase 2; lo demás, `versionHintPhase3`. Cambió el valor inicial de `band1Enabled` (on → off) y `band1Frequency`
  (1 kHz → 30 Hz): no afecta a proyectos guardados (guardan sus valores).
- GUI: panel con selector de banda (las encendidas con ●), ON, FORMA, PENDIENTE (activa solo en cortes), FRECUENCIA /
  GANANCIA / Q (las que la forma no usa, atenuadas) con el color de la banda, y un texto según la forma (centro y ancho,
  punto medio, corte −3 dB y pendiente, dB/oct del flat tilt...). Visor: curva total (suma en dB de las 24 bandas + la
  salida), relleno de cada banda encendida en su color (la elegida más marcado y con su curva propia), nodos numerados
  (sobre la curva de la banda si la forma tiene ganancia, si no en 0 dB; la elegida se ve aunque esté apagada); clic en
  un nodo = elegir la banda. Combos con letra de 13 px. Revisada con capturas del Standalone (clics automatizados).
- Tests `FathomTests` nuevos: cada forma y varias pendientes con otras bandas encendidas = curva de la GUI (< 0.00001
  dB), 24 campanas en 0 dB bit a bit, corte/notch/flat tilt/corte de 6 dB apagados = bit a bit e idle, forma/pendiente/
  encendido/ajustes al azar en 3 bandas cada bloque sin clics (mitad de la cota; sin fundidos sale 1000 veces sobre
  ella), 24 bandas al azar con extremos estables. Los de la Fase 2 adaptados a la API de 24 bandas.
- Sin cambios: salida ±24 dB, bypass con fundido, tomas del analizador, latencia 0, escalas del visor.
- Release con 0 warnings; ctest 8/8; pluginval strictness 10 ampliado: SUCCESS.

Siguiente: Fase 7 (respuesta analógica), cuando el usuario confirme la Fase 6 en FL.
