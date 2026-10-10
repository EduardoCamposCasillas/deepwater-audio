# CLAUDE.md — Riptide, saturador

Plugin 2 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del saturador.

Nombre: **Riptide** (target `Riptide`, `PLUGIN_CODE Rpt1`; fabricante `Undw`, el de toda la suite; `COMPANY_NAME
"Deepwater"`). Hasta la Fase 1 la carpeta se llamaba `plugins/saturador/`.

## Objetivo del proyecto
Construir un saturador de calidad profesional (referencia de nivel: Soundtoys Decapitator y FabFilter Saturn 2),
con nombre, diseño y código propios. Se usa principalmente en FL Studio sobre Windows, en bajos, voces,
baterías, buses y master.

Es el segundo plugin del dueño del proyecto, que ya construyó un sintetizador wavetable con JUCE (Undertow).
Sigue APRENDIENDO programación de audio, DSP y ahora también MEZCLA: el objetivo es doble,
un plugin de nivel profesional y que él entienda cada parte y sepa cuándo y cómo usar saturación.

## Requisitos propios (además de las reglas del hilo de audio del raíz)
- Cambiar el factor de oversampling no debe causar clics ni crasheos (todos los buffers para el factor máximo se
  reservan en `prepareToPlay`; la latencia informada al host cambia con el factor).
- Es un efecto: funciona en mono y en estéreo, y con el bypass del host y el propio sin clics.

## Librería DSP compartida
`libs/dsp` (`deepwater::dsp`) ya existe: se creó al migrar Undertow al monorepo. Lo que el saturador va a usar:
- `Halfband.h`: decimador e interpolador halfband ×2 (100 dB). El oversampling 2x–16x de la Fase 2 se construye
  encadenando etapas ×2 (la latencia total es la suma de las etapas: hay que informarla al host).
- `Svf.h`: SVF TPT/ZDF para los filtros pre/post y el tono (Fase 4); base de los crossovers (Fase 6).
- `Smoothing.h`: suavizado de gain, drive y mix; fundidos de bypass.
- `FastMath.h` (`fastTanh`), `Fft.h` (analizador fuera del hilo de audio), `Simd.h`.

Todo DSP nuevo que sirva para futuros plugins (EQ, compresor, delay, reverb) va en la librería, con tests. Candidatos
que hoy están en Undertow y podrían moverse cuando haga falta: el ADAA de `Distortion.h` (Fase 2/3) y el LFO
(`Lfo.h`, Fase 7). Moverlos exige el null test de Undertow (ver el `CLAUDE.md` raíz).

## Aprendizaje de mezcla y diseño sonoro (igual de importante que el código)
Al terminar cada fase:
1. **Qué hace cada control al sonido**, en lenguaje musical: calidez, brillo, aspereza, densidad, pegada,
   presencia. Relacionarlo con el analizador de espectro (qué armónicos aparecen) y la forma de onda.
2. **Ejercicio de escucha en FL**, siempre con el volumen igualado (gain-matched), sobre material real:
   un loop de batería, un bajo, una voz. Qué mover, de qué valor a cuál y qué notar.
3. **Receta de uso**: cómo usar lo construido en un caso real de mezcla (ej. dar presencia a un 808
   en bocinas pequeñas, engrosar una voz, pegar un bus de batería), explicando por qué.
4. **Reto sin receta**: describir un objetivo de mezcla, dejar que yo lo intente y luego dar pistas.
5. **Vocabulario** nuevo con definición corta y ejemplo de dónde se escucha en música real
   (ej. armónicos pares/impares, headroom, clipping suave/duro, aliasing, crossover).
6. **Errores comunes**: cuándo NO usar saturación o cuándo es demasiada.

Actualizar el archivo `APRENDIZAJE.md` de esta carpeta (`plugins/riptide/`) al final de cada fase (conceptos,
recetas de uso, vocabulario, retos completados). Los presets de fábrica incluyen notas de para qué sirven y por qué.

## Comandos
Desde la raíz del repositorio (ver también los comandos generales en el `CLAUDE.md` raíz):
```
cmake --build build --config Release --target Riptide_VST3          # solo el plugin
build/plugins/riptide/Release/RiptideTests.exe                       # tests del motor (sin JUCE)
```
- VST3: `build/plugins/riptide/Riptide_artefacts/Release/VST3/Riptide.vst3`.
- Standalone: `build/plugins/riptide/Riptide_artefacts/Release/Standalone/Riptide.exe` (la entrada de audio arranca
  silenciada para evitar realimentación: activarla en Options).

## Estructura
```
plugins/riptide/
  CMakeLists.txt           # plugin y tests; enlaza deepwater::dsp
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado, processBlock, publicación de medidores
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    dsp/Engine.h             # cadena de audio completa, sin JUCE
    gui/                     # tema, LookAndFeel, medidores
  tests/EngineTests.cpp    # tests del motor (sin JUCE)
```

## Hoja de ruta
- [x] Fase 1 — Proyecto base: `plugins/riptide/` con su `CMakeLists.txt` (VST3 + Standalone, efecto mono/estéreo),
      añadido al CMake raíz y enlazado a `deepwater::dsp`. Passthrough con input y output gain (dB, suavizados con
      `Smoothing.h`), medidores de nivel de entrada y salida (pico y RMS) en una GUI funcional y bypass sin clics.
      Tests de lo que no dependa de JUCE y pluginval. Decidir antes: el nombre del plugin y su `PLUGIN_CODE`
      (único en la suite; el fabricante, compartido).
- [ ] Fase 2 — Waveshaping básico + oversampling seleccionable (2x a 16x, etapas halfband de la librería), filtro DC,
      comparación con y sin oversampling en el analizador para escuchar y ver el aliasing.
- [ ] Fase 3 — Modos de saturación con carácter: cinta (suave, compresión, realce de graves),
      bulbo (asimétrico, armónicos pares), transistor (más duro, impares), clip duro, rectificador y wavefold.
      Cada modo con su propia curva y comportamiento según el nivel de entrada.
- [ ] Fase 4 — Tono: filtros pre y post saturación (HP/LP), tilt EQ y control de "emphasis"
      para decidir qué frecuencias se saturan más.
- [ ] Fase 5 — Mix dry/wet con compensación de fase/latencia, compensación automática de volumen
      (auto gain) y modo delta (escuchar solo lo que agrega la saturación).
- [ ] Fase 6 — Multibanda: crossovers Linkwitz-Riley (hasta 4 bandas), modo y drive por banda,
      solo/mute por banda, suma plana cuando no hay saturación.
- [ ] Fase 7 — Dinámica y modulación: envelope follower y LFO que modulan el drive u otros parámetros.
- [ ] Fase 8 — GUI profesional: curva de transferencia en vivo, analizador de espectro antes/después,
      visualización de bandas, escalable.
- [ ] Fase 9 — Presets con notas educativas (batería, bajo, voz, bus, master, efectos creativos).
- [ ] Fase 10 — Pulido: optimización de CPU, pluginval estricto, comparación gain-matched contra referencias.

## Estado actual
Fase 2 IMPLEMENTADA (2026-10-10), pendiente de que el usuario la pruebe en FL Studio.
- Cadena (`dsp/Engine.h`): entrada → × input (es el drive) → [medidor IN] → ↑N → `fastTanh` → ↓N → filtro DC (5 Hz) →
  × output → fundido de bypass con la señal original retrasada la latencia → × fundido de factor → [medidor OUT].
  tanh es simétrica (solo armónicos impares) y limita a ±1. No hay parámetro Drive aparte: Input hace de drive, como
  estaba previsto en la Fase 1.
- Librería (nuevo): `Oversampler.h` (un canal; cascada de 0–4 etapas halfband de `Halfband.h`, todas diseñadas en
  `prepare`, muestra a muestra, sin buffers por bloque; cada etapa subida+bajada retrasa 2H − 1 muestras de su
  frecuencia alta y la suma se completa hasta un entero de muestras del host con un retardo puro en la frecuencia más
  alta) y `DcBlocker.h` (y = x − x₁ + R·y₁, R = e^(−2π·fc/sr)). Latencias medidas (×2/×4/×8/×16): 44.1 kHz 69/75/77/78,
  48 kHz 39/44/46/47, 88.2 kHz 13/17/19/20, 96 kHz 11/15/17/18. Aliasing audible de tanh(4·seno de 5 kHz) a 48 kHz:
  −25 dB sin oversampling, −58 dB ×2, −118 dB ×4, en el ruido del float desde ×8.
- Parámetro nuevo `oversampling` (choice Off/2x/4x/8x/16x, def. 4x, versionHint 2). El índice = número de etapas: no
  cambiar el orden. Cambiar el factor: el motor baja la salida a cero en 10 ms (`factorFade`), cambia las etapas
  (borrando su historia) y la latencia, y vuelve a subir. El retardo del camino original (256 muestras fijas, máximo
  real ~140 a frecuencias bajas) conserva su historia. El procesador publica la latencia en un atómico y un
  `juce::Timer` (20 Hz, hilo de mensajes) llama a `setLatencySamples` cuando cambia; en `prepareToPlay` se informa
  directamente.
- GUI: selector OVERSAMPLING en la cabecera, junto a BYPASS. La comparación con y sin oversampling se hace con un
  analizador de FL (Wave Candy / Fruity Parametric EQ 2) después de Riptide; el analizador propio llega en la Fase 8.
- Tests: `DeepwaterDspTests` (+3: latencia entera exacta del oversampler a 4 sample rates × 5 factores, aliasing que baja
  con el factor medido con FFT, filtro DC) y `RiptideTests` reescritos (bypass = entrada retrasada bit a bit en todos
  los factores/bloques/mono-estéreo, procesado alineado con el original, saturación sin armónicos pares, DC eliminada,
  conmutar bypass y factor sin saltos, latencia publicada tras el cambio, independencia del tamaño de bloque).
- Release con 0 warnings; ctest 4/4; pluginval strictness 10 ampliado: SUCCESS (compilado en `out/pv` porque FL tenía
  abierto el VST3 de `build/`). Undertow no cambia (solo se agregaron headers a la librería).

Fase 1 COMPLETADA (2026-10-10). Probada por el usuario en FL Studio: funciona.
- Nombre decidido con el usuario: Riptide (`Rpt1`). `COMPANY_NAME "Deepwater"` en Riptide y también en Undertow (antes
  "Undertow"): FL agrupa la suite bajo un fabricante. Los códigos no cambian, así que los proyectos guardados siguen
  encontrando a Undertow (quizá haga falta volver a escanear en FL para ver el nombre nuevo).
- `dsp/Engine.h` (sin JUCE): entrada → × input gain → [medidor IN] → (saturación, Fase 2) → × output gain → fundido
  de bypass con la señal original → [medidor OUT]. Ganancias con `LinearRamp` de 20 ms (llega EXACTAMENTE al objetivo:
  0 dB = bit a bit, también después de mover las perillas); bypass = `EffectSwitch` de 20 ms; fuera del fundido se
  copia la señal tal cual. Cuando haya latencia (Fase 2) la señal original tiene que retrasarse lo mismo antes del
  fundido.
- Librería (nuevo): `Decibels.h` (`dbToGain`, `gainToDb`, `silenceDb` = −100), `LevelMeter.h` (`LevelMeter`: RMS
  exponencial de 300 ms en double + pico máximo desde la última lectura; `PeakDisplay`: balística de la GUI, caída
  20 dB/s, marca 1.5 s, clip latcheado si pasa de 1.0) y `LinearRamp` en `Smoothing.h`. No se usó `SmoothedParameter`
  por su bug conocido (nunca llega al objetivo, ver el `CLAUDE.md` de Undertow); arreglarlo cambiaría el sonido de
  Undertow.
- Parámetros (versionHint 1; IDs en `Parameters.h`): `inputGain`, `outputGain` (−24..+24 dB, paso 0.1, def. 0) y
  `bypass` (devuelto por `getBypassParameter()`: el bypass de FL y el botón de la GUI son el mismo parámetro; JUCE
  llama siempre a `processBlock` y el motor hace el fundido). Layouts: mono → mono y estéreo → estéreo. Latencia 0.
- Medidores: el audio publica por canal el pico (máximo atómico con compare_exchange; la GUI lo recoge con
  exchange(0), no se pierde ningún pico aunque el bloque sea de 4096) y el RMS. La GUI (30 fps, con el tiempo real
  entre cuadros) aplica `PeakDisplay`. IN mide después del input gain (el nivel que llegará al saturador).
- GUI 520 × 340: medidor IN | perillas Input y Output (doble clic = 0 dB) | medidor OUT; BYPASS en la cabecera.
  Escala −60..+6 dB, barra tenue = pico, sólida = RMS, línea = pico sostenido, LED rojo = clip (clic = apagar).
- Tests: `DeepwaterDspTests` (+4: LinearRamp, Decibels, LevelMeter, PeakDisplay) y `RiptideTests` (unidad bit a bit
  en mono/estéreo con bloques 1–4096 y tras mover las perillas, rampa exacta de 20 ms y sin saltos, bypass quieto bit a
  bit y conmutado sin escalones con DC, independencia del tamaño de bloque con automatización, medidores).
- Release con 0 warnings; ctest 4/4; null test de Undertow tras los cambios en la librería: idéntico bit a bit;
  pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096, `--repeat 3 --randomise`): SUCCESS. Revisado con
  captura del Standalone.

Siguiente: que el usuario pruebe la Fase 2 en FL; después, Fase 3 (modos de saturación).
