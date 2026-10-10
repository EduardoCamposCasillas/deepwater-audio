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

## Fase 3 — Seis modos con carácter

Hasta ahora Riptide tenía una sola curva (tanh). Un saturador profesional tiene varias porque **cada curva es un
instrumento distinto**: no se trata de "más o menos distorsión" sino de *qué tipo* de armónicos aparece y *desde qué
nivel*. Los seis modos tienen el mismo volumen con señal baja (pendiente 1 en el origen), así que puedes cambiar entre
ellos y comparar el carácter sin que el volumen te engañe.

### Conceptos aprendidos

- **La rodilla (knee).** El tramo donde la curva deja de ser recta. Una rodilla **blanda** (cinta) empieza a redondear
  muy pronto pero poco a poco: se siente como compresión más que como distorsión. Una rodilla **corta** (transistor)
  deja pasar la señal limpia casi hasta el límite y ahí se ensucia de golpe. El **clip duro** no tiene rodilla: recto
  hasta 0 dBFS y plano después. Con la misma entrada (1.0): la cinta da 0.71, el transistor 0.84 y el clip 1.0.
- **Comportamiento según el nivel.** Por la rodilla, cada modo reacciona distinto al subir Input: la cinta se nota
  "un poquito" desde muy abajo, el transistor no hace nada hasta que de pronto muerde, el wavefold cambia de timbre por
  completo cada vez que la onda se dobla otra vez. Por eso un mismo modo suena distinto en un bajo a −24 dBFS que a
  −6 dBFS.
- **Armónicos pares vs impares.** Una curva simétrica (cinta, transistor, clip, wavefold) solo crea impares (3×, 5×...):
  sonido "cuadrado", hueco, con mordida. Una curva **asimétrica** (bulbo) aplasta antes un semiciclo que el otro y crea
  también **pares** (2× = una octava, 4× = dos octavas): se funden con la nota y suenan "llenos", cálidos. El bulbo de
  Riptide añade un poco de 2º armónico incluso con señal baja (entre −60 y −40 dB): es su calidez de fondo.
- **DC por asimetría.** Si un semiciclo se aplasta más que el otro, la onda deja de estar centrada en 0. El filtro DC
  de la Fase 2 la vuelve a centrar. El bulbo con mucho drive llega a −1.41 en el semiciclo negativo (+3 dBFS): mira el
  medidor OUT y baja Output.
- **Pre-énfasis / de-énfasis (cinta).** Las grabadoras de cinta suben los agudos antes de grabar y los bajan al
  reproducir. Con señal baja se anulan (respuesta plana). Con drive, los agudos llegan 6 dB más fuertes a la cinta y se
  saturan antes: **la cinta comprime más los agudos que los graves**. Por eso suaviza platillos, sibilancias y
  transientes. Medido en Riptide: con +12 dB de drive, 8 kHz se comprime más de 2 dB más que 300 Hz.
- **Head bump.** Las cabezas reproductoras de cinta tienen una resonancia en los graves. Riptide la imita con +2 dB en
  70 Hz (campana ancha). Está siempre, incluso sin drive: es parte del "sonido de cinta" (cuerpo en bombo y bajo).
- **Rectificación.** Dar vuelta el semiciclo negativo (|x|). La onda se repite el doble de rápido: **la fundamental
  desaparece y suena una octava arriba**. Riptide quita la DC *antes* de saturar (si saturara primero, con mucho drive
  la onda sería casi cuadrada, y una cuadrada rectificada es una línea recta: silencio). Es la idea de los pedales de
  octava fuzz de los 60–70.
- **Wavefolding.** En vez de aplanar los picos, la curva (sin(x)) los **dobla hacia abajo**. Con más drive se doblan
  otra vez y aparecen picos nuevos dentro de cada ciclo. El timbre se vuelve metálico y cambia todo el tiempo con el
  nivel. Es el corazón de la síntesis "West Coast" (Buchla).
- **Cambiar de modo sin clic.** Riptide calcula el modo nuevo en paralelo, en silencio, mientras su oversampling se
  llena, y después cruza los dos en 20 ms. Puedes cambiar de modo mientras suena (y automatizarlo).

### Qué hace cada control

- **Mode** (los seis botones; la descripción del activo aparece debajo):
  - **Tape**: el más sutil. Redondea, comprime, suaviza agudos y suma cuerpo en 70 Hz. En el analizador: 3er armónico
    discreto y agudos que bajan un poco al subir el drive.
  - **Tube**: engorda. 2º y 3er armónico; el 2º es el protagonista. Con drive alto "ronca" y el sonido se vuelve más
    grueso que brillante.
  - **Transistor**: limpio hasta que muerde. Armónicos impares altos (5º, 7º...): presencia, filo, aspereza.
  - **Hard Clip**: no cambia nada mientras la señal (después de Input) no pase de 0 dBFS; cuando pasa, recorta en seco.
    Sirve para controlar picos; con mucho drive suena a onda cuadrada y es el modo con más aliasing (oversampling 8x o
    16x si suena "digital").
  - **Rectify**: efecto extremo. Octava arriba, sin fundamental, sonido de fuzz de octava. Un bajo pierde su nota real.
  - **Fold**: con poco drive parece una saturación suave; cuando el pico pasa de ~+4 dBFS (después de Input) empieza a
    doblarse y el timbre cambia radicalmente. Mueve Input lentamente: el timbre "barre" como un filtro.
- **Input / Output / Oversampling / Bypass**: igual que en la Fase 2. Ahora el Input decide *en qué parte de la rodilla*
  de cada modo trabajas.
- En la forma de onda (Wave Candy, oscilloscope) con un seno: Tape = seno con hombros redondos; Tube = un semiciclo
  más aplastado que el otro; Transistor = casi cuadrada con esquinas suaves; Hard Clip = cuadrada con esquinas;
  Rectify = "jorobas" al doble de velocidad; Fold = el pico se hunde y forma una "M".

### Ejercicio de escucha guiado: el mismo drive, seis caracteres

1. Un bajo sostenido (por ejemplo 3x Osc en sierra, **La2 (55 Hz)** y **La3 (110 Hz)**) a un insert con Riptide, y
   después un **Fruity Parametric EQ 2** para ver el espectro. Oversampling 4x.
2. Ajusta la fuente para que IN marque unos **−12 dBFS RMS**. Riptide en **Tape, Input +9 dB**. Iguala el volumen con
   Output (mismo RMS en OUT con y sin bypass). Anota el Output que necesitaste.
3. Recorre **Tube, Transistor, Hard Clip** con el mismo Input. Para cada uno iguala con Output y alterna con bypass.
   Qué notar: Tube suena más grueso, Transistor más brillante y con filo, Hard Clip más "cuadrado". En el analizador,
   de esos tres Tube es el único con un pico fuerte en 2× (110 Hz si tocas La2).
4. Baja **Input a 0 dB**: Tape aún se nota un poco (head bump y redondeo), Transistor y Hard Clip suenan casi idénticos
   al bypass. Esa es la diferencia de rodilla.
5. Sube **Input** en **Fold** y muévelo despacio entre +6 y +24 dB: el timbre se transforma con cada pliegue.
6. Pon **Rectify** y toca una melodía: suena una octava arriba. En el analizador, la fundamental casi desaparece.
7. Cambia de modo mientras suena: no hay clics.

### Receta de uso: calidez en una voz con Tube

1. Riptide en el insert de la voz, **Tube**, Oversampling 4x.
2. Que la voz llegue a IN con unos −18 dBFS RMS. Sube **Input de +3 a +6 dB** (poco: la voz es lo más expuesto de la
   mezcla).
3. Iguala con **Output** y alterna el bypass.
4. Qué notar: la voz suena más "cerca" y gruesa, las consonantes no se vuelven ásperas. Si suena sucia en las notas
   fuertes, baja Input.
5. ¿Por qué Tube? El 2º armónico es una octava exacta de cada nota: engorda sin añadir notas "extrañas". Los impares
   de Transistor añadirían filo, que en una voz se oye rápido como aspereza.
6. Variante: **Tape** con +6 a +9 dB en un bus de batería para "pegarla" y suavizar los platillos.

### Reto sin receta

Tienes un **loop de batería con hi-hats muy brillantes y agresivos** y quieres que el loop suene más denso **sin que
los hi-hats se vuelvan más molestos** (al contrario: un poco más suaves), con el volumen igualado. Elige el modo y el
drive. Pista de partida: piensa qué modo trata distinto a los agudos y a los graves. Inténtalo y después pide pistas.

### Vocabulario

- **Knee (rodilla)**: el tramo donde la curva deja de ser recta. *En música:* también existe en los compresores
  ("soft knee" = compresión gradual).
- **Armónicos pares**: 2×, 4×, 6× la fundamental. *En música:* la calidez de un preamplificador de válvulas en una
  voz de soul de los 60; el bajo "gordo" pasado por un amplificador a válvulas.
- **Saturación de cinta**: compresión suave + suavizado de agudos + cuerpo en graves. *En música:* el sonido de los
  discos de rock de los 70 (Fleetwood Mac, Led Zeppelin) y de muchas baterías "pegadas".
- **Pre-énfasis / de-énfasis**: subir una zona de frecuencias antes de un proceso y bajarla después. *En música:* las
  grabadoras de cinta y los discos de vinilo (curva RIAA).
- **Head bump**: resonancia de graves de la cinta. *En música:* el bombo "redondo" de muchas grabaciones analógicas.
- **Clip duro**: corte en seco al llegar al límite. *En música:* los clippers que usan los productores de trap y
  drill para que el 808 y el bombo suenen fuertes sin subir el pico.
- **Rectificador / octava fuzz**: dar vuelta media onda para doblar la frecuencia. *En música:* el pedal Octavia de
  Jimi Hendrix ("Purple Haze").
- **Wavefolder**: dobla los picos en vez de aplanarlos. *En música:* sintetizadores Buchla y módulos de Eurorack;
  bajos "growl" en dubstep.

### Errores comunes

- **Elegir el modo por el volumen**: algunos modos suben más el RMS que otros. Iguala Output en cada modo antes de
  decidir.
- **Tube con mucho drive y sin mirar OUT**: el semiciclo negativo pasa de 0 dBFS. Si el LED rojo se enciende, baja
  Output.
- **Hard Clip con Oversampling Off**: el modo con más aliasing de todos. Usa 4x como mínimo, 8x si hay agudos fuertes.
- **Rectify o Fold en un bajo que tiene que llevar la nota**: la fundamental desaparece o se transforma y el bajo deja
  de sostener la armonía. Úsalos en sonidos de efecto o en una capa paralela (el Mix llega en la Fase 5).
- **Tape en todo "porque es cálido"**: el head bump suma graves en cada pista; en 20 pistas, los graves se acumulan.
- **Cuándo NO usar cada modo**: Transistor en voces suaves y acústicas (filo innecesario); Tube en material que ya es
  grueso y embarrado (más medios-graves); Tape cuando necesitas transientes con mucho ataque.

### Retos completados

(Ninguno todavía.)

## Fase 4 — Tono: qué se satura y cómo suena lo que sale

Hasta ahora todo el espectro entraba igual a la curva. Un saturador profesional te deja decidir **qué frecuencias
saturan** y **qué color tiene el resultado**. La clave de esta fase es una sola idea: la saturación no es lineal, así
que **un mismo filtro suena distinto antes o después de la curva**.

### Conceptos aprendidos

- **El orden importa (procesos no lineales).** Un EQ seguido de otro EQ da lo mismo en cualquier orden. Un EQ y un
  saturador, no. Lo que va **antes** (PRE) decide qué llega a la curva y con cuánto nivel, o sea, qué se satura. Lo que
  va **después** (POST) solo cambia el color de lo que sale, incluidos los armónicos que la curva acaba de crear.
- **Intermodulación.** Cuando dos frecuencias saturan juntas, no solo aparecen sus armónicos: aparecen también tonos
  de **suma y diferencia** (100 Hz y 1 kHz → 900 Hz, 1100 Hz...). No son múltiplos de ninguna nota: suenan a "barro",
  aspereza, desafinación. El caso clásico: un bajo fuerte que satura junto con una voz o una guitarra y la ensucia.
  Un Low Cut **PRE** evita que los graves participen en la saturación.
- **Low Cut / High Cut (high-pass / low-pass).** Filtros de **12 dB/oct**: en el corte la señal baja 3 dB; una octava
  más allá, 12 dB; dos octavas, 24 dB. En el extremo de la perilla se apagan ("Off"): la señal pasa intacta.
- **PRE vs POST en la práctica.**
  - Low Cut **PRE**: el bajo deja de empujar la curva (más claridad, menos intermodulación). OJO: también se quita de
    la salida. Hasta la Fase 5 (mix) no hay forma de "saturar sin graves y recuperarlos limpios".
  - Low Cut **POST**: el bajo satura con todo y después se le quita la fundamental, pero sus **armónicos se quedan**.
    Sirve para capas de efecto o para quitar barro después de saturar.
  - High Cut **POST**: el uso más común. Suaviza el **fizz** (el chisporroteo agudo que crea la saturación).
  - High Cut **PRE**: los agudos no entran a la curva, así que no se ensucian. Pero los graves y medios crean sus
    armónicos DESPUÉS del filtro: siguen apareciendo agudos nuevos.
- **Emphasis / de-emphasis (a la carta).** Es el truco de la cinta de la Fase 3, pero eliges zona y cantidad: una
  campana ancha sube esa zona **antes** de la curva y su inversa exacta la baja **después**. Sin drive se anulan
  (respuesta plana: compruébalo con el bypass a nivel bajo). Con drive, la zona realzada llega más fuerte a la curva y
  es la que **más satura**; con emphasis **negativo**, esa zona llega más baja y queda **más limpia** que el resto.
  Medido en Riptide (Transistor, +6 dB): +12 dB de emphasis en 2 kHz triplica el 3er armónico de un tono de 2 kHz;
  −12 dB lo baja a menos de la mitad; un tono de 100 Hz casi no se entera.
- **Tilt.** Inclina todo el espectro de la salida alrededor de 1 kHz: +3 dB = agudos +3 dB y graves −3 dB (1 kHz no
  cambia). Es un "control de tono" muy suave, como el de un amplificador de guitarra o de un equipo de música: cambia
  el color general sin dibujar picos ni valles.
- **El medidor IN ahora ve el tono PRE.** IN mide justo antes de la curva: si subes el emphasis o pones un corte PRE,
  IN cambia aunque la salida (sin drive) no cambie. Es lo que la curva "ve".

### Qué hace cada control

- **Low Cut** (20 Hz = Off ... 2 kHz) y su botón **PRE/POST** (por defecto PRE). En el analizador: en PRE desaparecen a
  la vez los graves y los armónicos que creaban; en POST desaparecen los graves pero los armónicos medios siguen ahí.
- **High Cut** (500 Hz ... 20 kHz = Off) y su botón (por defecto POST). En POST el espectro cae por encima del corte
  (adiós fizz); en PRE siguen apareciendo armónicos por encima del corte (la curva los crea después).
- **Emphasis** (−12 ... +12 dB) y **Emph Freq** (50 Hz ... 10 kHz). Con Input en 0 dB y señal suave no oyes nada (es lo
  correcto). Sube Input y mueve Emph Freq: la "suciedad" se mueve por el espectro, como un filtro que barre, pero de
  saturación en vez de volumen.
- **Tilt** (−6 ... +6 dB): más brillante (positivo) o más oscuro y cálido (negativo). Va al final: no cambia qué se
  satura.
- Doble clic en cualquier perilla = su valor por defecto (cortes en Off, emphasis y tilt en 0 dB).
- En la forma de onda: un Low Cut PRE con un bajo saturado deja la onda casi senoidal (no llega a recortar); en POST
  la onda se ve recortada pero "flotando" (sin la fundamental).

### Ejercicio de escucha guiado: PRE contra POST (con volumen igualado)

1. Un bajo sostenido (3x Osc en sierra, **La2 (55 Hz)**) o un loop de bajo en un insert con Riptide, y un **Fruity
   Parametric EQ 2** después para ver el espectro. **Tube**, Oversampling 4x, **Input +12 dB**.
2. Iguala con Output contra el bypass. Escucha y mira: graves fuertes y armónicos hasta los medios.
3. **Low Cut en 300 Hz, PRE.** Iguala con Output (vas a tener que subirlo bastante: se fueron los graves). Qué notar:
   suena fino y casi limpio, porque lo que queda llega con poco nivel a la curva.
4. Pulsa **POST** (vuelve a igualar). Qué notar: también fino, pero ahora áspero y "gritón": son los armónicos que creó
   el bajo saturado, sin su fundamental. Alterna PRE/POST varias veces: misma perilla, sonido opuesto.
5. Low Cut en Off. **High Cut en 3 kHz, POST**, y después **PRE**. En POST el fizz desaparece y queda un sonido
   redondo; en PRE vuelven a aparecer agudos (mira el analizador por encima de 3 kHz).
6. High Cut en Off. **Emphasis +9 dB** y barre **Emph Freq** despacio de 80 Hz a 5 kHz. Qué notar: la zona que se
   "rompe" se mueve. Después pon **−9 dB en 80 Hz**: el bajo sigue fuerte pero más limpio y firme, y la suciedad queda
   en los medios.
7. Al final, **Tilt** de −4 a +4 dB: solo cambia el brillo; la saturación no se mueve.

### Receta de uso: un 808 que se oye en bocinas pequeñas (celular, laptop)

1. Riptide en el insert del 808. **Tube** (armónicos pares: octavas que refuerzan la nota), Oversampling 4x.
2. **Emphasis +6 a +9 dB en unos 100 Hz**: el cuerpo del 808 es lo que más satura. Sus armónicos (2×, 3×, 4×: de 90 a
   180 Hz para un 808 en **Fa2 (44 Hz)**, y más arriba) son justo lo que una bocina pequeña sí puede reproducir.
3. **Input +6 a +12 dB** hasta que la nota se entienda a volumen bajo. **High Cut POST en 5–8 kHz** para quitar el fizz,
   que no le aporta nada a un bajo.
4. Iguala con Output, alterna el bypass y prueba en audífonos baratos o en la bocina de la laptop.
5. ¿Por qué funciona? El oído "reconstruye" una fundamental que no oye si escucha sus armónicos (la **fundamental
   ausente**). El emphasis hace que la saturación caiga sobre el cuerpo del 808 y no sobre su clic, que ya es brillante.
6. Variante para voz: **Low Cut PRE en 120–150 Hz** (que los golpes de micrófono y la respiración grave no saturen),
   **Tape** o **Transistor** con +3 a +6 dB, **Emphasis +4 dB en 3 kHz** (presencia) y **High Cut POST en 12 kHz**.

### Reto sin receta

Tienes un **bajo eléctrico** que quieres hacer **gruñir en los medios** (que se oiga la "mugre" de las cuerdas), pero el
**sub tiene que seguir limpio y firme**, con el mismo volumen que sin Riptide. Ojo: un Low Cut PRE no sirve, porque se
llevaría el sub de la salida. Inténtalo y después pide pistas.

### Vocabulario

- **PRE / POST**: antes o después del proceso. *En música:* el Tube Screamer recorta graves antes de distorsionar; por
  eso la guitarra de Stevie Ray Vaughan suena clara y no embarrada.
- **Intermodulación**: tonos de suma y diferencia entre frecuencias que saturan juntas. *En música:* por qué un acorde
  completo con mucha distorsión suena áspero, y los guitarristas de metal tocan "power chords" (quinta y octava).
- **Low Cut / High Cut**: filtros que quitan graves / agudos (high-pass / low-pass). *En música:* el "efecto teléfono"
  en voces (los dos cortes muy cerrados), típico en intros de canciones pop y en coros de hip-hop.
- **Pendiente (dB/oct)**: cuánto baja un filtro por cada octava pasado el corte. 12 dB/oct es suave y musical.
- **Fizz**: el chisporroteo agudo de la saturación. *En música:* una guitarra distorsionada grabada directo, sin el
  filtro natural del altavoz y el gabinete.
- **Emphasis / de-emphasis**: realzar una zona antes de un proceso y devolverla después. *En música:* las cintas de
  estudio, los discos de vinilo (curva RIAA) y la radio FM.
- **Tilt EQ**: inclinación del espectro alrededor de un pivote. *En música:* el control "Tone" de muchos pedales y
  amplificadores, y los EQ de mastering con control de tilt.
- **Fundamental ausente**: el oído percibe la nota aunque falte su fundamental, si oye sus armónicos. *En música:* por
  qué un 808 saturado "se oye" en un celular que físicamente no reproduce 45 Hz.

### Errores comunes

- **Low Cut PRE "para limpiar" y perder el bajo**: el corte PRE también quita esos graves de la salida. Si quieres
  graves limpios y medios sucios, la herramienta es el **emphasis negativo** en los graves (o el Mix de la Fase 5).
- **Juzgar el emphasis sin igualar el volumen**: con drive, subir el emphasis sube el nivel. Iguala con Output antes de
  decidir.
- **High Cut POST demasiado bajo**: el sonido queda opaco y se va al fondo de la mezcla. Bájalo hasta que el fizz
  desaparezca, y no más.
- **Emphasis +12 dB en agudos con Hard Clip y oversampling bajo**: agudos fuertes + clip duro = aliasing. Sube el
  oversampling a 8x.
- **Usar el tilt como EQ correctivo**: el tilt es para el color general. Un problema en una frecuencia concreta (una
  resonancia, un "honk") es trabajo de un EQ paramétrico (el plugin 3 de la suite).
- **Cuándo NO usar el tono de Riptide**: si solo quieres ecualizar sin saturar, usa un EQ: es más preciso y no le
  agregas al canal la latencia del oversampling.

### Retos completados

(Ninguno todavía.)
