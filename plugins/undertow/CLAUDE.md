# CLAUDE.md — Undertow, sintetizador wavetable

Plugin 1 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del synth.

## Objetivo del proyecto
Construir un sintetizador wavetable polifónico de calidad profesional (referencia de nivel: Serum 2 y Vital),
con nombre, diseño y código propios. Se usa principalmente en FL Studio sobre Windows.

## Aprendizaje de diseño sonoro (igual de importante que el código)
Además de programar, quiero aprender a DISEÑAR SONIDOS. Al final del proyecto debo poder crear
mis propios sonidos desde cero, no solo usar presets. Por eso, al terminar cada fase:

1. **Qué hace cada control al sonido.** Por cada parámetro nuevo, explicar en lenguaje musical
   (no solo técnico) qué se escucha al moverlo: brillo, cuerpo, ataque, ancho, movimiento, agresividad.
   Relacionarlo con lo que se ve en un analizador de espectro y en un osciloscopio.
2. **Ejercicio de escucha guiado.** Pasos concretos para hacer en FL Studio: qué nota tocar, qué perilla
   mover, de qué valor a qué valor, qué debo notar. Si ayuda, sugerir un proyecto de FL sencillo
   (un patrón en el Piano Roll) para escuchar el cambio en contexto.
3. **Receta de sonido.** Con SOLO lo que existe hasta esa fase, una receta paso a paso para crear
   1 o 2 sonidos reales (ej. un sub bass, un pluck, un pad), explicando por qué cada ajuste está ahí.
4. **Reto sin receta.** Describir un sonido objetivo (o una referencia de un género) y dejar que yo
   lo intente primero; después darme pistas, no la solución completa de golpe.
5. **Vocabulario.** Los términos nuevos de la fase (ej. "armónicos", "detune", "resonancia", "transitorio")
   con una definición corta y un ejemplo de dónde se escucha en música real.

Mantener el archivo `APRENDIZAJE.md` de esta carpeta (`plugins/undertow/`), actualizado al final de cada fase, con:
conceptos aprendidos, recetas de sonidos, vocabulario y los retos completados. Es mi cuaderno de
diseño sonoro; debe servirme como referencia aunque no esté abierto el código.

Los presets de fábrica (Fase 10) deben incluir notas que expliquen cómo se construyó cada sonido,
para que funcionen también como material de estudio.

## Comandos
Desde la raíz del repositorio (ver también los comandos generales en el `CLAUDE.md` raíz):
```
cmake --build build --config Release --target UndertowSynth_VST3        # solo el plugin
build/plugins/undertow/Release/UndertowTests.exe                         # tests de DSP y voces (--fase7/8/9/12/13/15/16, --verbose)
build/plugins/undertow/Release/UndertowTests.exe --cpu                   # tabla de CPU (Fase 11)
build/plugins/undertow/UndertowPresetTests_artefacts/Release/UndertowPresetTests.exe   # presets, huellas, memoria
UndertowPresetTests --render plugins/undertow/tests/reference/migration-test.mid <carpeta>   # WAV para null tests
python scripts/null_test.py <carpeta_referencia> <carpeta_nueva>
```
- VST3: `build/plugins/undertow/UndertowSynth_artefacts/Release/VST3/Undertow Synth.vst3`.
- Standalone: `build/plugins/undertow/UndertowSynth_artefacts/Release/Standalone/Undertow Synth.exe`.

## Estructura
```
plugins/undertow/
  CMakeLists.txt           # plugin, presets de fábrica (BinaryData) y tests; enlaza deepwater::dsp
  Source/
    PluginProcessor.h/.cpp   # parámetros, estado, processBlock
    PluginEditor.h/.cpp      # GUI
    Parameters.h             # IDs de parámetros (NO cambiarlos: los proyectos guardados dependen de ellos)
    dsp/                     # osciladores, filtro de voz, envolventes, LFO, warp, efectos (lo compartido: libs/dsp)
    synth/                   # voces, polifonía, matriz de modulación, banco de wavetables, cadena de efectos
    gui/                     # componentes visuales propios
    presets/                 # formato propio (categorías, .uwpreset, BinaryData); el sistema está en libs/presets
  Presets/Factory/         # .uwpreset de fábrica (compilados dentro del plugin)
  tests/                   # DspTests.cpp (sin JUCE), PresetTests.cpp (con JUCE), reference/
  docs/                    # capturas para el README
```

## Hoja de ruta
- [x] Fase 1 — Proyecto base: CMake + JUCE, compila VST3 y Standalone, carga en FL, onda senoidal con MIDI.
- [x] Fase 2 — Polifonía y envolvente ADSR (sin clics, voice stealing correcto).
- [x] Fase 3 — Oscilador wavetable sin aliasing (mipmaps por octava, interpolación, morphing de posición).
- [x] Fase 4 — Filtros ZDF/TPT: LP/HP/BP 12 y 24 dB, resonancia, drive, key tracking.
- [x] Fase 5 — Modulación: 2 envolventes extra, LFOs sincronizados al tempo, matriz de modulación.
- [x] Fase 6 — Segundo oscilador, sub, ruido, unison (hasta 16 voces) con detune y ancho estéreo.
- [x] Fase 7 — FM y warp de osciladores: FM/PM entre osciladores (OSC B → OSC A, ruido → OSC, sub → OSC),
      ring mod, y modos de warp (bend, sync, PWM, mirror, quantize/bitcrush). Todo modulable desde la matriz
      y con control de aliasing (oversampling o técnicas band-limited donde haga falta).
- [x] Fase 8 — Efectos internos: distorsión (con oversampling), chorus, delay, reverb.
- [x] Fase 9 — GUI profesional: visualización del wavetable (mostrando el warp aplicado), arrastrar para modular, escalable.
- [x] Fase 10 — Presets: guardado/carga, navegador, librería inicial de sonidos.
- [x] Fase 11 — Pulido: optimización de CPU (SIMD), pluginval estricto, comparación contra referencias.

Ampliación: cuerda pulsada (Karplus-Strong, Jaffe & Smith; desde la literatura publicada, sin código ajeno). Elegida
como fuente nueva por voz (como el sub y el ruido), no como tipo de oscilador: corre a 1× (no depende del camino de
warp/FM), un solo juego de parámetros, y apagada no se calcula.
- [x] Fase 12 — Núcleo: retardo fraccionario Lagrange en libs/dsp, cuerda con ruido blanco, Decay (T60), Damping,
      note-off con "palma", afinación < 1 cent (MIDI 12–96, 4 sample rates), pestaña "Cuerda".
- [x] Fase 13 — Excitación: Pick Position (peine en la excitación), ruido filtrado (tono de la púa) e impulso;
      destinos de la matriz (al final): String Level, Pitch, Decay, Damping, Pick Position, Pick Tone.
- [x] Fase 14 — Presets de cuerda (guitarra, koto, arpa, plucks) con notas, y APRENDIZAJE.md.
- [x] Fase 15 (opcional) — Stiffness / inharmonicidad (all-pass de dispersión en el lazo).
- [x] Fase 16 (opcional) — La cuerda como resonador: excitación = las fuentes de la voz (opción al final de la lista).

Ampliación: fase inicial de los osciladores (como PHASE / RAND / Phase Memory de Serum 2, según su manual; sin código
ajeno). Compatible: por defecto el arranque de siempre.
- [x] Fase 17 — Phase (0–360°), Rand y modo Reset/Free por oscilador (A y B), en la pestaña "Warp y FM".

## Estado actual
Fase 1 COMPLETADA (2026-09-24): CMake + JUCE 9.0.2, VST3 + Standalone, seno monofónico (prioridad a la última
nota) con MIDI sample-accurate y rampa anti-clic de 5 ms. Release con 0 warnings (/W4, MSVC 19.44, VS 2022 17.14),
pluginval strictness 10: SUCCESS (`tools/pluginval/pluginval.exe`). Probado por el usuario en FL Studio 2025: suena bien.
Nota FL: la ruta de búsqueda de los artefactos debe estar marcada como tipo VST3 en Manage plugins.

Fase 2 COMPLETADA (2026-09-24). Probada por el usuario en FL Studio: suena bien.
- `dsp/AdsrEnvelope.h`: ADSR exponencial (tiempos exactos gracias al overshoot), attack desde el nivel actual,
  sustain suavizado y `quickRelease` lineal de 5 ms para el robo de voces.
- `synth/Voice.h`, `synth/VoiceManager.h/.cpp`: pool de 32 ranuras y polifonía de 1 a 16 (parámetro).
  El robo no produce clics: fade de 5 ms mientras la nota nueva suena en otra ranura.
  Prioridad para robar: nota soltada → sostenida por el pedal → pulsada más antigua.
  Repetir la misma nota reutiliza su voz. Pedal de sustain CC64, CC120 (fade) y CC123 (release).
- Parámetros APVTS (IDs en `Parameters.h`, NO cambiarlos): attack, decay, sustain, release, voices, velocity, master.
  El estado se guarda y carga como XML.
- GUI funcional: 7 perillas y contador de voces sonando.
- Tests: `tests/DspTests.cpp` (ejecutable `UndertowTests`, sin JUCE): todos pasan.
- Release con 0 warnings; pluginval strictness 10: SUCCESS.
- Tests de afinación: las 88 teclas a 4 sample rates, peor desviación 0.0003 cents.

Fase 3 COMPLETADA (2026-09-24). Probada por el usuario en FL Studio.
- `dsp/Fft.h`: FFT radix-2 propia (solo fuera del hilo de audio). `dsp/Pitch.h`: midiNoteToHz (SineOscillator eliminado).
- `dsp/Wavetable.h/.cpp`: tabla inmutable construida desde espectros (`fromSpectra`) o ciclos muestreados
  (`fromWaveforms`). 11 mipmaps por octava (1024 → 1 armónicos); tamaño por nivel = max(2048, 16·armónicos)
  → 16384/8192/4096/2048…; normalización de pico por frame (escala del nivel 0 para todos los niveles).
- `dsp/WavetableOscillator.h`: interpolación cúbica Catmull-Rom, morph lineal entre frames, Position suavizada
  (10 ms), fundido de 5 ms al cambiar de tabla, `reset()` al empezar nota desde silencio. Límite de armónicos:
  sr − 20 kHz a 44.1/48 kHz (el reflejo cae > 20 kHz), Nyquist a 88.2/96 kHz.
- `synth/WavetableBank`: 5 tablas de fábrica generadas por código (Basic Shapes 4 frames; Pulse Width, Harmonic
  Build, Hard Sync, Vowels de 64 frames). ~47 MB, ~0.4 s; compartido entre instancias (`SharedResourcePointer`).
  El orden de `names` es parte del estado guardado: solo añadir al final.
- Parámetros nuevos: oscAWavetable (choice), oscAPosition (0..1), versionHint 2. Por defecto = seno (sonido de Fase 2).
- GUI: grupo "Oscilador A" con selector, perilla Position y visor de la forma de onda (`gui/WavetableDisplay.h`).
- Tests: peor alias medido < 20 kHz: −87 dB (44.1/48 kHz) y −92 dB (88.2/96 kHz) con pulso 3 % y sync; sierra
  ingenua: −12 dB. Armónicos altos conservan su nivel (0.00 dB). Morph y cambio de tabla sin clics.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS.
- Límite conocido: el paso de mipmap es por octava; con pitch bend/glide (Fase 5) podría oírse un leve salto de
  brillo al cruzar un límite. RESUELTO en Fase 5 (fundido entre niveles vecinos).

Fase 4 COMPLETADA (2026-09-24). Probada por el usuario en FL Studio: suena bien.
- `dsp/Filter.h`: `SvfStage` (SVF TPT/ZDF de 2 polos: LP/BP/HP a la vez) y `Filter` (drive → etapa 1 → etapa 2).
  12 dB = 1 etapa (Q Butterworth 0.707); 24 dB = 2 etapas (Q 0.541 y 1.307). La resonancia multiplica el Q de la
  etapa resonante hasta ×17 (exponencial; 12 dB: Q 12, pico +15 dB tras compensar). Compensación LP/HP: −6 dB en la
  banda de paso a resonancia máxima (tipo ladder). Band-pass normalizado a pico 0 dB. Sin auto-oscilación.
  Todo suavizado 5 ms por muestra (cutoff en octavas); tipo y pendiente se cambian con fundido (la etapa 2 siempre
  procesa). Coeficientes (tan) solo se recalculan mientras cutoff/resonancia se mueven.
  Drive: tanh con hasta +30 dB, mezclado con la señal limpia en el primer 10 % (drive 0 = lineal exacto).
  `filterMagnitude()`: respuesta teórica (la usa la GUI y la verifican los tests).
- `keyTrackedCutoff()`: referencia C5 de FL (MIDI 60). Cada voz aplica su propio key tracking.
- Voz: oscilador → filtro → envolvente. Filtro on/off con fundido de 5 ms; apagado no se procesa (= sonido Fase 3).
- Parámetros nuevos (versionHint 3): filter1On (off), filter1Type (LP/HP/BP), filter1Slope (12/24, def. 24),
  filter1Cutoff (20 Hz–20 kHz log, def. 2 kHz), filter1Resonance, filter1Drive, filter1KeyTrack (0 %).
  El orden de las opciones de Type/Slope se guarda: no cambiarlo.
- GUI: grupo "Filtro" (On, tipo, pendiente, 4 perillas) y visor de la curva (`gui/FilterResponseDisplay.h`).
- Tests: medido vs teórico ≤ 0.0001 dB (3 tipos × 2 pendientes × 3 resonancias × 4 sample rates); −3 dB exactos en
  el cutoff hasta 15 kHz; 3 octavas: −36.5 / −73 dB; estable con cutoff aleatorio por muestra y resonancia máxima;
  cambios de tipo/pendiente/on-off sin clics. CPU: 16 voces sierra + filtro 24 dB en movimiento ≈ 5.5 % de un núcleo.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS.
- Límite conocido: el drive no tiene oversampling. Alias medido (sierra, 48 kHz, drive 100 %): −55 dB a 110 Hz,
  −43 dB a 440 Hz, −27 dB a 1760 Hz. Solución prevista: oversampling en Fase 7/8.

Fase 5 COMPLETADA (2026-09-25). Probada por el usuario en FL Studio: suena bien.
- `dsp/Lfo.h`: LFO bipolar (Sine, Triangle, Saw Up/Down, Square, Sample & Hold), fase en double + contador de ciclos.
  S&H = hash de (semilla, ciclo): sin estado, repetible y el mismo en todas las voces en modo Free. One Shot = 1 ciclo.
- `synth/Modulation.h`: enums y nombres de fuentes (None, Env 1 (Amp), Env 2, Env 3, LFO 1, LFO 2, Velocity, Key,
  Mod Wheel, Aftertouch) y destinos (None, Osc A Position, Osc A Pitch, Filter Cutoff, Filter Resonance, Filter
  Drive, Volume); 17 divisiones de tempo (4/4). TODOS los órdenes se guardan: solo añadir al final.
  Escalas del amount (100 %): Position/Resonance/Drive = toda la perilla; Pitch ±24 st; Cutoff 10 octavas;
  Volume ×(1 + m) limitado a 0..2. Key = (nota − 60) / 60 → Key→Cutoff 50 % = key tracking 100 % exacto.
- Voz: fuentes y matriz por muestra. Env 2/3 por voz (reset al empezar desde silencio). LFO por voz: Retrigger/One
  Shot reinician en cada nota; Free se re-alinea con el reloj común del VoiceManager al inicio de cada render
  (con Sync + host reproduciendo, la fase sale de ppqPosition). Suavizado: LFO 1 ms, velocity/mod wheel/aftertouch
  10 ms, amount 5 ms; envolventes SIN suavizar (Env→Cutoff llega en 1 ms). Cambiar la fuente/destino de una ruta:
  la vieja se apaga en 5 ms y la nueva entra desde 0 (`activeRoutes`). Primera muestra de una nota: valores
  directos (snap). Sin rutas activas la matriz no se calcula.
- La modulación se suma DESPUÉS del suavizado de las perillas: `Filter::setModulation` (cutoff en octavas,
  resonancia/drive; coeficientes recalculados por grupos solo si cambian) y
  `WavetableOscillator::setPitchModulation/setPositionModulation`. Refactor del filtro: se suavizan resonancia/drive/
  pendiente en espacio de parámetro y los k se derivan (mismo resultado medido que en Fase 4).
- Resuelto el límite de la Fase 3: fundido entre mipmaps en los últimos 1/6 de octava antes de cada límite
  (mezcla hacia el nivel más pobre: sin alias nuevo).
- Parámetros nuevos (versionHint 4; IDs en `Parameters.h`): env2/env3 Attack/Decay/Sustain/Release (def. 5 ms,
  500 ms, 0 %, 150 ms); lfo1/lfo2 Shape, Mode (def. Retrigger), Sync (def. on), Rate (0.02–40 Hz log, def. 2 Hz),
  Division (def. 1/4); mod1..mod8 Source, Destination, Amount (−100..100 %). Todo vacío por defecto = sonido Fase 4.
  MIDI: CC 1 → Mod Wheel, channel pressure → Aftertouch. Tempo/posición de `getPlayHead()` (120 BPM sin host).
- GUI: dos pestañas ("Sonido" = Fase 4; "Modulación" = LFO 1 | LFO 2 con visor y punto de fase, Env 2 | Env 3,
  matriz 2×4). Tamaño igual al de la Fase 4 (870×602): la versión en una sola ventana de 1470 px no cabía en la
  pantalla del usuario con escalado de Windows. Literales con tildes: usar `juce::String::fromUTF8` con escapes.
- Tests (14 nuevos): formas y velocidad exacta del LFO, One Shot, S&H, tempo sync, fase desde ppq, Free vs
  Retrigger, Key→Cutoff = key tracking, Mod Wheel→Pitch (+12 st y +1 st, error 0.0000 cents), vibrato ±100 cents,
  Env 2→Cutoff (pico 6400 Hz en 1.0 ms), modulación con saltos sin clics, continuidad del fundido de mipmaps.
  CPU: 16 voces + filtro 24 dB sin rutas ≈ 7–8 % de un núcleo; con 8 rutas activas ≈ 12–14 %.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS (compilado en `out/pv` porque FL tenía
  el VST3 de `build/` abierto y bloqueado).
- Pendiente de ideas para más adelante: fade-in/delay del LFO, modular el amount con otra fuente (aux), pitch bend.

Fase 6 COMPLETADA (2026-09-25). Probada por el usuario en FL Studio: funciona bien.
- `dsp/WavetableOscillator.h` con unison: hasta 16 copias con fase propia que comparten tabla, mipmap y frames
  (una copia extra = 2–4 interpolaciones). Detune cuadrático (100 % = ±100 cents; 25 % = ±6; 50 % = ±25), copias a
  distancias iguales en cents (razones en progresión geométrica: 2 exp2 por recálculo). Mipmap elegido para la copia
  MÁS AGUDA (sin alias). Fases al azar por nota con N > 1 (fase 0 con N = 1). Nivel 1/√N (RMS medido ±0.03 dB de
  1 a 16 copias). Paneo equal power ×√2 (centro L = R = 1 → el sonido por defecto es idéntico al de la Fase 5);
  parejas simétricas abiertas alternando lado. Ganancias por copia suavizadas 5 ms (cambiar N/ancho/paneo sin clics);
  detune suavizado 5 ms. `setPitchModulation` renombrado a `setPitchOffset` (afinación + modulación).
  Tope de frecuencia 0.45·sr: con Octave +4 y pitch al máximo la fase se salía de la tabla (crash en el test de
  automatización de pluginval; el bug ya existía en la Fase 5 con nota 127 + 24 st). Test de regresión incluido.
- `dsp/NoiseGenerator.h`: xorshift32 + filtro de 1 polo TPT. Color 0 → LP 100 Hz, 0.5 → blanco, 1 → HP 10 kHz.
- `dsp/Filter.h`: `processStereo` (2 juegos de estado, coeficientes compartidos) y `copyLeftStateToRight`.
- `synth/SourceSettings.h`: ajustes de Osc A/B, sub (usa los 4 frames de *Basic Shapes*, mono) y ruido (mono).
  `SubShape` y su orden se guardan: solo añadir al final.
- Voz estéreo: fuentes → filtro → amp. Si ningún oscilador audible es estéreo, la voz y el filtro van en mono
  (al pasar a estéreo se copia el estado L → R). `SourceLevel`: fundido on/off y nivel 5 ms + modulación.
  Afinación suavizada 5 ms. `VoiceManager::render (left, right, n)`; `render (out, n)` = mono (L + R) / 2.
- Destinos nuevos (al final): Osc B Position, Osc B Pitch, Osc A/B Level, Sub Level, Noise Level, Osc A/B Detune,
  Global Pitch (A, B y sub). Pan y Width no son destinos (recalcular 16 cos/sin por muestra).
- Parámetros nuevos (versionHint 5; IDs en `Parameters.h`, `params::oscillators[]`): oscXOn, oscBWavetable,
  oscBPosition, oscXOctave (−4..4), oscXSemi (−12..12), oscXFine (±100 ct), oscXLevel (def. 100 %), oscXPan,
  oscXUnison (1..16), oscXDetune (def. 25 %), oscXWidth (def. 80 %); subOn, subShape, subOctave (−3..0, def. −1),
  subLevel (def. 75 %); noiseOn, noiseLevel (def. 50 %), noiseColor (def. 50 %). Osc A on, B/sub/ruido off:
  un proyecto de la Fase 5 suena igual. Osc A conserva oscAWavetable/oscAPosition (versionHint 2).
- GUI: 3 pestañas del mismo tamaño que antes (870×602): "Osciladores" (A, B con visor y 9 perillas; Sub | Ruido),
  "Filtro y Amp" (filtro; Env 1 | Voz) y "Modulación".
- Tests (11 nuevos): posición exacta de cada copia (error 0.000000 cents), picos del unison en el espectro,
  volumen constante, L = R = salida mono por defecto, correlación L/R 0.39 con ancho 100 %, potencia constante con
  el paneo, cambios sin clics (salto máx. 0.009), afinación de Osc B y sub (0.00001 cents), formas del sub (3.º
  armónico de la cuadrada −9.54 dB), color del ruido (blanco −0.2 dB, oscuro −27.6, brillante +24.2), destinos
  nuevos, tono extremo seguro, alias con unison (−92.8 dB). CPU (48 kHz): 8 notas sin unison ≈ 5 %; supersaw 8 notas
  × 7 copias ≈ 9 %; peor caso (16 notas, A y B con 16 copias, sub, ruido, 4 rutas) ≈ 65–80 % de un núcleo.
- Release con 0 warnings (build limpio); pluginval strictness 10: SUCCESS.
- Límites conocidos: el unison al máximo es caro (optimizar con SIMD en Fase 11). La suma mono de un unison abierto
  pierde hasta −3 dB en las copias de los lados (ley de paneo equal power; explicado en APRENDIZAJE.md).

Fase 7 COMPLETADA (2026-09-25). Probada por el usuario en FL Studio: funciona.
- `dsp/Warp.h`: `WarpMode` (orden guardado: None, Sync, Bend +, Bend -, PWM, Mirror, Quantize, Bitcrush) y
  `Warp::make` (amount → Sync ratio 2^(4a) = 1..16; Bend pendiente 2^(3a) = 1..8 con curva hiperbólica
  s·x/((s−1)x+1) en mitades simétricas; PWM ancho 1 − a·15/16; Quantize 2^(1+7(1−a)) escalones leyendo el centro del
  tramo; Bitcrush 2^(7(1−a)) niveles/unidad; Quantize/Bitcrush entran en el primer 10 %). Amount 0 = onda exacta.
  `warpedValue()` la comparten el oscilador y la GUI.
- `WavetableOscillator`: camino `renderWarped` (sin warp ni FM se usa el de la Fase 6, intacto). FM = PM
  (`setPhaseModulation`, en ciclos); primero la FM y después el warp. Mipmap para la lectura MÁS RÁPIDA (incluye la FM;
  pico con release de 20 ms para no conmutar niveles al ritmo del modulador; se recalcula si cambia > 0.1 %).
  polyBLEP + polyBLAMP de 4 muestras (B-spline cúbica; salida retrasada 2 muestras): Sync (salto + quiebre), PWM y
  Mirror (quiebres), Quantize (saltos), Bitcrush (un salto por nivel, cruce estimado con Hermite + Newton acotado).
  `setSampleRate (rate, harmonicLimit)`, `defaultHarmonicLimit()`.
- `dsp/HalfbandDecimator.h`: FIR halfband con ventana de Kaiser, 100 dB (139 taps a 44.1 kHz, 79 a 48 kHz; retardo
  34.5 / 19.5 muestras). Banda de paso ±0.0001 dB hasta 20 kHz; rechazo medido −100 dB.
- Voz: `SourcePathConfig` = modos de warp y FM/RM de A y B. Con alguno ≠ None/Off, osciladores y sub corren a 2×
  (también a 88.2/96 kHz) con límite de armónicos min(normal, 0.37·(2·fs − 20 kHz)) y decimador L/R; el ruido sigue a
  1× (idéntico), se suma después del decimador y como modulador se repite en las dos muestras internas.
  `FmMode` (orden guardado): Off, FM: otro osc / Sub / Noise, RM: otro osc / Sub / Noise. La moduladora es la señal
  cruda (antes de Level y aunque esté apagada). Si A escucha a B, B se calcula primero. FM: 2 ciclos·a² (β máx 4π);
  RM: (1 − a) + a·m. Cambiar un modo = fundido de 2.5 ms (antes del decimador) → espera a vaciar el decimador →
  aplicar → 2.5 ms de subida. Warp/FM Amount suavizados 5 ms + modulación.
- Destinos nuevos (al final): Osc A Warp, Osc B Warp, Osc A FM/RM, Osc B FM/RM (100 % = toda la perilla).
- Parámetros nuevos (versionHint 6; IDs en `Parameters.h`, `params::oscillatorWarps[]`): oscXWarpMode, oscXWarpAmount,
  oscXFmMode, oscXFmAmount. Por defecto None / 0 / Off / 0: un proyecto de la Fase 6 suena igual.
- GUI: pestaña "Warp y FM" (A | B) con visor `gui/WarpDisplay.h` (original en gris, deformada en naranja), selector y
  Amount para Warp y para FM/RM. Mismo tamaño de ventana (870×602).
- Tests (11 nuevos; `UndertowTests --fase7` corre solo estos y `--verbose` imprime el detalle por nota): identidad con
  amount 0 y velocidad acotada de los warps; decimador (±0.0001 dB, −100 dB); FM = Bessel (0.11 dB; con β = 2.405 la
  portadora a −75 dB); RM (−6.02 dB exactos, portadora −128 dB); modo con amount 0 = camino normal (0.0001 dB, ruido
  0.000 dB); alias C4–C7 a 4 sample rates: Sync −78, Bend + −76, Bend − −62, PWM −81, Mirror −92, Quantize −82,
  Bitcrush −53, FM −72 dB (sync ingenuo: −27 dB); cambiar de modo sin clics; destinos por la matriz; extremos sin NaN.
  CPU (48 kHz): 8 notas con Sync ≈ 9 %; FM de 2 osciladores ≈ 14 %; Sync con 7 copias ≈ 20 %; peor caso
  (16 notas, 2 × 16 copias, Mirror + FM/RM) ≈ 300 % de un núcleo.
- Release con 0 warnings (build limpio); pluginval strictness 10 (con tests de GUI): SUCCESS.
- Límites conocidos: Bend − al 100 % (−62 dB: su curva frena y acelera de golpe); Bitcrush de una sierra (−53 dB: pulsos
  más cortos que una muestra cerca del salto); C8 con amounts extremos (−53 a −79 dB). El camino con warp/FM cuesta 2–4×
  (SIMD en Fase 11). El drive del filtro sigue sin oversampling (Fase 8).

Fase 8 COMPLETADA (2026-09-25). Probada por el usuario en FL Studio: suena bien.
- Efectos GLOBALES (sobre la suma de voces, antes del master), orden fijo: distorsión → chorus → delay → reverb.
  `synth/Effects.h`: `EffectsSettings` y `EffectsChain` (sin JUCE). On/off con fundido de 10 ms; apagado y con el fundido
  terminado no se procesa; al volver a encender se vacía el efecto (`reset (true)`: la señal que entra a las líneas sube
  en 10 ms, si no la primera repetición empieza con un corte = clic). Todo apagado = señal intacta bit a bit.
- `dsp/HalfbandDecimator.h`: `HalfbandDesign` (coeficientes compartidos) + `HalfbandInterpolator` (polifásico, ±0.0001 dB,
  imagen −98 a −117 dB). `dsp/DelayLine.h`: `DelayLine` (potencia de 2, lectura entera, lineal y Catmull-Rom),
  `SmoothedParameter`, `EffectSwitch`.
- `dsp/Distortion.h`: oversampling ×4 (2 interpoladores + 2 decimadores halfband; retardo 1.7 ms a 44.1, 0.9 ms a 48 kHz) +
  ADAA de 1.er orden (en double, sobre la señal ANTES del drive: F(g·u)/g). `DistortionMode` (orden guardado): Soft Clip,
  Hard Clip, Tube (tanh con bias 0.3: armónicos pares), Fold (sin). Drive 0..+36 dB (Fold 0..+20 dB: más no cabe en
  ningún oversampling). Makeup = 0.25 / tanh(0.25·g) (una señal de −12 dBFS conserva el pico; volumen −0.7..+2.9 dB).
  Tone = LP 12 dB 400 Hz–40 kHz a 4×; DC blocker 5 Hz; Mix DENTRO del dominio 4× (la señal limpia llega con el mismo
  retardo). Cambio de modo = fundido de 10 ms calculando las dos curvas. OJO: `down1.process (down2.process (a),
  down2.process (b))` era un bug (orden de evaluación de argumentos no especificado en C++; MSVC evalúa de derecha a izq.).
- `dsp/Chorus.h`: una copia por canal, retardo 10 ms ± 7 ms (depth), LFO seno con el derecho invertido (180°), Catmull-Rom,
  feedback hasta 90 %. `dsp/StereoDelay.h`: hasta 4 s, tiempo redondeado a muestras enteras, cambio de tiempo = fundido de
  50 ms entre dos lecturas (estilo "digital", sin desafinar), Tone = LP 1 polo en el lazo (500 Hz–20 kHz; 100 % = sin
  filtro), feedback hasta 95 %, ping-pong mezclado con estéreo por un valor suavizado (cambiarlo no hace clic).
  `dsp/Reverb.h`: pre-delay (0–250 ms) → 4 all-pass por canal (difusión 0.65) → FDN de 8 líneas (29.7–73.3 ms × Size 0.4..1.6,
  matriz Householder, ganancia por línea = 10^(−3·L/(RT60·sr)), damping LP 1 polo 20 kHz → 1 kHz, oscilación ±0.3 ms con
  lectura lineal) → salida con dos filas de Hadamard (L/R decorrelados).
- Parámetros nuevos (versionHint 7; IDs en `Parameters.h`): fxDistOn/Mode/Drive/Tone/Mix; fxChorusOn/Rate (0.05–8 Hz)/Depth/
  Feedback/Mix; fxDelayOn/Sync/Time (1 ms–2 s)/Division/Feedback/PingPong/Tone/Mix; fxReverbOn/Size/Decay (0.2–30 s)/
  Damping/PreDelay/Mix. `delayDivisions` (14, orden guardado; por defecto 1/8 D). Todo apagado por defecto: un proyecto de
  la Fase 7 suena igual. `getTailLengthSeconds()` = release + cola de reverb y delay (hasta −60 dB). `hostBpm` atómico.
- GUI: pestaña "Efectos" (Distorsión | Chorus; Delay; Reverb). 5 pestañas de 96 px; mismo tamaño de ventana (870×602).
- Tests (12 nuevos; `UndertowTests --fase8`): interpolador; distorsión con Mix 0 transparente (±0.0003 dB); armónicos
  (simétricas: pares < −150 dB; Tube: 2.º −20 dB); alias hasta 1.2 kHz −67 a −92 dB, peor caso C8 a 0 dBFS −55 dB
  (ingenuo −17 dB); volumen compensado; cambios sin clics; chorus (retardo exacto, correlación L/R 0.00); ecos exactos
  (muestra y nivel), ping-pong y sync; cambios de tiempo sin clics; RT60 medido (banda < 1 kHz) −5 % del Decay a 4 sample
  rates; pre-delay; L/R de la cola decorrelados; estabilidad al máximo; cadena intacta apagada y sin clics al conmutar.
  CPU (48 kHz): distorsión ≈ 3 %, chorus 0.15 %, delay 0.04 %, reverb 0.6 %; los cuatro ≈ 4 % de un núcleo.
- Release con 0 warnings (build limpio en `out/pv`); pluginval strictness 10: SUCCESS (compilado en `out/pv` porque FL
  tenía abierto el VST3 de `build/`).
- Límites conocidos: el drive del filtro sigue sin oversampling (es por voz; la distorsión global sí lo tiene; revisar ADAA
  en Fase 11). Los efectos no son destinos de la matriz (es por voz). La distorsión añade ~1 ms de retardo (no se
  informa como latencia al host). Orden de efectos fijo.

Fase 9 COMPLETADA (2026-10-08). Probada por el usuario en FL Studio. El audio no cambia.
- Escalado: todo vive en `Content` (858 × 666 px de diseño, `DragAndDropContainer`) dibujado con
  `AffineTransform::scale (zoom)`. Ventana redimensionable con proporción fija, zoom 70–200 % (menú en la cabecera o
  esquina). Zoom inicial: propiedad `uiZoom` del estado (se guarda con el proyecto) → preferencia en
  `%APPDATA%\Undertow\Undertow Synth.settings` → el mayor ≤ 100 % que cabe en pantalla (en la del usuario, 85 %).
  OJO: `setResizeLimits` redimensiona la ventana: el zoom se calcula antes y `rememberZoom` evita guardar ese tamaño.
- `gui/Theme.h` (paleta y un color por fuente de modulación) y `gui/UndertowLookAndFeel.h` (perillas con arco de
  valor —desde el centro si el rango es bipolar—, grupos como paneles, interruptores, combos, etiquetas sin recuadro;
  `upperCase` propio porque `String::toUpperCase` deja la "ó" en minúscula en Windows).
- Arrastrar para modular: `gui/ModSourceBar.h` (9 fichas arrastrables + línea de mensajes), `gui/ModKnob.h` (Slider +
  DragAndDropTarget: anillo por ruta en el color de la fuente, punto blanco en vivo, arrastrar el anillo = amount con
  gesto de automatización, Shift fino, clic derecho = elegir/quitar rutas + menú del host, doble clic = valor por
  defecto), `gui/ModulationRouter.h` (escribe las rutas de la matriz con begin/end gesture; orden amount → destino →
  fuente para que el audio nunca vea una ruta a medias) y `gui/ModulationTargets.h` (sin JUCE: perilla ↔ destino,
  escala del anillo —Cutoff 10/9.97, Pitch sobre Semi—, fuentes bipolares = LFO y Key, `findSlotFor`: la misma ruta si
  ya existe → ruta vacía → a medio configurar → −1). Amount inicial al soltar: 25 %. Volume y Global Pitch no tienen
  perilla (solo desde la matriz). Las pestañas se abren solas al pasar por encima arrastrando una fuente.
- `gui/WavetableView.h` (reemplaza a WavetableDisplay y WarpDisplay): vista 3D (hasta 32 frames apilados + el frame que
  suena, relleno) o 2D (original gris + deformada naranja), con el warp aplicado (`dsp::warpedValue`) y la modulación en
  vivo; arrastrar = Position. Osciladores en 3D, Warp y FM en 2D.
- Procesador: `getModulationDisplay (destino)` = suma de la matriz en la nota más reciente (`VoiceManager::
  getNewestPlayingVoice`), 20 atómicos por bloque; nullopt sin notas.
- Tests (2 nuevos; `UndertowTests --fase9`): tabla de destinos (única, todos con perilla salvo None/Volume/Global
  Pitch, escalas), rangos del anillo uni/bipolares con recorte, reparto de rutas. Todos los tests pasan.
- Release con 0 warnings; pluginval strictness 10 (con tests de editor): SUCCESS. Revisado con capturas del Standalone
  (5 páginas, arrastre LFO 1 → Position, ajuste del anillo, menú de zoom).
- Pendiente para más adelante: modular el amount de una ruta con otra fuente, macros, ver los LFO/envolventes como
  fuentes arrastrables también desde sus paneles, accesibilidad con teclado del anillo.

Fase 10 COMPLETADA (2026-10-08). Probada por el usuario en FL Studio. El audio no cambia.
- `presets/PresetManager.h/.cpp` (JUCE, solo hilo de mensajes): formato `.uwpreset` = XML legible
  (`<UndertowPreset formatVersion name category author><Notes/><Parameters><PARAM id value/>`). Valores en UNIDADES
  REALES; las listas aceptan índice o nombre de la opción y los bool On/Off. Lo que falta = valor por defecto (los de
  fábrica solo escriben lo que cambia respecto a Init); ids desconocidos se ignoran con aviso; fuera de rango se limita.
  Los del usuario guardan TODOS los parámetros (listas por índice). Cargar = `setValueNotifyingHost` solo de los que
  cambian (el audio ya suaviza). Categorías (orden del navegador): Init, Bass, Lead, Pad, Pluck, Keys, FX, Otros.
- Fábrica: `Presets/Factory/<Categoría>/*.uwpreset` compilados en el plugin con `juce_add_binary_data`
  (`UndertowPresetData`, GLOB CONFIGURE_DEPENDS). 22 presets (Init + 7 Bass, 4 Lead, 4 Pad, 2 Pluck, 2 Keys, 2 FX):
  las recetas de las fases 2–9 + 6 nuevos. Notas en español; en el XML se parten a ~118 columnas y `reflowNotes` junta
  los párrafos (solo fábrica). Ninguno resuelve los retos pendientes. Nivel igualado con Master: RMS −17.5…−19.6 dBFS
  (Init −18.3); los Pluck se igualan por pico (−9…−11 dBFS).
- Usuario: `Documentos\Undertow\Presets` (recursivo). Guardar con el nombre de uno de fábrica está prohibido; mismo
  nombre = reemplazar ese archivo. Borrar = papelera de reciclaje.
- Proyecto de FL: propiedades del estado `presetName`, `presetFactory`, `presetModified` (como `uiZoom`; no son
  parámetros). "Modificado" = algún parámetro difiere > 1e-4 (normalizado) de la foto tomada al cargar. Proyecto sin
  esas propiedades (Fase 9) → "Sin nombre". No se usan los programas del host (en VST3 pueden llegar en el hilo de audio).
- GUI: ventana 858 × 704 (antes 666): fila 1 = título + `gui/PresetBar.h` (◀ ▶, nombre con categoría y `*`, Guardar,
  menú ··· con Init / Guardar como / abrir carpeta / releer) + zoom; fila 2 = pestañas + voces. `gui/PresetBrowser.h`
  (encima de las páginas: categorías con contador, búsqueda en nombre/categoría/notas, lista —clic o flechas = cargar,
  doble clic/Enter = cargar y cerrar, Esc—, notas, borrar con doble confirmación) y `gui/PresetSaveDialog.h` (nombre,
  categoría, autor —se recuerda en la configuración—, notas; "Basado en «X»" si se parte de uno de fábrica). Sin
  ventanas modales. Cambiar de pestaña o arrastrar una fuente de modulación cierra los paneles.
- Tests: `UndertowPresetTests` (app de consola con JUCE que compila el procesador; en ctest). Valida cada preset de
  fábrica (ids, opciones, rangos, nombres únicos, notas), carga/defectos/Init, `*`, guardar (ida y vuelta exacta,
  reemplazar, nombres prohibidos, borrar), archivos rotos, estado del proyecto, siguiente/anterior, render de los 22
  presets (sin NaN, pico < −0.5 dBFS, RMS en −22…−14) a 48 kHz y sin NaN a 44.1/96 kHz, y cambiar de preset cada 100 ms
  con un acorde sonando (pico −0.3 dBFS). `--verbose` = tabla de niveles; `--user <carpeta>` mide también esos
  presets; `--notes` imprime las notas como las muestra el navegador. Usa una carpeta temporal (nunca Documentos).
- Release con 0 warnings (build limpio en `out/pv`); ctest 2/2; pluginval strictness 10 (con tests de editor): SUCCESS.
  Revisado con capturas del Standalone (barra, navegador, notas, guardar, borrar, menú, páginas).
- Pendiente para más adelante: favoritos/etiquetas, arrastrar un .uwpreset a la ventana, preset aleatorio, A/B.

Fase 11 COMPLETADA (2026-10-09). Probada por el usuario en FL Studio. El sonido no cambia (medido).
- `dsp/Simd.h`: `simd::Float4` (SSE2, 4 floats; versión escalar con la misma interfaz si no hay SSE2), `transpose`,
  `readPositions` (fases double → índice + fracción, mismo redondeo que el escalar), `advancePhases` (máscara, sin if).
  Solo SSE2: lo tiene todo x64 (AVX no está garantizado y colgaría el plugin).
- `WavetableOscillator`: unison de 4 en 4 copias (`FrameReader::read4`, `interpolate4` = Catmull-Rom con las mismas
  operaciones en el mismo orden: cada copia idéntica bit a bit). Una sola copia → camino escalar. Ganancias en
  estructura de arrays (`UnisonGains`: left/right/mono[16]); `increments[k]` = phaseIncrement × ratios (0 en copias
  que no suenan: el último grupo SIMD va completo). `CachedReader` (current/currentFast/previous/previousFast): el
  TableReader solo se rehace si cambian tabla, posición o mipmap (antes ~20 ns por muestra). Camino de warp: 1) fases
  por copia (escalar) → 2) lecturas en lote SIMD (`readCopies`: principal + extra para Mirror/Quantize) → 3) saltos y
  quiebres (escalar); valores fijos por muestra (pendiente en 0, final/principio del Sync, este perezoso) una vez y no
  por copia; Bend/FM sola suman con SIMD; `Correction::used` evita acumular ceros. Tono/detune perezosos
  (`incrementPending`: un recálculo por muestra como mucho; ratios solo si cambia el detune o N). `mipFor` solo hace
  log2 dentro de la franja de fundido. `wrapPhase` sin std::floor (mismo resultado).
- `HalfbandDecimator.h`: decimador e interpolador = producto escalar SIMD (`dotProduct`) con una ventana simétrica de
  pesos (`HalfbandDesign::window`, múltiplo de 4); el decimador guarda las primeras y segundas de cada pareja aparte
  (polifásico).
- `Distortion.h`: `AntiderivativeCache` (F(x₋₁) = la F(x) de la muestra anterior; mismo resultado). `dsp/FastMath.h`:
  `fastTanh` (fracción continua de Lambert, error 6·10⁻⁷ = −124 dB) para el drive del filtro. `Filter`: con las
  perillas quietas (`settled`) no se recorre el suavizado. Probado y DESCARTADO: exp2/tan rápidas en el cutoff (sin
  mejora medida en la voz).
- `PluginProcessor::setStateInformation`: los parámetros discretos (on/off, listas) se llevan a su valor exacto tras
  replaceState. Bug desde la Fase 2 encontrado por pluginval con `--randomise`: un on/off con valor crudo 0.34 (Off)
  no se tocaba si el estado restaurado también decía Off.
- CPU antes → ahora (48 kHz, mejor de 5, `UndertowTests --cpu`): Init 8 notas 2.5 → 2.0 %; supersaw 7 copias 5.5 →
  3.9 %; doble supersaw + sub + 4 rutas 20.2 → 11.5 %; pad 16 notas 2 × 16 copias 55 → 29 %; Sync 7 copias 20 → 13 %;
  FM 10.8 → 8.6 %; peor caso (2 × 16 copias, Mirror, FM/RM, 16 notas) 283 → 144 %; distorsión 2.6 → 2.0 %; los 4
  efectos 3.4 → 2.7 %. Presets de fábrica: media 3.3 → 3.2 % (máx. ~7 %): su coste es el fijo de cada voz.
- Verificación de "mismo sonido": 37 escenarios renderizados antes/después (scratch, no en el repo): peor −125 dB bajo
  el pico (orden de la suma); camino de warp idéntico bit a bit. Huellas de los 22 presets de fábrica
  (`tests/reference/factory-presets.txt`, 24 bandas 40 Hz–20 kHz, generadas con el código de la Fase 10): diferencia
  0.005 dB (redondeo a 2 decimales). Tolerancia del test: 0.05 dB en bandas a menos de 60 dB de la más fuerte.
- Tests: `UndertowTests --cpu` (tabla, sin tests). `UndertowPresetTests`: cuenta new/delete dentro de processBlock
  (operator new reemplazado en ese ejecutable) = 0 con los 22 presets, bloques de 1/7/64/256/4096 y cambiando de preset;
  CPU por preset (`--verbose`); huellas (`--update-references` las reescribe: revisar el diff); restaurar el estado deja
  on/off y listas exactos. UndertowTests enlaza con /STACK:8 MB (los tests crean VoiceManagers de ~350 KB en la pila).
  `stdout` sin buffer en UndertowTests (si se cae, se ve en qué test).
- Release con 0 warnings (build limpio); ctest 2/2; pluginval strictness 10 estándar: SUCCESS; ampliado (4 sample rates,
  bloques 1–4096, `--repeat 3 --randomise`): SUCCESS.
- Límites conocidos / pendiente: el coste fijo por voz (~30 ns por muestra fuera del oscilador: envolventes, mezcla,
  suavizados) no se vectorizó; procesar 4 voces a la vez (como Vital) sería el siguiente paso grande. El drive del filtro
  sigue sin oversampling ni ADAA. La latencia de la distorsión (~1 ms) sigue sin informarse al host. Las mediciones de
  CPU son de este equipo (laptop; varían ±10 % entre ejecuciones aunque se tome el mínimo).

Ampliación — Librería por géneros (2026-10-09). Probada por el usuario en FL Studio. Sin cambios en el motor.
- 21 presets de fábrica nuevos (43 en total): Reggaeton (Bajo 808 Perreo, Pluck Dembow, Pad Noche Urbana, Lead Silbido),
  Synthwave (Bajo Synthwave Octavas, Lead Outrun, Pad Neón, Brass Ochentero, Arpegio Retrowave), EDM (Lead Big Room,
  Bajo Future House, Pad Sidechain, Riser Supersaw), Electropop (Bajo Electropop, Stab Electropop, Lead Vocal Chop, Pad
  Brillo Pop), Alternativo (Bajo Fuzz Indie, Lead Fuzz Shoegaze, Pad Shoegaze, Textura Cinta Rota). Las categorías no
  cambian: el género va en la primera línea de las notas ("Género: …"; la búsqueda del navegador lo encuentra).
- El usuario ELIGIÓ incluir soluciones de retos pendientes (sidechain falso: Pad Sidechain y Lead Big Room; lead de
  synthwave: Lead Outrun). APRENDIZAJE.md lo avisa en cada reto. Los demás retos siguen sin resolver en la librería.
- Niveles: RMS −20.1…−17.2 dBFS; Plucks por pico (−10.8 y −11.4). CPU media 3.0 % (máx. Pad Shoegaze 7.4 %).
- Test de huellas: ahora usa un procesador NUEVO por preset (el contador de notas = semilla del unison/ruido/S&H y el
  reloj de los LFO libres seguían corriendo entre renders: añadir un preset cambiaba la huella de los de detrás). Las
  22 huellas viejas se regeneraron con el código sin cambios (comprobado antes que las viejas pasaban sin los nuevos);
  al añadir los 21 el diff solo agrega líneas.
- OJO build: archivos de `build/` con fecha futura hacían que MSBuild no reenlazara (el test no veía presets nuevos);
  y al cambiar el número de presets quedan `BinaryData*.obj` viejos duplicados (LNK4006): borrar
  `build/plugins/undertow/UndertowPresetData.dir` y `build/plugins/undertow/juce_binarydata_UndertowPresetData`
  y reconfigurar (rutas desde la migración al monorepo).
- Release con 0 warnings; ctest 2/2; pluginval strictness 10: SUCCESS.

Migración al monorepo (2026-10-09). Probada por el usuario en FL Studio: funciona. El sonido no cambia (null test:
idéntico bit a bit). FL: la ruta de búsqueda es `build\plugins` como VST3 (editarla en la ventana de FL la vuelve VST2).
- El synth vive en `plugins/undertow/` (movido con `git mv`; `git log --follow` conserva el historial). JUCE, C++20 y
  `enable_testing()` vienen del CMakeLists.txt raíz. El VST3 queda en
  `build/plugins/undertow/UndertowSynth_artefacts/Release/VST3/Undertow Synth.vst3`.
- Pasaron a la librería compartida `libs/dsp` (namespace `deepwater::dsp`): `HalfbandDecimator.h` → `Halfband.h`,
  `SvfStage` (de `Filter.h`) → `Svf.h`, `SmoothedParameter`/`EffectSwitch` (de `DelayLine.h`) → `Smoothing.h`,
  `DelayLine.h`, `Simd.h`, `FastMath.h`, `Fft.h` (+ `src/Fft.cpp`) y `Pitch.h`. Las menciones a esos archivos en las
  fases anteriores se refieren a sus nuevas rutas. Los headers del synth declaran lo que toman (`using
  deepwater::dsp::SvfStage;`, `namespace simd = deepwater::dsp::simd;`); el código de dentro no cambió.
- Se quedan aquí `Filter` (filtro de voz: resonancia ×17, compensación, drive, key tracking, modulación), `Distortion`,
  `Chorus`, `StereoDelay`, `Reverb`, `Lfo`, osciladores, envolventes y ruido. Se mueven cuando otro plugin los necesite.
- Tests: FFT, decimador e interpolador pasaron a `DeepwaterDspTests`. `UndertowPresetTests --render <mid> <carpeta>`
  toca `tests/reference/migration-test.mid` con 4 presets (Supersaw Trance, Sync Lead 80s, Bajo Fuzz Indie, Pad
  Shoegaze) y guarda WAV float de 32 bits; `scripts/null_test.py` (raíz) compara dos carpetas. Referencia de antes de
  la migración en `out/reference-pre-monorepo/` (no está en git).
- Release con 0 warnings (build limpio); ctest 3/3; pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096,
  `--repeat 3 --randomise`): SUCCESS.
- Encontrado al escribir los tests de la librería (bug previo, sin arreglar todavía porque cambiaría el sonido en
  ~1e-5): `SmoothedParameter::approach` y el cutoff de `Filter` nunca terminan de llegar a un objetivo distinto de 0
  (el paso queda por debajo de la resolución del float antes de cruzar el umbral de "llegó"; simulado en float32: se
  quedan a 1.4e-5 y a 1.1e-4 octavas) y siguen avisando de cambios para siempre: tras mover el cutoff, el filtro deja
  de estar `settled`, y tras mover una perilla de chorus/delay/reverb esos efectos recalculan coeficientes en cada
  muestra. Solo cuesta CPU.

Cambio desde Riptide Fase 7 (2026-10-10). El sonido no cambia (null test: idéntico bit a bit).
- El LFO pasó a la librería: `dsp/Lfo.h` → `libs/dsp/include/deepwater/dsp/Lfo.h` (namespace `deepwater::dsp`), junto
  con `lfoShapeNames`, `LfoDivision`, `lfoDivisions` y `defaultLfoDivision` (antes en `synth/Modulation.h`). Su código no
  cambió. `Source/dsp/Lfo.h` quedó como un header que declara `using deepwater::dsp::Lfo/LfoPhase/LfoShape`, y
  `synth/Modulation.h` toma las formas y las divisiones con `using`.

Fase 12 COMPLETADA (2026-10-11). Probada por el usuario en FL Studio: funciona. Lo existente suena igual (null test
bit a bit).
- `libs/dsp` `DelayLine.h`: `LagrangeTaps` (Lagrange de 3.er orden, fracción en [1, 2): ganancia ≤ 1; `forDelay`,
  `response`, `phaseDelay`, `magnitude`) y `DelayLine::readLagrange`. Solo se añadió: chorus/delay/reverb idénticos.
  Test en DeepwaterDspTests (cúbicas exactas, retardo exacto en graves, |H| ≤ 1, fase medida = teoría).
- `dsp/PluckedString.h`: línea (Lagrange) → paso bajo de 1 polo → × g, realimentada; punteo = 1 período de ruido
  blanco sin media, SUMADO a lo que ya vibra (redisparar no hace clic). Decay = T60 del fundamental (igual en todas las
  notas: g por vuelta compensa el filtro y Lagrange en f0). Damping: T60 a 2.5 kHz (o al 2.º armónico si la nota está
  por encima) = Decay × 0.003^damping; el polo sale de esos dos tiempos (`poleForSquaredGain`, forma estable).
  Afinación: vuelta = 1 + línea + retardo de fase del filtro en f0, con la desviación de Lagrange medida y corregida, y
  la corrección del "resbalón" del modo con pérdidas (Δω = −ln|G|·m / (τg² + m²), desarrollo de 1.er orden del polo);
  la caída por muestra ln|G|·τg / (τg² + m²) es la que se ajusta al T60. Límites del polo: continua ≤ passGain^(1/4)
  (cae como mucho 4× más lento que el fundamental; sin esto quedaba continua a −83 dB tras el note-off) y retardo del
  filtro ≤ período − 4. Si la ganancia toca el límite (Lagrange también quita en f0) se ablanda el filtro y se repite.
  Coeficientes cada 32 muestras con rampa lineal de g y del polo; el largo de la línea sigue al tono en cada muestra
  (sin esto, Semi +12 hacía clic). Note-off = palma: en ~15 ms Decay → 0.1 s y Damping → 85 %. Por debajo de −100 dB
  durante una vuelta deja de calcularse. Rango 8 Hz … fs/8; línea reservada en prepare para 8 Hz (12 000 muestras a
  96 kHz por voz). `Voice::prepare` y `VoiceManager::prepare` ya no son noexcept (reservan memoria).
- Voz: `StringSettings` en `SourceSettings` (on, octave −2..2, semi, fine, level 80 %, decay 2 s, damping 50 %).
  Mono, a 1×; en el camino de warp/FM se suma después del decimador (como el ruido). Sigue a la nota, su afinación
  (suavizada 5 ms) y Global Pitch. Se puntea en cada noteOn si está On; `release()` aplica la palma; el pedal la deja
  sonar. Pasa por el filtro, Env 1 y los efectos como las demás fuentes.
- Parámetros nuevos (versionHint 8, al final): stringOn (off), stringOctave, stringSemi, stringFine, stringLevel,
  stringDecay (0.05–30 s log), stringDamping. Sin destinos de matriz todavía (Fase 13). Huellas de los 43 presets: iguales.
- GUI: pestaña "Cuerda" (6.ª): On + Octave, Semi, Fine, Level, Decay, Damping, y un texto que explica la cuerda.
- Tests (`UndertowTests --fase12`): afinación MIDI 12–96 × 4 sample rates × Damping 0/50/100 %: peor 0.012 cents
  (ingenuo en C8 de FL a 44.1 kHz: 47.5 cents); en la voz con Octave/Semi/Fine/Global Pitch: 0.0002 cents; T60 medido
  del fundamental (C4–C8 de FL, Decay 0.5/2/8 s, 3 Damping, 44.1 y 96 kHz): peor 0.13 %; Damping con la FFT de libs/dsp
  (A3 de FL): sin Damping todos los armónicos −20 dB/s; con 25 % h16 −54 y h23 −87 dB/s (diseño a 2.5 kHz: −85);
  extremos (frecuencia 0.5 Hz–50 kHz, Decay y Damping al azar cada 37 muestras, S&H → Global Pitch 100 %, bloques
  1–4095): sin NaN y todo se apaga sin punteos; cambios de Decay/Damping/Semi/Level y note-off sin clic (segunda
  diferencia ≤ 1.11× la de antes); cuerda apagada = voz idéntica bit a bit. CPU (48 kHz, 16 notas): cuerda sola 3 %,
  con vibrato (Global Pitch) 9 %, cuerda + Osc A + filtro 24 dB 6 %.
- Release con 0 warnings; ctest 4/4; null test (`out/reference-pre-string` vs `out/render-fase12`): idéntico bit a
  bit; pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096, `--repeat 3 --randomise`): SUCCESS.
- Límites conocidos: Lagrange atenúa un poco los agudos cuando la fracción cae cerca de 0.5 (con Damping 0 el brillo de
  la cola varía levemente de nota a nota por encima de ~10 kHz); en notas muy agudas con Decay largo el Damping alto se
  ablanda solo (límite de la continua); por encima de fs/8 (5.5 kHz a 44.1 kHz) la cuerda no sube más.

Fase 13 COMPLETADA (2026-10-11). Probada por el usuario en FL Studio: funciona. Lo existente suena igual (null test bit a bit) y
la cuerda con White Noise y Pick Position 0 % suena igual que en la Fase 12 (huella bit a bit, ver abajo).
- `dsp/PluckedString.h`: `StringExcitation` (orden guardado: White Noise, Filtered Noise, Impulse) y `PluckShape`
  (excitación, Pick Position 0–0.5, Pick Tone 200 Hz–20 kHz) que se pasa a `pluck (seed, shape)`. La excitación:
  fuente de un período (el ruido de la Fase 12, misma secuencia, o un golpe de 1 muestra) → Pick Tone: paso bajo TPT de
  1 polo CIRCULAR (estado inicial periódico S / (1 − aᴺ), sin transitorio; solo Filtered Noise e Impulse) → quitar la
  media → peine de Pick Position → nivel. Peine (Jaffe & Smith) x[n] − w·x[n − D], D = β·período REAL, fraccionario
  (Lagrange de 3.er orden por posición) y LINEAL: la excitación dura N + D + 2 muestras (buffer de 1.5 períodos). Probado
  y descartado: D redondeado (lo acordado en el diseño) y el peine circular dentro de las N muestras enteras: los huecos
  se quedaban en −14…−20 dB (el período no es entero); lineal + fraccionario: −57…−79 dB. w sube de 0 a 1 en el
  primer 5 %. Nivel: White Noise se divide por √(energía esperada del peine) = √(1 + w²·Σℓ² − 2w·ℓ₀) (con 1/√(1 + w²)
  quedaba 0.7 dB bajo por el suavizado de Lagrange); Filtered Noise e Impulse: energía exacta = la del ruido (N/3); pico
  limitado a 4 (× 0.25 × Level en la voz: 0.8 como mucho). Por ese límite un Impulse brillante en graves suena más suave:
  con Pick Tone 2.5 kHz −6 dB en A3 de FL (110 Hz), −9 dB en A2 (55 Hz), −13 dB en A1 (27.5 Hz); a 20 kHz hasta −16 dB.
  `setDecay (seconds, modulation)`: modulación en la escala log (100 % = ×600). `getExcitation()` solo para tests.
- Voz: el punteo se aplaza a la primera muestra de la nota (`pendingPluck`), después de `computeModulation`: la matriz
  (Velocity → Pick Tone, String Pitch, Global Pitch) ya vale lo de esa nota al puntear. Decay y Damping se pasan a la
  cuerda en `renderSources` (perilla + matriz) solo si cambian (`updateStringLoss`); ya no en `setSourceSettings`.
  `lastGlobalPitch` eliminado (con vibrato en Global Pitch el punteo usa el valor de la muestra actual: único cambio
  medible respecto a la Fase 12, solo con modulación de Global Pitch).
- Destinos nuevos (al final de `modDestinationNames`): String Level, String Pitch (±24 st, anillo sobre Semi), String
  Decay (100 % = toda la perilla log), String Damping, String Pick Pos (100 % = 0–50 %), String Pick Tone (100 % = ×100);
  los dos últimos solo se leen al puntear. En `gui/ModulationTargets.h` (23 perillas destino).
- Parámetros nuevos (versionHint 9, al final): stringExcitation (White Noise), stringPickPosition (0–50 %, def. 0 %),
  stringPickTone (200 Hz–20 kHz log, def. 2.5 kHz).
- GUI: pestaña "Cuerda", fila 2: grupo "Punteo" (Excitation, Pick Pos, Pick Tone; Pick Tone atenuado con White Noise) y
  su explicación al lado. Arreglado: el texto de ayuda de la Fase 12 estaba guardado con doble codificación UTF-8
  ("lÃ­nea" en la GUI); ahora con escapes, como el resto del editor.
- Tests (`UndertowTests --fase13`): Pick Position con Impulse plano (MIDI 45/57/69 × 4 sample rates): a 50 % los pares
  −61 dB respecto a los impares, a 33 % los múltiplos de 3 −57 dB; Pick Tone (agudos respecto a graves, 500 Hz / 2.5 kHz
  / 20 kHz): Filtered Noise −15.9 / −4.3 / +0.9 dB, Impulse −16.8 / −5.3 / −0.1 dB; White Noise no depende de Pick Tone;
  Impulse no depende de la semilla; energía: Filtered Noise 0.00 dB, White Noise con peine (media de 300) ≤ 0.25 dB,
  pico ≤ 4; los 6 destinos con la Mod Wheel = mover la perilla (bit a bit); Velocity → Pick Tone (+13.8 dB de agudos);
  cambiar el punteo con la nota sonando no la cambia (bit a bit) y la siguiente sí; LFO cuadrado en Decay / Damping /
  Level sin clics; extremos al azar sin NaN; CPU. Fase 9: String Pitch sobre su Semi.
- CPU (48 kHz salvo indicación): 16 punteos de A1 de FL (27.5 Hz) a 96 kHz (el peor caso, ~3500 muestras por período): White Noise
  266 µs, Filtered Noise + Pick Position 816 µs, una vez por nota; 16 notas con LFO → String Decay, Damping y Pitch: 9.6 %.
- Verificación de "mismo sonido": huella FNV de la cuerda antes/después (scratch, no en el repo): `PluckedString`
  directa (6 notas × 3 Damping × 4 sample rates, con re-punteo y note-off) y en la voz sin modulación: idénticas bit a
  bit. Null test de los 4 presets (`out/render-fase12` vs `out/render-fase13`): idéntico bit a bit. Huellas de los 43
  presets: iguales.
- Release con 0 warnings (build limpio); ctest 4/4; pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096,
  `--repeat 3 --randomise`): SUCCESS.
- Límites conocidos: Impulse brillante en graves más suave (límite de pico, arriba); el punteo cuesta ~3× el de la
  Fase 12 (bucles dependientes: paso bajo, sumas); con D < 2 muestras (Pick Position muy bajo en notas muy agudas) la
  compensación de nivel es aproximada.

Fase 14 COMPLETADA (2026-10-11). Probada por el usuario en FL Studio: funciona. Sin cambios en el motor: solo presets y cuaderno.
- 6 presets de fábrica nuevos en `Presets/Factory/Pluck/` (49 en total): Guitarra Nylon, Guitarra Acústica Acero, Koto,
  Arpa de Concierto, Pluck Cuerda Pop (cuerda + sierra con unison) y Kalimba de Madera. Categoría Pluck (las
  categorías no cambian); las notas empiezan por "Instrumento: …" o "Género: …" y dicen "cuerda pulsada".
- Trucos de la matriz que enseñan: Key → String Decay negativo (agudas más cortas), Velocity → String Pick Tone, LFO
  S&H en Retrigger a 0.02 Hz → String Pick Pos (un punto de punteo al azar por nota = humanizar), Env 2 → String Pitch
  +0.5 % (twang del koto) y Mod Wheel → String Pitch +8.33 % (= +2 st, oshide).
- No resuelven retos pendientes (contrabajo pizzicato de la Fase 12, clavecín de la Fase 13). APRENDIZAJE.md: sección
  Fase 14 (las cinco preguntas para diseñar un instrumento, receta de guitarra de funk, reto: banjo de bluegrass).
- Niveles igualados con Master al pico de los plucks existentes: −10.2…−10.9 dBFS con la frase Pluck. Acorde
  sostenido de 4 notas (medido aparte con `--user`): acero −11.2, arpa −5.5 dBFS. CPU 2.1–3.5 %.
- Huellas: el diff de `tests/reference/factory-presets.txt` solo añade las 6 líneas; las 43 viejas, iguales.
- Release con 0 warnings; ctest 4/4; pluginval strictness 10: SUCCESS.
- Límite que se nota en estos presets: el note-off siempre apaga la cuerda con la palma (~0.1 s), así que arpa y koto
  necesitan notas largas o el pedal (CC 64). Un control para elegir cuánto apaga la palma sería una mejora posible.

Fase 15 COMPLETADA (2026-10-11). Probada por el usuario en FL Studio: funciona. Lo existente suena igual: con Stiffness 0 la cuerda
no tiene all-pass (huella FNV de `PluckedString` idéntica bit a bit, scratch) y null test de los 6 presets de referencia
(`out/render-fase14` vs `out/render-fase15`): idéntico bit a bit. Huellas de los 49 presets: iguales.
- `dsp/PluckedString.h`: rigidez = cascada de M all-pass de 1.er orden iguales, (a + z⁻¹) / (1 + a·z⁻¹) con a ≤ 0,
  entre la línea y el filtro de lazo (Van Duyne & Smith). M = 8, o (período − 4) / 2 si es menos (al menos 1). B = 0.05·s⁴
  (`inharmonicityFor`); objetivo fₙ = n·f0·√(1 + B·n²) / √(1 + B) (`stiffPartialRatio`, el 1.er parcial en la nota).
  'a' por Newton con salvaguarda de bisección: fase de la vuelta = 2π·n en el 8.º parcial (o en fs/4 si cae más
  arriba; n fraccionario de la cuadrática sin restas), con la línea despejada de la condición en f0; el filtro de lazo no
  entra (su dispersión ya estaba). La línea se acorta el retardo de fase del all-pass en f0 y su retardo de grupo entra
  en el cálculo del T60 y del "resbalón". Límites: la línea no baja de 1/4 de la vuelta ni de 3 muestras (con 3, una nota
  grave muy rígida subida con la matriz acumulaba la excitación ×20: los agudos casi no se retrasan en el all-pass y para
  ellos la vuelta era de 4 muestras); el polo del filtro deja sitio al all-pass; a ≥ −0.999; vuelta ≥ M + 5.
- 'a' se elige al puntear una cuerda EN SILENCIO y queda fijo hasta que vuelve al silencio (se quita el all-pass); un
  redisparo con la cuerda sonando conserva su rigidez. Probado y descartado (ver el comentario de la clase): moverla con
  la cuerda sonando, primero con deslizamiento de 10 ms (clics de hasta 35×: la línea saltaba cientos de muestras cada
  32), luego con rampa por muestra y límite de velocidad (sin clics, pero cada cambio sube o baja el nivel 2–7 dB por la
  energía que guarda el all-pass, y con S&H a Stiffness y a Pitch o redisparos esos saltos se sumaban: ×10 000; la forma
  de escalera normalizada, ortogonal, no lo arreglaba). Con 'a' fijo el all-pass es LTI. Única excepción: si la nota sube
  tanto que no cabe, 'a' va hacia 0 lo justo, en rampa de 32 muestras, y no vuelve (monótono: el nivel cambia una vez).
  Con el tono moviéndose, el retardo del all-pass en f0 se sigue con su pendiente d(φ/ω)/dω = (grupo − fase) / ω entre
  cálculos completos (`updateLength`).
- Voz: `StringSettings::stiffness` (al final del struct); Stiffness (perilla + matriz) se pasa en `updateStringBody`
  (antes `updateStringLoss`) solo si cambia. Destino nuevo al final: String Stiffness (100 % = toda la perilla), en
  `gui/ModulationTargets.h` (24 perillas destino).
- Parámetro nuevo (versionHint 10, al final): stringStiffness (0–100 %, def. 0). GUI: 7.ª perilla de la fila de la
  cuerda y una frase en su ayuda.
- Render de referencia (`UndertowPresetTests --render`): ahora 6 presets, + Koto y Guitarra Nylon (para que el null
  test cubra la cuerda).
- Tests (`UndertowTests --fase15`): parciales 1–8 medidos (A0–A5 de FL, 4 sample rates) contra la fórmula: fundamental
  0.005 cents; parciales 2–8, peor 0.2 / 9 / 36 / 40 cents con Stiffness 25 / 50 / 75 / 100 % (la cascada de all-pass
  iguales no sigue la curva lejos del 8.º; el simulador en Python lo predijo igual); afinación MIDI 12–96 × 4 sample
  rates × 3 Damping con Stiffness 50/100 %: 0.03 cents; saltos de tono de −12 a +12 st con la cuerda sonando: 0.04
  cents; T60 del fundamental con 100 %: 0.3 %; Stiffness 0 sin all-pass y la nota siguiente a una rígida, bit a bit;
  perilla y redisparo con la cuerda sonando no la cambian (bit a bit); Mod Wheel → String Stiffness = la perilla (bit a
  bit); Velocity → String Stiffness; extremos al azar sin NaN; tortura (tono ±48 st a 40 Hz, redisparos con otra rigidez,
  Decay hasta 30 s): pico 9.6 contra 8.6 sin rigidez. CPU (48 kHz, 16 notas): cuerda sola 2.9 %, con Stiffness 100 %
  4.0 %, con vibrato 11.3 %.
- Release con 0 warnings; ctest 4/4; pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096,
  `--repeat 3 --randomise`): SUCCESS.
- Límites conocidos: por encima del 8.º parcial la curva se estira menos que la fórmula (y en notas muy agudas, con
  pocas secciones, también los primeros); el hueco de Pick Position está calculado para armónicos y con mucha rigidez
  se "rellena"; los parciales agudos mueren antes que el fundamental aunque Damping sea 0 (dan más vueltas por segundo:
  físico); Stiffness no se puede barrer con la nota sonando (para eso, Damping/Decay o el filtro).

Fase 16 COMPLETADA (2026-10-11). Probada por el usuario en FL Studio: funciona. Lo existente suena igual: con White Noise, Filtered
Noise e Impulse la cuerda hace el mismo cálculo (huella FNV de `PluckedString` y de la voz, scratch: idénticas bit a
bit) y null test de los 6 presets de referencia (`out/render-fase15` vs `out/render-fase16`): idéntico bit a bit.
Huellas de los 49 presets: iguales.
- `dsp/PluckedString.h`: `StringExcitation::sources` (al final de la lista). `resonate (input)`: una entrada en el lazo
  en cada muestra (se suma donde entraba la excitación), H = 1 / (1 − G·z⁻ᴺ). Cadena de la entrada: filtro DC (5 Hz) →
  Pick Tone (paso bajo TPT de 1 polo de libs/dsp) → Pick Position (el peine de la Fase 13 en tiempo real, sobre una
  línea propia de medio período, Lagrange, nivel / √(1 + w²)) → × (1 − |G|), con |G| la ganancia real de la vuelta en
  el modo (lazo × filtro × Lagrange), en rampa con la del lazo. Así un tono afinado sale con su mismo nivel en el
  fundamental con cualquier Decay y nunca más fuerte; entre dos armónicos sale (1 − g) / (1 + g). Descartado:
  normalizar la potencia del ruido blanco (un tono afinado subía hasta +40 dB con Decay largo). Pick Pos y Pick Tone
  se deslizan 10 ms y se leen siempre (`setResonatorShape`). Una cuerda en silencio despierta cuando la entrada pasa
  de −100 dB (`wakeUp`, compartido con `pluck`; la rigidez se elige ahí). `processSample()` = `tick<false>` (el cálculo
  de siempre); `resonate` = `tick<true>`. `letRing()` quita la palma.
- Voz: con Excitation = Sources no se puntea; la mezcla de las fuentes (en mono, después del decimador y del fundido de
  cambio de camino) × Input entra en la cuerda y la voz saca la cuerda × Level en vez de las fuentes. `stringResonance`
  (0 = fuentes directas, 1 = por la cuerda) se desliza 5 ms: encender/apagar la cuerda o cambiar Excitation con la nota
  sonando es un fundido. Sin palma en modo resonador (manda el Release de Env 1). Si el resonador no está activo, el
  camino es el de la Fase 15 (mismo orden de sumas).
- Parámetros nuevos: la opción "Sources" al final de String Excitation (cambia el número de pasos del parámetro: una
  automatización de Excitation guardada en FL se movería; el estado guardado es el índice y carga bien) y stringInput
  (versionHint 11, 0…+36 dB, def. 0 dB): el ruido por el resonador baja 16 / 23 / 31 dB con Decay 0.2 / 0.5 / 2 s
  (A4 de FL, Damping 30 %), sin ella no se podía compensar. Los destinos String Pick Pos / Pick Tone actúan siempre en
  modo resonador.
- GUI: grupo "Punteo" con 3 perillas (Pick Pos, Pick Tone, Input; Input atenuada fuera de Sources); la ayuda de al lado
  cambia a la del resonador al elegir Sources.
- Tests (`UndertowTests --fase16`): seno afinado (MIDI 33–81, 4 sample rates, Decay 0.1/0.3/1 s, Damping 0/50/100 %):
  peor 0.03 dB; a 1.5·f0, de 0.0 a +0.4 dB de la teoría (1 − g) / (1 + g); T60 al cortar la entrada: 0.0 % y vuelve al
  silencio; Pick Position 50 % con ruido: pares −37.6 dB respecto a impares; Pick Tone 300 Hz: −11.3 dB (teoría −11.1);
  voz: sin fuentes, silencio exacto (no hay punteo); sierra afinada: fundamental +0.00 dB, 5.º −5.9 dB; cuerda una
  quinta por debajo: el armónico común −5 dB, el que cae entre dos −42 dB; Input +20 dB = +20.00 dB; cuerda apagada en
  modo Sources = voz sin cuerda bit a bit; On/Off, Sources ↔ Impulse, Pick Pos, Pick Tone, Decay e Input con la nota
  sonando sin clic (segunda diferencia ≤ 0.93× la mayor de las dos ondas); note-off sin palma (−2.6 dB en 0.1 s, igual
  que la sierra directa); extremos al azar sin NaN (cuadrada de amplitud 1: pico 2.06; voz con S&H: 3.47). CPU (48 kHz,
  16 notas): sierra 4.0 %, sierra por el resonador 6.4 %, con Pick Position y vibrato 15.9 %.
- Release con 0 warnings; ctest 4/4; pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096,
  `--repeat 3 --randomise`): SUCCESS.
- Límites conocidos: la cuerda es mono (el unison y el paneo de los osciladores se pierden al pasar por ella); la
  normalización es por el fundamental: con Damping alto los armónicos agudos de un tono afinado salen más bajos (es el
  Damping), y el ruido y lo desafinado salen mucho más bajos (subir Input); con Decay largo la resonancia tarda en
  llenarse (un ataque lento: físico); Stiffness se elige al despertar la cuerda, y con la entrada sonando sin parar no
  vuelve a elegirse hasta la nota siguiente desde silencio.

Cambio desde Riptide Fase 9 (2026-10-11). El sonido no cambia (huellas de los 49 presets: iguales).
- El sistema de presets pasó a la librería compartida `libs/presets` (`deepwater::presets`): `PresetManager.cpp`,
  `PresetBar.h`, `PresetBrowser.h` y `PresetSaveDialog.h` (movidos con `git mv`), generalizados con un `PresetFormat`
  (etiqueta raíz, extensión, categorías, carpeta, parámetros excluidos, archivos de fábrica) y una `Palette` de colores.
  El pegamento del editor (cargar, siguiente/anterior, guardar con las notas empezadas, menú ···) quedó en
  `PresetPanel.h`; el editor de Undertow solo lo coloca. Aquí quedan `presets/PresetManager.h` (categorías, extensión y
  `format()`) y `presets/PresetFormat.cpp` (lee BinaryData). El formato `.uwpreset` y el comportamiento no cambian.
  Arreglado de paso: el aviso de "versión más nueva" llevaba tildes sin `fromUTF8` (aserción de JUCE en Debug).
- `UndertowPresetTests`, pluginval strictness 10 ampliado: SUCCESS. Revisado con capturas del Standalone.

Fase 17 COMPLETADA (2026-10-11). Pendiente de probar en FL Studio. Lo existente suena igual: null test de los 6 presets
de referencia (`out/render-fase16` vs `out/render-fase17`): idéntico bit a bit; huellas de los 49 presets: iguales.
- Origen: el usuario oía "distorsionada" la sierra del Init en acordes graves (C2–C4 de FL). Medido con el motor real
  (programa aparte, scratch): afinación C2–C4 a 4 sample rates, peor 0.004 cents; alias < 16 kHz −92…−97 dB; armónicos
  = 1/h. No había error: es la aspereza de los batidos entre armónicos de terceras graves (explicado en APRENDIZAJE.md).
  Como mejora "a lo Serum 2" se añadió la fase inicial (su manual describe PHASE, RAND y Phase Memory; los valores de su
  Init no están publicados y el usuario no tiene Serum).
- `dsp/WavetableOscillator.h`: `PhaseStart` (phase 0..1, random 0..1, freeRunning) y `reset (seed, start)`. Una copia:
  fase = Phase + Rand·u (u = `randomPhase (seed, 0)`, envuelta); Phase 0 y Rand 0 dan 0.0 exacto (el arranque de
  siempre). Con unison las copias siguen al azar como en la Fase 6 (Phase/Rand no cuentan; elegido por compatibilidad:
  sin "laser zap" con Rand 0, a diferencia de Serum). Free: las fases no se tocan (cada ranura de voz sigue donde se quedó
  su oscilador, como las tarjetas de voz de un analógico); la primera nota de un oscilador nuevo arranca como Reset
  (`phasesInitialised`). El sub no tiene fase propia (sigue en 0).
- `synth/SourceSettings.h`: `PhaseMode` (orden guardado: Reset, Free) y en `OscillatorSettings` phase, phaseRandom,
  phaseMode y `phaseStart()`. `Voice::start` pasa `osc.phaseStart()` al reiniciar desde silencio (un redisparo con la
  voz sonando no reinicia nada, como antes).
- Parámetros nuevos (versionHint 12, al final; `params::oscillatorPhases[]`): oscXPhase (0–360°, def. 0), oscXPhaseRand
  (0–100 %, def. 0), oscXPhaseMode (Reset/Free, def. Reset). No son destinos de la matriz (solo se leen al empezar la nota).
- GUI: pestaña "Warp y FM", columna "Fase" de cada oscilador: selector Reset/Free (con tooltip) + perillas Phase y Rand,
  atenuadas con Free o con Unison > 1. Revisado con captura del Standalone. OJO: los escapes UTF-8 escritos desde un
  script de Python quedaron doble codificados ("0Â°"); en el fuente deben quedar como `\xc2\xb0` dentro de `fromUTF8`.
- Tests (`UndertowTests --fase17`): Phase = sin (2π·fase) en la primera muestra (error 1.2e-8) y 360° = 0°; Rand 25 %
  queda en el primer cuarto; Rand 100 % uniforme (media −0.003, media del cuadrado 0.505, 4000 notas distintas); Rand 0
  igual con cualquier semilla; con unison Phase/Rand no cambian nada (bit a bit); Free = oscilador que nunca se
  reinicia (1 y 5 copias, bit a bit) y la primera nota en Phase; en la voz Phase 180° invierte Osc A y Osc B (4.5e-8) y
  dos notas desde silencio son idénticas con Rand 0 y distintas con Rand 100 % o Free.
- Release con 0 warnings; ctest 6/6; pluginval strictness 10 ampliado (4 sample rates, bloques 1–4096,
  `--repeat 3 --randomise`): SUCCESS.
- Ideas para más adelante: línea de la fase de arranque en el visor 2D (como Serum), Phase como destino de la matriz
  (por nota), modo "Per Voice" de Serum, fase del sub.
