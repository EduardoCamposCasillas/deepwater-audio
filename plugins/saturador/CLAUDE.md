# CLAUDE.md — Saturador

Plugin 2 de la suite. Las reglas generales (modo de trabajo, stack, reglas del hilo de audio, estilo, git) están en
el `CLAUDE.md` de la raíz del repositorio; aquí solo va lo propio del saturador.

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

Actualizar el archivo `APRENDIZAJE.md` de esta carpeta (`plugins/saturador/`) al final de cada fase (conceptos,
recetas de uso, vocabulario, retos completados). Los presets de fábrica incluyen notas de para qué sirven y por qué.

## Hoja de ruta
- [ ] Fase 1 — Proyecto base: `plugins/saturador/` con su `CMakeLists.txt` (VST3 + Standalone, efecto mono/estéreo),
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
Fase 1 sin empezar. La librería compartida ya existe (migración al monorepo, 2026-10-09); la carpeta tiene solo este
archivo y `APRENDIZAJE.md`, todavía sin `CMakeLists.txt` ni código (el CMake raíz aún no la incluye).
