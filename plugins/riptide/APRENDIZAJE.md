# APRENDIZAJE — Riptide (saturador)

Cuaderno de mezcla y diseño sonoro del saturador. Se completa al final de cada fase (ver `CLAUDE.md`).

## Fase 1 — Niveles, medidores y comparar a oído justo

Riptide todavía no satura: por ahora solo pasa la señal. Pero esta fase enseña lo más importante de toda la
saturación: **medir el nivel y comparar con el volumen igualado**. Sin eso, cualquier saturador "suena mejor" solo
porque suena más fuerte.

### Conceptos aprendidos

- **Decibelios (dB).** El oído percibe el volumen de forma logarítmica, así que los niveles se cuentan en dB:
  dB = 20 · log10(ganancia). +6 dB es el doble de amplitud, −6 dB la mitad, 0 dB = ×1 exacto. Un cambio de 1 dB
  apenas se nota; uno de 10 dB se oye como "el doble de fuerte".
- **dBFS (dB *full scale*).** En digital, 0 dBFS es la muestra más grande que cabe en un WAV o en el conversor de la
  tarjeta de sonido. Todo se mide hacia abajo desde ahí: −6 dBFS, −18 dBFS... Dentro de FL el audio es de punto
  flotante y puede pasar de 0 sin romperse, pero al exportar o al llegar a las bocinas lo que pasa de 0 **se recorta**.
- **Pico vs RMS.** El pico es la muestra más alta: decide si la señal se recorta. El RMS (raíz de la media de los
  cuadrados) es la energía media en los últimos ~300 ms, y se parece mucho más a lo fuerte que se *oye*. Un seno
  tiene el RMS 3 dB por debajo del pico; una batería, 12–20 dB por debajo (golpes cortos y altos); un bajo
  sostenido, 6–10 dB.
- **Factor de cresta.** La diferencia entre pico y RMS. Es la medida de qué tan "dinámica" o "con pegada" es una
  señal. **La saturación reduce el factor de cresta**: recorta los picos y deja la energía. Por eso suena más densa y
  más fuerte con el mismo pico. Lo veremos en la Fase 2: el pico se queda, el RMS sube.
- **Gain staging.** Decidir con qué nivel entra la señal a cada etapa. En un saturador es la decisión principal: el
  mismo sonido entrando a −24 dBFS pasa casi limpio; entrando a 0 dBFS satura mucho. Por eso el medidor **IN de
  Riptide mide después de la perilla Input**: muestra el nivel con el que la señal llega al saturador.
- **Por qué el bypass hace un fundido.** Conmutar de golpe entre dos señales distintas deja un salto en la forma de
  onda, y un salto se oye como clic. Riptide cruza las dos señales en 20 ms: es lo bastante corto para sentirse
  instantáneo y lo bastante largo para no hacer clic.
- **Suavizado de perillas.** Igual que el bypass: si la ganancia saltara de golpe se oiría un clic (o un "zipper"
  si se mueve la perilla). Cada cambio se reparte en una rampa de 20 ms.

### Qué hace cada control

- **Input** (−24 a +24 dB): sube o baja la señal *antes* del saturador. En esta fase solo cambia el volumen; desde la
  Fase 2 será el **drive**: cuánto se empuja la señal contra la curva de saturación (más Input = más armónicos, más
  densidad, menos pegada).
- **Output** (−24 a +24 dB): el volumen final. Su trabajo es **devolver el volumen a su sitio** después de saturar,
  para que la comparación con el bypass sea justa.
- **Bypass**: escucha la señal original. Es el mismo botón que el interruptor del slot en el mixer de FL.
- **Medidores IN y OUT**: barra tenue = pico; barra sólida = RMS; línea blanca = pico más alto de los últimos 1.5 s;
  LED rojo arriba = alguna muestra pasó de 0 dBFS (clic en el medidor para apagarlo). Abajo, en números, el pico
  sostenido y el RMS del canal más fuerte.
- Doble clic en una perilla = 0 dB. Con Input y Output en 0 dB la señal sale idéntica bit a bit.

### Ejercicio de escucha guiado: "más fuerte = mejor"

1. En FL, carga un loop de batería en el Channel Rack, mándalo a un insert del mixer y pon Riptide en ese insert.
2. Mira los medidores: anota el pico y el RMS del loop. ¿Cuánto es su factor de cresta? Haz lo mismo con un bajo y
   con un pad. Compara: la batería tendrá la diferencia más grande.
3. Pon **Output en +1.0 dB** y alterna el Bypass varias veces mientras suena (el interruptor del slot o el botón).
   Casi seguro te parece que con Riptide activo "suena mejor": más lleno, más presente, más brillante. Pero solo
   sube 1 dB y nada más.
4. Ahora pídele a alguien que alterne el bypass sin decirte cuál es cuál (o cierra los ojos y alterna tantas veces
   que pierdas la cuenta). Intenta adivinar cuál es el procesado. Repite con +0.5 dB y con +0.2 dB.
5. Conclusión: el oído confunde "más fuerte" con "mejor". **Toda comparación de un efecto se hace con el volumen
   igualado**, y para eso está la perilla Output.

### Receta de uso: preparar el nivel antes de saturar (gain staging)

Con lo que hay en esta fase no se colorea el sonido, pero ya sirve para la costumbre que vas a usar con cualquier
saturador:

1. Pon Riptide en el insert y reproduce la parte más fuerte de la pista.
2. Mueve **Input** hasta que el medidor IN marque unos **−18 dBFS de RMS** (picos alrededor de −6 a −10 dBFS). Es
   un nivel de referencia típico: deja margen (headroom) y en la Fase 2 será el punto donde la saturación empieza a
   notarse de forma suave.
3. Compensa con **Output** lo mismo al revés (Input +4 → Output −4). Comprueba que OUT y el bypass suenan igual de
   fuerte: mira que el RMS de OUT sea el mismo que tenía la señal original.
4. ¿Por qué? Así, cuando haya saturación, Input decide *cuánto color* hay y Output decide *el volumen*, y nunca se
   mezclan las dos decisiones.

### Reto sin receta

Tienes una voz grabada muy baja (RMS −30 dBFS) y una caja muy alta (picos a −1 dBFS). Pon una instancia de Riptide en
cada una y deja las dos con el **mismo RMS en el medidor OUT**, sin que la caja encienda el LED de clip. Después,
escúchalas juntas: ¿suenan igual de fuertes aunque el RMS sea el mismo? ¿Por qué podría no ser así? Inténtalo y
después pide pistas.

### Vocabulario

- **dB / dBFS**: escala logarítmica de nivel; dBFS cuenta desde el máximo digital (0). *En música:* los masters
  modernos de pop y reggaeton tienen picos a −1 dBFS.
- **Pico**: la muestra más alta. Decide si algo se recorta. *En música:* el golpe de una caja es casi todo pico.
- **RMS**: nivel medio de energía; se parece a lo fuerte que se oye. *En música:* un 808 sostenido tiene un RMS
  alto aunque su pico sea igual al de una caja.
- **Factor de cresta**: pico − RMS. Alto = dinámico, con pegada; bajo = denso, aplastado. *En música:* el jazz
  grabado en vivo tiene factor de cresta alto; un drop de EDM, muy bajo.
- **Headroom (margen)**: lo que queda entre el pico y 0 dBFS. *En música:* las mezclas se dejan con 3–6 dB de margen
  para que el mastering tenga sitio.
- **Clipping (recorte)**: lo que pasa cuando la señal intenta pasar de 0 dBFS: se aplana la punta de la onda. Duro
  y feo cuando es accidental; en la Fase 3 lo usaremos a propósito (modo hard clip).
- **Gain staging**: elegir con qué nivel entra la señal a cada etapa de la cadena. *En música:* el sonido "caliente"
  de las consolas analógicas viene en buena parte de cómo los ingenieros empujaban cada etapa.
- **Unity gain**: ganancia ×1 (0 dB): la señal sale como entró.
- **Gain matching (igualar volumen)**: compensar el volumen de un efecto para compararlo con el original sin que el
  oído se deje engañar por el nivel.
- **Clic**: salto brusco en la forma de onda; se oye como un "tac" aunque el sonido sea suave.

### Errores comunes

- **Comparar con distinto volumen.** El error número uno al saturar. Si no igualas el volumen, no sabes si te gusta
  el color o solo el nivel.
- **Juzgar el volumen con el medidor de pico.** Dos sonidos con el mismo pico pueden sonar a volúmenes muy distintos;
  para "qué tan fuerte se oye" mira el RMS (y al final, tus oídos).
- **Ignorar el LED de clip** "porque FL no recorta por dentro". Es verdad dentro del mixer, pero el siguiente plugin
  puede comportarse distinto con señales sobre 0 dBFS, y al exportar a WAV de punto fijo se recorta.
- **Subir Input "porque se ve bajo"** sin bajar Output después: la pista sube de volumen en la mezcla y todo lo demás
  parece peor.

### Retos completados

(Ninguno todavía.)

## Fase 2 — La primera saturación (tanh) y el aliasing

Ahora Riptide satura de verdad. La perilla **Input** es el *drive*: decide con qué nivel llega la señal a la curva.
Y aparece un control técnico que todo saturador serio tiene: el **oversampling**.

### Conceptos aprendidos

- **Waveshaping (curva de transferencia).** Cada muestra pasa por una función: `salida = f(entrada)`. Si `f` fuera una
  recta, sería solo un cambio de volumen. Riptide usa `tanh`, que es casi recta cerca de 0 y se dobla suavemente hacia
  ±1: las partes bajas de la onda pasan igual y los picos se redondean. **Redondear la onda = crear armónicos.**
- **Clipping suave vs duro.** `tanh` es un clip *suave*: la curva se dobla poco a poco, así que los armónicos crecen
  de forma gradual al subir el drive. Un clip duro (Fase 3) corta en seco: más armónicos agudos y más aspereza.
- **Armónicos impares.** Una curva simétrica (lo que hace con la parte positiva lo hace igual con la negativa) solo
  crea armónicos impares: 3×, 5×, 7× la frecuencia. Con un La4 de 220 Hz aparecen 660, 1100, 1540 Hz... Suenan más
  "huecos", "nasales" o "cuadrados" que los pares (Fase 3: bulbo asimétrico = pares, más "cálidos").
- **Techo.** `tanh` nunca pasa de ±1: por mucho drive que pongas, antes de Output la señal no supera 0 dBFS. Por eso
  con mucho drive el pico se queda quieto y el RMS sube: **baja el factor de cresta** (lo que se predijo en la Fase 1).
- **Aliasing.** A 44.1 kHz no cabe nada por encima de 22.05 kHz (Nyquist). Si la curva crea un armónico a 35 kHz,
  no desaparece: se **refleja** como en un espejo y cae en 44.1 − 35 = 9.1 kHz. Ese tono no es múltiplo de la nota:
  suena inarmónico, metálico, "digital barato". Lo delator: si la nota sube, el alias puede **bajar**.
- **Oversampling.** Saturar a 2, 4, 8 o 16 veces la frecuencia de muestreo. Así los armónicos tienen sitio para
  existir sin reflejarse, y luego un filtro los quita antes de volver a 44.1 kHz. Medido en Riptide con un seno de
  5 kHz muy saturado: el aliasing está a −25 dB sin oversampling (se oye), −58 dB con 2x y −118 dB con 4x (inaudible).
- **Latencia.** Los filtros del oversampling tardan un poco (≈1.7 ms con 4x a 44.1 kHz). Riptide se lo dice a FL, y
  FL retrasa las demás pistas lo mismo para que todo siga alineado (compensación de latencia, *PDC*). Al cambiar el
  factor, Riptide hace un silencio de 10 ms para que el cambio no haga clic.
- **DC.** Si una curva desplaza la onda hacia arriba o abajo, aparece una componente de 0 Hz. No se oye, pero gasta
  headroom. Riptide tiene un filtro DC a 5 Hz después de la curva (con tanh casi no trabaja; será importante en la
  Fase 3 con las curvas asimétricas).

### Qué hace cada control

- **Input (drive)**: con la señal a −18 dBFS RMS, de 0 a +6 dB apenas se nota (un poco más de densidad, picos algo
  más redondos). De +6 a +12 dB la saturación se oye: más "cuerpo", los transitorios pierden filo, aparecen armónicos
  en el analizador. De +12 a +24 dB es distorsión clara: la onda se vuelve casi cuadrada, el sonido se aplana y se
  llena de agudos ásperos.
- **Output**: igual que antes, para igualar el volumen. Regla rápida: si subes Input X dB, baja Output un poco menos
  de X (tanh comprime, el volumen no sube tanto) y ajusta mirando el RMS de OUT.
- **Oversampling** (Off / 2x / 4x / 8x / 16x): **4x** es el punto de partida (aliasing inaudible con poco CPU). Off solo
  para oír el aliasing o si buscas ese sonido "lo-fi digital". 8x y 16x apenas cambian con tanh; servirán con las
  curvas duras de la Fase 3.
- En el analizador: con un seno en la entrada, cada paso de drive hace aparecer una "escalera" de picos en 3×, 5×,
  7×... la frecuencia. En la forma de onda (Wave Candy, modo oscilloscope) el seno se va volviendo cuadrado.

### Ejercicio de escucha guiado: ver y oír el aliasing

1. Crea un canal con **3x Osc** (una sola onda senoidal) y toca un **La7 (1760 Hz)**. Mándalo a un insert con Riptide
   y, **después** de Riptide, un **Fruity Parametric EQ 2** (su analizador) o **Wave Candy** en modo espectro.
2. Riptide en **Oversampling 4x, Input +18 dB, Output −12 dB**. En el analizador verás la escalera de armónicos
   impares: 5280, 8800, 12320 Hz... ordenados, a intervalos iguales.
3. Pon **Oversampling Off**. Aparecen picos **entre** los armónicos, incluso por debajo de la fundamental. Eso es
   aliasing. Escucha: el tono se ensucia con un zumbido metálico.
4. Haz un **glissando** lento hacia arriba (slide en el Piano roll) con Off: oirás tonos que bajan mientras la nota
   sube. Es la firma del aliasing. Repite con 4x: desaparecen.
5. Prueba con una **nota grave** (La3, 110 Hz) en Off y en 4x: casi no hay diferencia. El aliasing es un problema
   sobre todo con **agudos fuertes y mucho drive** (hi-hats, platillos, voces brillantes, leads).
6. Cambia entre Off y 16x mientras suena: hay un pequeño hueco de silencio, sin clic, y FL vuelve a compensar la
   latencia solo.

### Receta de uso: densidad en un bus de batería

1. Pon Riptide en el insert de la batería (o de un loop). Oversampling 4x.
2. Lleva el RMS de entrada a unos −18 dBFS con Input y, desde ahí, sube **Input de +4 a +8 dB**.
3. Baja **Output** hasta que el RMS de OUT sea el mismo que con bypass. Alterna el bypass.
4. Qué notar: la batería suena más "junta" y densa, la caja con más cuerpo, los platillos un poco más presentes; a
   cambio, los golpes pierden algo de pegada (el pico se redondea). Si pierdes demasiada pegada, baja Input.
5. ¿Por qué funciona? tanh aplasta los picos (los golpes) más que el resto: baja el factor de cresta y añade
   armónicos que hacen que cada golpe se oiga más, incluso al mismo volumen.

### Reto sin receta

Tienes un **808** que suena bien en audífonos pero desaparece en las bocinas del celular (no reproducen por debajo de
~100 Hz). Usa Riptide para que el 808 se oiga en bocinas pequeñas **sin que suba su volumen** (mismo RMS en OUT con y
sin bypass). Para comprobarlo, pon un **Fruity Parametric EQ 2** después de Riptide que corte todo por debajo de
150 Hz (simula el celular). Inténtalo y después pide pistas.

### Vocabulario

- **Waveshaper / curva de transferencia**: la función que convierte cada muestra de entrada en una de salida.
  *En música:* los pedales de distorsión de guitarra son waveshapers analógicos.
- **Drive**: cuánto se empuja la señal contra la curva. *En música:* el "Drive" de un Tube Screamer o de Decapitator.
- **Soft clipping (clip suave)**: la curva se dobla poco a poco. *En música:* la cinta y los preamplificadores de
  consola; el "pegamento" de muchas mezclas de rock de los 70.
- **Armónicos impares**: 3×, 5×, 7× la fundamental. *En música:* el sonido de un fuzz o de una onda cuadrada (el
  clarinete también es rico en impares).
- **Nyquist**: la mitad de la frecuencia de muestreo; lo más agudo que cabe (22.05 kHz a 44.1 kHz).
- **Aliasing**: armónicos que pasan de Nyquist y se reflejan como tonos inarmónicos. *En música:* la aspereza de los
  primeros plugins de distorsión y de muchos bitcrushers (ahí se usa a propósito).
- **Oversampling**: procesar a una frecuencia de muestreo más alta para evitar el aliasing.
- **Latencia / PDC**: el retardo que añade un plugin y la compensación automática del DAW que mantiene todo alineado.
- **DC offset**: desplazamiento constante de la onda respecto al 0. *En música:* aparece en grabaciones con equipos
  defectuosos; hace que los cortes de audio hagan clic.

### Errores comunes

- **Saturar con más volumen** y creer que suena mejor. Iguala siempre con Output antes de decidir.
- **Dejar Oversampling en Off** en material brillante con mucho drive: el aliasing ensucia los agudos y se acumula al
  usar varias instancias.
- **Poner 16x "por si acaso"** en 30 pistas: gasta CPU sin diferencia audible con tanh. 4x basta; sube solo si oyes
  o ves aliasing.
- **Mucho drive en todo**: si todas las pistas están saturadas, la mezcla pierde dinámica y profundidad. Satura donde
  haga falta densidad o presencia, no por costumbre.
- **Cuándo NO usar saturación**: en pistas que ya vienen distorsionadas o muy comprimidas (se vuelven planas), en
  instrumentos acústicos donde la dinámica es lo importante (piano clásico, guitarra fingerstyle), y en el master si
  no puedes comparar con el volumen igualado.

### Retos completados

(Ninguno todavía.)
