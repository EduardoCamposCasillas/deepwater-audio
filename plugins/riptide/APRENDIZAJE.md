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

## Fase 5 — Mezcla en paralelo, auto gain y delta

Hasta ahora Riptide reemplazaba la señal por su versión saturada. Los ingenieros casi nunca trabajan así: mezclan la
saturada **con** la original (procesamiento en paralelo), comparan siempre al mismo volumen, y de vez en cuando escuchan
**solo lo que el proceso agrega** para saber qué están haciendo. Esta fase trae las tres herramientas.

### Conceptos aprendidos

- **Procesamiento en paralelo (dry/wet).** Mix = cuánto de la señal saturada (wet) se suma a la original (dry). Con
  50 % oyes las dos a la vez: la original conserva sus **transientes** (la pegada del golpe, el ataque de la nota) y la
  saturada aporta **densidad** y armónicos. Es lo que en un estudio se hace mandando la pista a un bus aparte (la
  "compresión Nueva York", pero con saturación).
- **Alineación y filtro de peine.** Sumar dos copias de la misma señal solo funciona si llegan **alineadas**. Si una va
  retrasada aunque sea 1 ms, unas frecuencias se suman y otras se cancelan: el espectro se llena de "dientes" (filtro de
  peine) y suena hueco, metálico, como un flanger quieto. Riptide retrasa la dry exactamente la latencia del
  oversampling y la pasa por un filtro DC igual al de la wet (que mueve un poco la fase de los graves). Resultado: sin
  drive, wet y dry son idénticas y el delta da silencio.
- **El sesgo del volumen (otra vez).** Desde la Fase 1: lo más fuerte parece mejor. Subir el Input sube el volumen, y
  el oído te engaña. **Auto gain** compensa solo: mover Input o el modo cambia el carácter, no el volumen.
- **Cómo funciona este auto gain.** No mide "entrada contra salida" (eso da saltos de volumen al bajar el drive y
  bombea con la batería). Riptide *sabe* qué curva usa cada modo, así que **calcula** cuánto volumen pierde la señal al
  pasar por ella según su nivel, y solo mide lentamente (1 s) el nivel de la música. Por eso reacciona al instante cuando
  mueves Input y no bombea. Medido: con un tono puro queda a ±0.1 dB; con ruido rosa (parecido a una mezcla) y mucho
  drive, hasta 2.4 dB por debajo. A propósito queda igual o más bajo, nunca más fuerte: si algo te suena mejor con auto
  gain puesto, no es por estar más fuerte. La lectura debajo del botón muestra la compensación (tenue cuando está
  apagado: lo que *haría*).
- **Delta (escuchar la diferencia).** Delta = salida − original. Lo que oyes es exactamente lo que Riptide le agrega
  (armónicos, fizz, barro) o le quita (lo que la curva aplastó). Sin drive, silencio. Es la misma idea que el "null
  test" de la librería: restar dos señales para ver en qué se diferencian.
- **Por qué el auto gain va antes del mix.** Si la wet saliera 8 dB más fuerte que la dry, el mix al 30 % sonaría casi
  como el 100 %. Igualadas, el mix cambia **la proporción de carácter**, no el volumen.

### Qué hace cada control

- **Mix** (0–100 %, por defecto 100). 0 % = la original (sin saturar); 100 % = solo la saturada. En la forma de onda:
  al bajar el mix vuelven a asomar los picos que la curva había aplastado (más pegada). En el analizador: los armónicos
  bajan de nivel, pero siguen ahí.
- **AUTO GAIN** (por defecto apagado) y su lectura en dB. Con el botón puesto, el medidor OUT se queda cerca de donde
  estaba sin drive. Con el botón apagado, sube Input mirando la lectura: ves cuánto está cambiando el volumen.
- **DELTA** (por defecto apagado). La salida pasa a ser solo la diferencia. Con mix al 50 % el delta es la mitad (lo que
  realmente agrega esa mezcla). Output también afecta al delta: súbelo para escuchar detalles.
- **Output** sigue siendo el último paso: ajusta todo (dry + wet, o el delta).
- Los tres controles cambian sin clics (rampas y fundidos de 20 ms) y el bypass sigue siendo la original intacta.

### Ejercicio de escucha guiado: lo que agrega cada modo (con volumen igualado)

1. Un loop de batería completo en un insert con Riptide. **AUTO GAIN puesto**, **Hard Clip**, Oversampling 4x.
2. Sube Input de 0 a +12 dB. Qué notar: el volumen casi no cambia (mira OUT), pero la batería se vuelve más densa y
   pierde pegada. Alterna con el bypass: por fin una comparación justa.
3. Pon **DELTA**. Qué notar: con Input en +3 dB solo oyes "chasquidos" en los golpes más fuertes (lo único que el clip
   toca). Sube a +12 dB: ahora el delta tiene cuerpo, un fizz continuo. Eso es lo que le estás agregando a la mezcla.
4. Con DELTA puesto, cambia de modo: **Tape** (un delta suave, sobre todo agudos de los platillos y algo de grave por el
   head bump), **Tube** (aparecen armónicos pares, más "llenos"), **Fold** (metálico). Quita DELTA.
5. **Mix al 40 %**, Hard Clip, Input +18 dB. Alterna con Mix al 100 %. Qué notar: al 40 % vuelven los transientes,
   pero la densidad se queda. Así suena la saturación en paralelo.
6. Con mix al 40 %, apaga y enciende **AUTO GAIN**: sin él la wet domina (está más fuerte); con él la mezcla es pareja.

### Receta de uso: saturación en paralelo en un bus de batería

1. Riptide en el bus de la batería. **Transistor** o **Hard Clip**, Oversampling 4x, **AUTO GAIN puesto**.
2. **Input +15 a +20 dB**: la wet tiene que sonar claramente destrozada (escúchala sola con Mix al 100 %).
3. **High Cut POST en 6–8 kHz** y **Low Cut PRE en 80–100 Hz**: la wet aporta medios (el "crunch" de la caja y el cuerpo
   de los toms) sin fizz y sin que el bombo ensucie la curva.
4. Baja el **Mix** hasta que la batería suene más grande y llena pero los golpes sigan "pegando": normalmente entre
   20 y 40 %. Verifica con el bypass.
5. ¿Por qué funciona? La dry conserva los transientes intactos (el oído los usa para el ritmo y la pegada); la wet
   rellena entre golpe y golpe (más energía media: la batería se oye más fuerte sin subir los picos). Con el Low Cut PRE
   en la wet, el bombo no pierde graves: su sub llega limpio por la dry.
6. Variante para un bajo: **Tube**, Input +12 dB, Low Cut PRE en 200 Hz, Mix 50 %: el sub llega limpio por la dry y la
   wet suma el gruñido de los medios (es una pista para el reto de la Fase 4). Si suena un poco hueco justo alrededor del
   corte es normal: el Low Cut desfasa esa zona de la wet respecto de la dry. Mueve el corte o baja un poco el mix.

### Reto sin receta

Tienes una **voz principal** que se pierde detrás de las guitarras en el estribillo. Quieres que **gane presencia y
se entienda**, pero sin que suene distorsionada ni más fuerte (compruébalo con AUTO GAIN y el bypass), y sin que las
**eses y las "t"** se vuelvan ásperas. Usa el DELTA para revisar qué estás agregando. Inténtalo y después pide pistas.

### Vocabulario

- **Dry / wet**: la señal sin procesar / la procesada. *En música:* los controles "Mix" de casi todos los efectos; una
  reverb al 100 % wet en una voz es el efecto "voz lejana" de muchas intros.
- **Procesamiento en paralelo**: mezclar la señal procesada con la original. *En música:* la "compresión Nueva York"
  de las baterías de rock y pop: golpes con pegada y a la vez un cuerpo enorme.
- **Filtro de peine (comb filtering)**: cancelaciones periódicas al sumar dos copias desalineadas de una señal. *En
  música:* el flanger y el phaser (a propósito), o dos micrófonos mal colocados en una batería (sin querer).
- **Coherencia de fase**: que dos señales lleguen con la misma fase y se sumen sin cancelarse. *En música:* el botón de
  "invertir fase" de las consolas, para los micrófonos de arriba y de abajo de una caja.
- **Gain matching / auto gain**: igualar el volumen para comparar con justicia. *En música:* los ingenieros de mastering
  comparan siempre así; las plataformas (Spotify, YouTube) normalizan el volumen, así que "más fuerte" no gana.
- **Delta / null test**: restar dos señales para oír la diferencia. *En música:* el truco de invertir la fase de la
  versión instrumental contra la canción completa para aislar la voz.

### Errores comunes

- **Dejar DELTA puesto y exportar.** El delta es para escuchar, no para mezclar. Si una pista suena como un fantasma,
  revisa que DELTA esté apagado.
- **Juzgar el mix sin AUTO GAIN.** Sin compensar, más wet suele ser más fuerte, y el oído siempre elige más. Pon AUTO
  GAIN (o iguala con Output) antes de decidir el porcentaje.
- **Confiar ciegamente en el auto gain.** Es una aproximación: con mucho drive queda hasta un par de dB por debajo (con
  Rectify y un tono puro, casi 3 dB). Para la decisión final, iguala de oído con Output contra el bypass.
- **Mix bajo para "arreglar" una saturación mal elegida.** Si la wet sola suena mal (fizz, barro), mezclarla al 20 %
  agrega un poco de eso mismo. Primero haz que la wet suene bien (modo, tono) y después baja el mix.
- **Paralelo en todo.** En un master o en un bus ya muy denso, la saturación en paralelo puede sumar volumen y fase
  rara sin aportar nada. Si con DELTA casi no oyes nada útil, quítalo.
- **Cuándo NO usar el paralelo:** cuando quieres justamente **domar** los picos (un clip para ganar headroom antes de
  un limitador): ahí la dry devuelve los picos que querías quitar. Usa Mix al 100 %.

### Retos completados

(Ninguno todavía.)

## Fase 6 — Multibanda: saturar cada zona del espectro por separado

Hasta ahora toda la señal pasaba junta por la curva. El problema: **la parte más fuerte manda**. En casi cualquier
mezcla esa parte son los graves (bombo, bajo, 808): ellos empujan la curva, y todo lo que suena encima se satura al
ritmo de ellos y se ensucia con su intermodulación. Un saturador multibanda parte la señal en bandas (graves, medios,
agudos...) y satura cada una con su propio modo y drive, como si tuvieras cuatro Riptides en paralelo, cada uno
dedicado a una zona.

### Conceptos aprendidos

- **Crossover (divisor de bandas).** Un par de filtros que reparten la señal: un low-pass se queda con lo de abajo del
  corte y un high-pass con lo de arriba. Con 4 bandas hay 3 cortes (Riptide los llama **cruces**). La condición de un
  buen crossover: si no se procesa nada, **la suma de las bandas tiene que sonar igual que la entrada**.
- **Por qué no sirve cualquier par de filtros.** Alrededor del corte, el low-pass y el high-pass dejan pasar las dos
  algo de señal, pero cada uno la retrasa distinto (su **fase**). Al sumarlas no se suman "derechas": con dos filtros
  Butterworth simples aparece un pozo (o una joroba de +3 dB) justo en el corte.
- **Linkwitz-Riley (LR4, 24 dB/oct).** Es la solución clásica (de los crossovers de cajas acústicas): dos Butterworth en
  cascada en cada lado. Cada banda vale **−6 dB en el corte** y las dos llegan **en fase** en todas las frecuencias, así
  que su suma es **plana**: 0 dB en todo el espectro. Lo único que queda es un giro de fase alrededor de cada corte (un
  **allpass**: deja pasar todo con el mismo volumen, solo cambia la fase), y un giro de fase lento como ese no se oye
  como color. Medido en Riptide: la suma de 2, 3 o 4 bandas sin saturar se aparta menos de 0.001 dB de la entrada.
- **La original del mix también se divide.** Por ese giro de fase, la señal multibanda ya no es idéntica a la original:
  si la sumaras directo con la dry (Mix < 100 %) volvería el filtro de peine de la Fase 5 alrededor de los cruces. Por
  eso la dry pasa por un **divisor gemelo**: las dos llegan con la misma fase y el delta sigue dando silencio sin drive.
- **Pendiente y solapamiento.** 24 dB/oct significa que una banda no termina de golpe en el cruce: una octava más allá
  todavía queda algo (−24 dB). Por eso Riptide no deja dos cruces a menos de **media octava**: si están más juntos, la
  banda del medio casi no tiene nada propio. Si mueves una perilla de cruce más allá de su vecina, la **empuja** (no se
  intercambian); lo que de verdad suena se lee debajo del nombre de cada banda.
- **Intermodulación, otra vez.** En la Fase 4 viste que un bajo fuerte que satura junto con una voz la ensucia (tonos de
  suma y diferencia). Separando bandas, el bajo y la voz ya no saturan **juntos**: cada banda solo intermodula consigo
  misma. Es la razón principal para usar multibanda en buses y en el master.
- **Auto gain por banda.** Cada banda mide su propio nivel y compensa su propio drive y su modo. Subir el drive de los
  medios cambia el carácter de los medios sin que suban de volumen respecto de los graves y los agudos.
- **CPU.** Cada banda es un saturador completo con su oversampling: 4 bandas a 16x cuestan unas 4 veces lo de 1 banda.
  Si el proyecto va justo, baja el oversampling o usa menos bandas.

### Qué hace cada control

- **BANDAS 1 2 3 4** (por defecto 1). Con 1, Riptide es exactamente el de la Fase 5 (comprobado bit a bit). Cambiar el
  número de bandas hace un fundido corto a silencio (10 ms de bajada y 10 de subida), como cambiar el oversampling: es
  un ajuste para elegir, no para automatizar.
- **Cruce 1, 2, 3** (20 Hz–16 kHz; por defecto 120 Hz, 1 kHz y 5 kHz). Entre cada par de bandas. Con 2 bandas solo
  cuenta el Cruce 1 (120 Hz: sub/graves contra todo lo demás); con 4: graves | medios graves | medios agudos | agudos.
  Se pueden mover con música sin clics (se suavizan en 20 ms).
- Por banda (de izquierda a derecha, de grave a agudo):
  - **Modo**: *Global* (el de los botones de arriba, por defecto) o uno propio. Graves en Tube y agudos en Tape suena
    muy distinto a todo en Tube.
  - **Drive** (−24 a +24 dB, por defecto 0): se suma al **Input**. Input sigue siendo el drive general; el de cada banda
    lo ajusta. Negativo = esa banda queda más limpia que el resto.
  - **S (solo)**: escuchar solo esa banda (varias en solo = suenan todas ellas). El solo gana al mute.
  - **M (mute)**: quitar esa banda de la salida.
  - **auto ±x dB**: la compensación del auto gain de esa banda (en color si AUTO GAIN está puesto).
- Solo y mute actúan también sobre la dry del mix y sobre el **DELTA**: con una banda en solo y DELTA puesto oyes
  exactamente lo que la saturación le agrega a esa banda.
- En el analizador: con una banda en solo ves su "montaña" con faldas de 24 dB/oct a cada lado del cruce. Con drive en
  una sola banda, los armónicos nuevos aparecen **encima** de esa banda (la saturación crea armónicos hacia arriba: los
  de los graves caen en los medios), no debajo.

### Ejercicio de escucha guiado: una sola saturación contra tres (con volumen igualado)

1. Un loop de batería completo (bombo, caja, platillos) en un insert con Riptide y un **Fruity Parametric EQ 2** después
   para ver el espectro. **AUTO GAIN puesto**, **Transistor**, Oversampling 4x, **1 banda**.
2. **Input +15 dB.** Qué notar: cada golpe de bombo "aplasta" todo: la caja y los platillos se ensucian y respiran al
   ritmo del bombo (la curva está ocupada con él).
3. Pasa a **3 bandas**, Cruce 1 en **150 Hz** y Cruce 2 en **2.5 kHz**. Mismo Input. Qué notar: el bombo sigue saturado
   pero los platillos ya no "bombean" con él, y la caja suena más clara. Es la intermodulación que desapareció.
4. Pon la **banda 1 en solo** y mueve el Cruce 1 entre 80 y 250 Hz: escucha dónde está el "golpe" del bombo y dónde
   empieza el cuerpo de la caja. Quita el solo.
5. **Drive de la banda 3 en −12 dB**: los platillos quedan limpios y el resto, saturado. Después **+6 dB**: el fizz
   vuelve, solo en los agudos. Pon DELTA un momento para escuchar qué agrega cada banda (prueba con solo + DELTA).
6. Banda 1 en **Tube**, banda 2 en **Global** (Transistor), banda 3 en **Tape**. Qué notar: el bombo gana cuerpo (pares),
   la caja tiene mordida y los platillos se redondean. Tres saturadores distintos en uno.
7. Vuelve a **1 banda** y compara: así de diferente es "toda junta" contra "por zonas".

### Receta de uso: un bus de batería grande sin platillos ásperos

1. Riptide en el bus de la batería. **3 bandas**, **AUTO GAIN puesto**, Oversampling 4x.
2. **Cruce 1 en 120–150 Hz** (debajo queda el bombo y el sub), **Cruce 2 en 3–5 kHz** (encima, platillos y aire).
3. **Banda 1: Tube, drive +6 dB.** Pares en el bombo: más redondo y más "nota", sin perder el golpe.
4. **Banda 2: Transistor (o Hard Clip), drive +9 a +12 dB.** Aquí vive la pegada de la caja y los toms: la mordida en
   los medios hace que la batería se oiga más grande y más cerca.
5. **Banda 3: Tape, drive −6 dB.** Los platillos casi limpios y redondeados: nada de fizz.
6. **Input** para el drive general y **Mix** al 60–100 % según cuánta pegada original quieras conservar. Iguala con
   Output y compara con el bypass.
7. ¿Por qué funciona? Cada zona recibe la saturación que la favorece: los graves, armónicos que refuerzan la nota; los
   medios, densidad y presencia; los agudos, nada (ya tienen todos los armónicos que necesitan, y saturarlos solo agrega
   aspereza). Y ninguna zona ensucia a las otras.
8. Variante para un **bajo**: 2 bandas, Cruce 1 en 100–150 Hz, banda 1 con drive −12 dB (sub limpio y firme) y banda 2
   con Tube +12 dB (el gruñido de las cuerdas). Es la solución "de manual" al reto de la Fase 4 (allá no había bandas;
   aquí sí, y sin el hueco del Low Cut en paralelo).

### Reto sin receta

Tienes la **mezcla completa** de un beat (el 2-pista, en el master) y quieres que suene **más caliente y pegada**, como
si hubiera pasado por una consola o una cinta, **sin que el bombo haga respirar al resto** y **sin que las voces y los
hi-hats se vuelvan ásperos**. Tiene que ser sutil: con AUTO GAIN y el bypass, la diferencia se tiene que notar en la
densidad, no en el volumen. Inténtalo y después pide pistas.

### Vocabulario

- **Crossover / cruce**: el punto (y los filtros) donde se separan dos bandas. *En música:* las cajas acústicas de dos
  vías tienen uno entre el woofer y el tweeter (suele estar entre 2 y 3 kHz).
- **Saturación (o compresión) multibanda**: procesar cada zona del espectro por separado. *En música:* el sonido "grande
  pero claro" de los masters de EDM y pop moderno; los compresores multibanda de la radio FM.
- **Linkwitz-Riley**: el tipo de crossover cuya suma es plana (24 dB/oct, −6 dB en el corte). *En música:* es el estándar
  de los crossovers de sistemas de sonido en vivo.
- **Allpass / giro de fase**: un filtro que no cambia el volumen de ninguna frecuencia, solo su fase. *En música:* es el
  corazón del phaser (varios allpass que se mueven, sumados con la señal original).
- **Banda**: la zona del espectro entre dos cruces. *En música:* "la banda de los 200 a 500 Hz" es donde suele vivir el
  barro de una mezcla.
- **Solo / mute por banda**: escuchar o quitar una zona. *En música:* así se busca en mastering en qué zona está un
  problema antes de corregirlo.

### Errores comunes

- **Cuatro bandas "por si acaso".** Más bandas = más cruces (más giro de fase), más CPU y más decisiones. Empieza con 2
  o 3 y agrega una solo si una zona necesita algo distinto.
- **Un cruce justo en el centro de un instrumento importante.** Si la voz tiene su cuerpo en 1 kHz y pones ahí un cruce
  con bandas muy distintas, cada mitad de la voz satura distinto y suena partida. Pon los cruces entre instrumentos, no
  en medio de uno (usa el solo para encontrarlos).
- **Drives muy distintos en bandas vecinas.** Con mucha diferencia (+12 en una, −12 en la otra) la zona del cruce, que
  comparten las dos, queda con un hueco o una joroba de volumen. Si lo oyes, acerca los drives o mueve el cruce.
- **Olvidar un solo o un mute puesto.** Si "falta" algo en la mezcla, revisa los botones S y M. Con un solo puesto el
  resto de las bandas no suena, tampoco en la dry.
- **Usar las bandas como EQ.** El drive de una banda cambia la saturación (y, sin AUTO GAIN, también el volumen), pero
  no es un ecualizador: para subir o bajar una zona sin saturar, usa un EQ.
- **Cuándo NO usar multibanda:** en una pista sola de un instrumento simple (un bajo limpio, una voz) suele bastar con 1
  banda y el tono de la Fase 4; y en sonidos donde quieres justamente que todo sature junto (un bajo de rock, un
  sintetizador agresivo), la intermodulación es parte del carácter.

### Retos completados

(Ninguno todavía.)

## Fase 7 — Dinámica y modulación: que la saturación se mueva sola

Hasta ahora Riptide era **estático**: con las perillas quietas, el golpe del bombo y la cola de la reverb pasaban por la
misma curva con el mismo drive. Una consola o una cinta real no son así: responden a lo fuerte que llega la señal, y
los efectos más vivos se mueven con el ritmo. Esta fase agrega dos "manos invisibles" que mueven una perilla cada una:
una **envolvente** que sigue el volumen de lo que entra y un **LFO** que oscila, libre o al tempo de FL.

### Conceptos aprendidos

- **Modulación.** Que un parámetro cambie solo, siguiendo una **fuente** (algo que se mueve: la envolvente, el LFO). Se
  arma con una **ruta**: fuente → **destino** (qué perilla mueve) × **amount** (cuánto). Es lo mismo que la matriz de
  modulación de Undertow, pero en un efecto: allá el LFO movía el filtro de cada nota; aquí mueve la saturación.
- **Escala del amount (la misma de Undertow).** 100 % con la fuente al máximo mueve la perilla destino **todo su
  recorrido**; 50 %, media perilla; 25 %, un cuarto. En Drive (−24..+24 dB) el recorrido son 48 dB, así que 25 % = 12 dB.
  Negativo = en sentido contrario. Las dos fuentes se suman si van al mismo destino, y el resultado nunca sale del rango
  de la perilla.
- **Seguidor de envolvente (envelope follower).** Convierte la señal (que sube y baja cientos de veces por segundo) en
  una curva lenta que dice "qué tan fuerte suena ahora". Riptide lo hace en dos pasos: primero un **detector de picos**
  que salta al instante a cada pico nuevo y, si no llega otro, va cayendo (eso es el **release**); después un suavizado
  que decide qué tan rápido sube la curva (eso es el **attack**). Separar los dos pasos importa: la versión simple de un
  solo paso se queda por debajo del pico real en los sonidos sostenidos (2 dB con un tono de 1 kHz y 5 ms de ataque).
- **Attack y release.** El attack es cuánto tarda la envolvente en subir: 0.1–2 ms reacciona dentro del transitorio
  (el golpe ya sale modulado); 20–100 ms deja pasar el golpe tal cual y reacciona después. El release es cuánto tarda en
  bajar: cada vez que pasa ese tiempo cae 8.7 dB (una recta en dB, pareja en todo el rango). Corto, sigue cada nota y
  con graves puede **ondular** (sigue cada ciclo del bajo); largo, suaviza pero tarda en soltar.
- **La envolvente se mide en dB.** 0 con la entrada a −40 dBFS (o menos), 1 a 0 dBFS. En dB porque así oímos el volumen:
  subir 6 dB mueve el destino lo mismo a cualquier nivel. La **sensibilidad** corre esa ventana: con +12 dB la envolvente
  llega al máximo a −12 dBFS. La barra celeste de ENVOLVENTE muestra cuánto marca: si siempre está llena o siempre
  vacía, la sensibilidad está mal puesta.
- **Saturación dinámica, en los dos sentidos.** Envolvente → Drive con amount **positivo**: lo fuerte satura más; los
  golpes "rompen" y lo suave queda limpio. Exagera la diferencia entre fuerte y suave (más dinámica de carácter). Con
  amount **negativo**: lo fuerte satura **menos**; los transitorios pasan limpios y con pegada, y la saturación engorda
  las colas, los sustains y lo que suena bajito. Es parecido a lo que hace una compresión paralela: más densidad sin
  aplastar el golpe.
- **LFO (Low Frequency Oscillator).** Un oscilador demasiado lento para oírse como nota (0.02–20 Hz) que no suena: mueve.
  Formas: **Sine/Triangle** (vaivén suave), **Saw Up/Down** (sube o baja y vuelve de golpe), **Square** (salta entre
  dos valores), **Sample & Hold** (un valor al azar por ciclo, "escalones"). Los saltos se redondean en 2 ms: sin eso,
  un salto de drive sería un clic.
- **Sync al tempo.** Con SYNC la velocidad es una figura musical: 1/4 = un ciclo por negra (a 120 BPM, 2 Hz), 1 bar = un
  compás, T = tresillo (2/3 de la duración), D = con puntillo (3/2). Con la canción sonando, la fase sale de la posición
  en la canción: un LFO de 1/4 cae siempre en el pulso, aunque empieces a reproducir a mitad de compás o se repita un
  loop. Sin SYNC, la velocidad es libre en Hz (no se alinea con nada: útil para movimientos "orgánicos").
- **Alinear la modulación con el audio.** El oversampling retrasa la señal (la latencia). Lo que va **antes** de la curva
  (drive, emphasis) se mueve con la modulación de ahora; lo que va **después** (la de-emphasis, el tilt, el mix) con la de
  hace una latencia, que es cuando entró a la curva el audio que sale ahora. Sin eso, un LFO sobre Emph Freq dejaba la
  de-emphasis desfasada del emphasis y quedaba una "EQ fantasma" ondulando: medido, −27.5 dB de residuo con un LFO de
  4 Hz a 44.1 kHz (audible); con la alineación, −88.5 dB (nada).
- **Modulación + AUTO GAIN.** El auto gain calcula su compensación muestra a muestra con el drive que de verdad sonó, así
  que también compensa el drive modulado: con AUTO GAIN puesto, la envolvente y el LFO cambian **cuánto satura** cada
  momento, no su volumen. Medido con ruido rosa y el drive saltando entre 0 y +24 dB: sin auto gain la parte saturada
  sale 11.2 dB más fuerte; con auto gain, 2.6 dB más baja (el lado seguro del modelo de la Fase 5). Sin AUTO GAIN, un LFO
  en el drive también es un trémolo de volumen.

### Qué hace cada control

- **ENVOLVENTE** (barra: lo que marca ahora, de 0 a 1):
  - **Attack** (0.1–100 ms, por defecto 5 ms) y **Release** (5 ms–2 s, por defecto 150 ms): ver arriba.
  - **Sens** (±24 dB, por defecto 0): sensibilidad del detector. Escucha lo que **entra** a Riptide, antes de Input y del
    tono: mover Input no cambia lo que marca.
  - **DESTINO** y **Amount** (−100..+100 %, por defecto 0 = apagada).
- **LFO** (barra: su valor, de −1 a +1 desde el centro):
  - **FORMA** y **SYNC** (por defecto encendido). Con SYNC, **DIVISIÓN** (por defecto 1/4); sin SYNC, **Rate**
    (0.02–20 Hz, por defecto 1 Hz).
  - **DESTINO** y **Amount**.
- **Destinos**: **Drive** (el Input: cuánto satura todo), **Mix**, **Emphasis**, **Emph Freq**, **Tilt** y **Band 1..4
  Drive** (solo esa banda). Cambiar de destino hace un fundido de 20 ms.
- **El arco celeste.** La perilla destino dibuja, por dentro de su anillo naranja, un arco celeste hasta el valor que
  suena de verdad. Lo que ves en la perilla es lo que pusiste; lo celeste es lo que hace la modulación encima.
- El medidor **IN** también se mueve: muestra el nivel que llega a la curva, ya con el drive modulado.
- En el analizador (Fruity Parametric EQ 2 después de Riptide): con el drive modulado, los armónicos **aparecen y
  desaparecen** al ritmo de la fuente (con amount positivo, en cada golpe; con el LFO, en cada ciclo). Con Emph Freq
  modulado, la zona más saturada (la que tiene más armónicos encima) **se desplaza** por el espectro.

### Ejercicio de escucha guiado: el mismo drive, en sentidos opuestos (con volumen igualado)

1. Un loop de batería en un insert con Riptide y el Parametric EQ 2 después. **Transistor**, Oversampling 4x, **AUTO
   GAIN puesto**, 1 banda, **Input +12 dB**.
2. Mira la barra de la ENVOLVENTE mientras suena: tiene que llegar casi al final en los golpes y bajar entre ellos. Si
   queda siempre llena, baja **Sens**; si apenas se mueve, súbela.
3. **Envolvente → Drive, Amount +25 %** (12 dB). Attack 1 ms, Release 80 ms. Qué notar: los golpes de bombo y caja se
   rompen más; entre golpes, la batería queda más limpia. El arco celeste de Input salta con cada golpe.
4. Ahora **Amount −25 %**. Qué notar: el golpe queda limpio y con pegada (el transitorio pasa antes de que la envolvente
   baje el drive), y lo que viene detrás (la cola de la caja, el ambiente) se satura y se engorda. La batería suena más
   densa y más "larga" sin perder el ataque.
5. Con −25 %, sube el **Attack** a 30 ms: el golpe alcanza a saturarse antes de que el drive baje. Vuelve a 1 ms. Ahí
   escuchas para qué sirve el attack: decide si el transitorio entra o no en la modulación.
6. Baja el **Release** a 10 ms: el drive "tiembla" dentro de cada golpe (sigue los ciclos de los graves) y aparece una
   aspereza nueva. Súbelo a 400 ms: la saturación tarda en volver y la batería respira más lento.
7. Compara con Amount 0 y con el bypass, siempre con AUTO GAIN puesto: la diferencia tiene que ser de carácter, no de
   volumen.

### Receta de uso: una batería que pega y además suena gorda

1. Riptide en el bus de la batería. **Tape** (o **Tube** para más cuerpo), **AUTO GAIN puesto**, Oversampling 4x.
2. **Input +9 a +15 dB**: la batería entera bien saturada (en este punto suena gorda pero el golpe se ablanda).
3. **Envolvente → Drive, Amount −20 a −35 %**, **Attack 0.5–2 ms**, **Release 60–150 ms** (más corto en tempos rápidos).
   Ajusta **Sens** para que la barra llegue casi al final en los golpes.
4. Qué pasa: en cada golpe el drive baja de inmediato y el transitorio pasa casi limpio; detrás de él el drive vuelve y
   satura el cuerpo y las colas. Tienes la pegada de la batería seca y la densidad de la saturada **en la misma señal**.
5. Si el golpe suena "cortado" o se oye el drive volviendo (un "swoosh"), alarga el Release. Si el golpe todavía suena
   blando, acorta el Attack o sube el amount negativo.
6. **Variante creativa con el LFO**: en un pad o una guitarra, **Emphasis +9 dB**, **LFO → Emph Freq, SYNC 1 bar,
   Triangle, Amount 20–30 %**. La zona que más se satura sube y baja una vez por compás: un movimiento parecido a un
   filtro lento, pero hecho de armónicos. Con **Sample & Hold a 1/8** el color cambia a saltos, al ritmo.

### Reto sin receta

Tienes una **voz** que en los versos se canta suave y en el estribillo se empuja fuerte. Quieres que en los versos
suene **limpia y cercana** y que en el estribillo **se ensucie sola**, con un poco de mordida, **sin automatizar nada**
en FL y sin que el estribillo salte de volumen respecto de los versos. Inténtalo y después pide pistas.

### Vocabulario

- **Modulación / ruta / destino / amount**: una fuente que mueve un parámetro, a cuál y cuánto. *En música:* el
  "wobble" del dubstep es un LFO moviendo el filtro de un bajo.
- **Envelope follower (seguidor de envolvente)**: el detector que convierte el volumen en una curva de control. *En
  música:* el auto-wah de una guitarra funk (la envolvente abre el filtro con cada púa).
- **Attack / release**: lo rápido que la envolvente sube y baja. *En música:* los mismos controles de un compresor; un
  release mal elegido es lo que hace "bombear" una mezcla.
- **Saturación dinámica**: saturación cuyo drive depende del nivel. *En música:* una cinta o una consola empujadas,
  donde los golpes fuertes se comprimen y ensucian más que lo suave.
- **LFO**: oscilador lento que no se oye, mueve. *En música:* el trémolo de un amplificador Fender (un LFO al volumen).
- **Sync / división de tempo / tresillo / puntillo**: velocidades atadas al tempo. *En música:* los delays y LFOs "a
  1/8 con puntillo" del rock de los 80 (el galope de The Edge, de U2).
- **Sample & Hold**: un valor al azar que se mantiene un tiempo y salta. *En música:* los "bleeps" aleatorios de los
  sintetizadores modulares y del sonido de la ciencia ficción de los 70.
- **Profundidad (depth)**: otra forma de decir amount. *En música:* "trémolo profundo" = el volumen baja mucho en cada
  ciclo.

### Errores comunes

- **Sensibilidad mal puesta.** Si la barra de la envolvente está siempre llena (o siempre vacía), la modulación es un
  valor fijo: no hay dinámica. Ajusta Sens mirando la barra con la música real.
- **Release muy corto con graves.** Con un bajo o un 808 y release de 5–20 ms, la envolvente sigue cada ciclo de la
  onda: el drive se mueve dentro de la forma de onda y eso es distorsión nueva (y a veces fea). En graves, release de
  80 ms o más.
- **Amount muy alto en Drive.** 100 % son 48 dB de recorrido: con la envolvente, de limpio a destrozado. Empieza con
  15–30 % y sube de a poco.
- **LFO en el drive sin AUTO GAIN.** Además del carácter, el volumen sube y baja con el LFO (trémolo). Si es lo que
  quieres, perfecto; si no, enciende AUTO GAIN.
- **Olvidar que SYNC está puesto.** Si mueves Rate y no pasa nada, es porque manda la división. Y al revés: sin SYNC, el
  LFO no cae en el pulso aunque suene "parecido".
- **Modular sin propósito.** Un LFO que se mueve sin relación con la música distrae; en una mezcla, la modulación tiene
  que servir a algo (pegada, respiración, movimiento en una parte estática).
- **Cuándo NO usarla:** en el master y en buses donde la saturación tiene que ser un "pegamento" estable, la envolvente
  con mucho amount se oye como bombeo; ahí, amount bajo o nada. Y en material que ya tiene mucha modulación propia (un
  pad con LFO, una guitarra con trémolo) otra modulación encima suele estorbar.

### Retos completados

(Ninguno todavía.)

## Fase 8 — Ver la saturación: curva en vivo, espectro antes/después y bandas

Hasta ahora, para ver lo que hacía Riptide había que poner el Parametric EQ 2 después y adivinar qué parte del cambio
venía de qué perilla. Esta fase le da ojos propios: una **curva de transferencia** que se dobla mientras mueves Input o
cambias de modo, un **analizador de espectro** que muestra a la vez lo que entra y lo que sale (con las bandas encima;
los cruces se arrastran ahí mismo) y una ventana **escalable**. El sonido no cambia nada: es la misma Fase 7.

### Conceptos aprendidos

- **Curva de transferencia.** La "huella" de una saturación: para cada valor que entra (eje horizontal) dice qué valor
  sale (vertical). La línea punteada es y = x, salir igual que entró: **todo lo que la curva se separa de ella es
  saturación**. Se lee así:
  - **la rodilla** (dónde deja de ser recta): Tape se curva muy pronto y muy suave; Transistor sigue recta casi hasta
    arriba y dobla de golpe; Hard Clip es recta y después plana (la esquina);
  - **el techo** (la altura máxima): de ahí no pasa la salida, por fuerte que entre la señal;
  - **la simetría**: si la mitad de abajo es la de arriba dada vuelta, solo hay armónicos impares. Tube tiene el techo
    de arriba más bajo que el de abajo: esa asimetría que **se ve** es la que crea los armónicos pares que **se oyen**;
  - Fold sube y vuelve a bajar (se dobla); Rectify da vuelta la mitad negativa (la curva queda toda arriba).
- **Drive = comprimir la curva hacia el centro.** El eje horizontal es la señal tal cual llega (los bordes son 0 dBFS) y
  la curva dibujada es f(drive · x). Subir Input no mueve tu señal: **trae la rodilla hacia ella**. Por eso el mismo
  modo es "limpio" o "destrozado" según el drive.
- **El tramo brillante = dónde vive tu señal.** Va de −pico a +pico de lo que entra (el pie del visor dice cuánto). Una
  voz a −18 dBFS solo usa el centro recto: aunque la curva se doble mucho más afuera, esa voz casi no se satura. Es el
  gain staging de la Fase 1, ahora visible.
- **Espectro y FFT.** Un sonido se puede describir como una suma de senos, cada uno con su frecuencia y su nivel. La FFT
  (transformada rápida de Fourier) toma un trozo de señal (las últimas 4096 muestras, unos 93 ms) y calcula cuánto de
  cada frecuencia hay en él. El resultado son **bins**: casilleros de frecuencia de sampleRate / 4096 = 10.8 Hz a
  44.1 kHz (a 88.2/96 kHz se usan 8192 muestras para que el trozo dure lo mismo).
- **Ventana de Hann.** La FFT supone que el trozo se repite sin fin; si no contiene ciclos enteros, el final no empalma
  con el principio y ese "salto" ensucia todas las frecuencias (**fuga espectral**). La ventana lleva los bordes del
  trozo suavemente a 0. Precio: cada tono se ve como un monte de unos 4 bins de ancho, con un poco de "falda" a los
  lados (por eso, alrededor de una fundamental muy grave y fuerte, el piso del espectro sube).
- **Resolución contra rapidez.** Más muestras = bins más juntos (más detalle en graves) pero el trozo dura más (reacciona
  más lento). Con 10.8 Hz por bin, dos notas de bajo a un semitono (55 y 58 Hz) se ven como un solo monte; en agudos,
  donde un semitono son cientos de Hz, sobra resolución.
- **Calibración.** Un seno de 0 dBFS marca 0 dB en su frecuencia (medido en los tests: ±0.01 dB; entre dos bins hasta
  1.4 dB menos). Ojo: la **música** se ve mucho más abajo que en el medidor, porque su energía se reparte entre miles de
  bins; el analizador sirve para **comparar** zonas y ver qué aparece, no para medir volumen.
- **Eje logarítmico y balística.** Cada octava ocupa el mismo ancho (como la oye el oído): 20–40 Hz mide lo mismo que
  10–20 kHz. Cada punto sube casi al instante y baja a 40 dB/s: así la música se lee sin que el dibujo tiemble.
- **Antes/después = qué agregó la saturación.** ANTES (gris) es lo que entra a Riptide; DESPUÉS (relleno) lo que sale.
  Con un tono puro: en ANTES un solo pico; en DESPUÉS, la **serie armónica** encima. Con AUTO GAIN puesto los dos
  quedan al mismo nivel y la comparación es justa (sin él, la salida más fuerte se ve "más arriba en todo").
- **Leer armónicos.** Los armónicos caen en múltiplos de la fundamental. Un A3 de FL (110 Hz): 2.º = 220 Hz (A4),
  3.º = 330 Hz (E5), 4.º = 440 Hz (A5), 5.º = 550 Hz (C#6). Pasa el mouse por el visor: arriba se lee la frecuencia, la
  nota (con los nombres de FL) y el nivel. Los pares (220, 440...) son octavas de la nota: Tube y Rectify. Los impares
  (330, 550...) son una quinta y una tercera mayor, octavas más arriba: Tape, Transistor, Clip, Fold.
- **Ver el aliasing.** Un armónico que pasaría de la mitad de la frecuencia de muestreo "rebota" y aparece en una
  frecuencia que **no** es múltiplo de la nota. En el visor: picos fuera de la serie, a veces por debajo de la
  fundamental. Con Oversampling en Off y notas agudas se ven; en 4x desaparecen.
- **Las bandas en el espectro.** Con 2 o más bandas cada zona se tiñe del color de su banda (el mismo de su columna y de
  su curva), con su número, su modo y su drive arriba. Cada cruce es una línea con un asa abajo: **se arrastra** (doble
  clic = valor por defecto) y es la misma perilla Cruce de abajo (FL graba el movimiento como automatización). Las
  bandas que no se oyen (mute, o fuera del solo) se oscurecen.
- **Por qué la GUI no le pregunta al audio.** El hilo de audio no puede esperar a nadie (ni a la GUI). Riptide deja una
  copia de lo que entra y de lo que sale en una "toma" circular que se pisa sola; la GUI copia las últimas muestras
  cuando quiere dibujar y después comprueba si el audio las pisó mientras copiaba (si pasó, descarta la copia y lo
  intenta en el cuadro siguiente). Sin locks: el audio nunca se entera de que alguien mira. Esa pieza
  (`AnalyzerTap.h`) y el analizador (`SpectrumAnalyzer.h`) quedaron en la librería: el EQ (plugin 3) los va a reusar.

### Qué hace cada control

- **CURVA** (arriba a la izquierda): no tiene perillas propias. Cambia con **Input**, el **modo** (los botones de justo
  debajo), el **drive y el modo de cada banda** y la **modulación** (con un LFO en el drive, la curva "respira"). Con 1
  banda: una curva naranja con el tramo de tu señal brillante y dos puntos en el pico. Con varias: una curva por banda,
  en su color (sin tramo: cada banda recibe solo una parte de la señal).
- **ESPECTRO**: **ANTES** y **DESPUÉS** encienden o apagan cada trazo. Mouse encima = frecuencia, nota y nivel. Las asas
  de los cruces se arrastran.
- **Zoom** (en la cabecera, junto a BYPASS): 70–200 %, o arrastrando la esquina inferior derecha. Se guarda con el
  proyecto de FL y como preferencia para las instancias nuevas.
- Todo lo demás está donde estaba, reacomodado: los modos en una fila bajo los visores, TONO junto a NIVEL Y MEZCLA y
  las BANDAS con una franja de su color.

### Ejercicio de escucha guiado: ver y oír cada modo (con volumen igualado)

1. Un canal con un seno: 3x Osc con solo el oscilador 1 en Sine (o Undertow en su sonido por defecto). Deja sostenida
   **A3 (110 Hz)**. Riptide en el insert, **AUTO GAIN puesto**, Oversampling 4x, 1 banda.
2. Con **Input 0 dB** mira la CURVA: el tramo brillante queda en la parte casi recta. En el ESPECTRO, ANTES y DESPUÉS
   son casi iguales: un solo pico en 110 Hz. Escucha: casi idéntico al bypass.
3. Sube **Input a +12 dB**. La curva se comprime hacia el centro y el tramo brillante llega a la rodilla. En DESPUÉS
   aparecen picos en 330, 550, 770 Hz (Tape: impares). Pasa el mouse para leerlos: E5, C#6...
4. Cambia a **Tube**: aparecen además 220 y 440 Hz (pares). Mira la curva: el techo de arriba es más bajo que el de
   abajo. Escucha: más "lleno", como si sumaras la octava.
5. **Transistor** y luego **Hard Clip**: los armónicos impares altos (los de la derecha) suben mucho más. La rodilla es
   más corta. Escucha: más áspero, más "cuadrado".
6. **Fold** con Input en +18 dB: la curva se dobla dentro del tramo brillante y el espectro se llena de armónicos que
   cambian al mover Input. **Rectify**: el pico de 110 Hz casi desaparece y la serie empieza en 220 Hz (una octava
   arriba).
7. Vuelve a Hard Clip, pon **Oversampling Off** y toca más agudo (A6, 880 Hz): aparecen picos sueltos fuera de la serie
   (aliasing). Pasa a 4x: se van. Ahora lo puedes **ver** además de oír.
8. En todo el ejercicio, compara con BYPASS: con AUTO GAIN el volumen apenas cambia; lo que cambia es el carácter.

### Receta de uso: elegir los cruces de un bus de batería mirando el espectro

1. Riptide en el bus de la batería, **3 bandas**, AUTO GAIN puesto, solo **ANTES** encendido.
2. Reproduce el loop y busca con el mouse el monte del bombo (casi siempre 45–70 Hz) y el cuerpo de la caja
   (150–250 Hz). Arrastra el **Cruce 1** justo por encima de la fundamental del bombo (p. ej. 90–110 Hz): la banda 1
   queda con el bombo solo.
3. Busca dónde empiezan los platillos (el piso que sube a la derecha, desde 5–8 kHz) y lleva el **Cruce 2** a ese punto.
   La banda 2 queda con la caja, los toms y el cuerpo de todo.
4. Banda 1 **Tube +3 dB** (cuerpo, pares), banda 2 **Tape +6 dB** (densidad), banda 3 **sin drive** o con −3 dB
   (platillos limpios). Enciende **DESPUÉS**: los armónicos nuevos tienen que aparecer en graves y medios, y la zona de
   los platillos tiene que quedar casi igual que en ANTES.
5. Usa **S** de cada banda para oír lo que estás separando: si en el solo de la banda 1 se oye el golpe de la caja, el
   Cruce 1 está muy arriba.
6. Por qué funciona: el espectro te dice **dónde** está cada instrumento; poner los cruces entre ellos (no encima) hace
   que cada banda sature un instrumento y que el bombo deje de "empujar" la saturación de la caja y los platillos.

### Reto sin receta

Pídele a alguien (o hazlo tú tapando la parte de abajo de la ventana) que elija un modo al azar con el mismo Input, sobre
un seno sostenido. Mirando **solo el espectro** (no la curva ni los botones), di cuál es. Después al revés: mirando
**solo la curva**, di qué armónicos van a aparecer antes de mirar el espectro. Inténtalo y después pide pistas.

### Vocabulario

- **Curva de transferencia**: la función entrada → salida de un saturador. *En música:* la "curva de compresión" de un
  compresor es lo mismo, en dB; los manuales de pedales de distorsión también la dibujan.
- **Rodilla (knee) / techo**: dónde la curva deja de ser recta y hasta dónde llega. *En música:* el "soft knee" de un
  compresor de voz (empieza a actuar de a poco) contra el "brick wall" de un limitador de master.
- **FFT / bin**: el cálculo que pasa una señal a frecuencias y cada casillero que resulta. *En música:* lo que hay detrás
  de Wave Candy, del Parametric EQ 2 y de cualquier analizador.
- **Ventana / fuga espectral**: el suavizado de los bordes del trozo que se analiza y la "suciedad" que evita. *En
  música:* por eso un analizador nunca muestra un seno como una raya finita, sino como un monte.
- **Resolución en frecuencia**: qué tan juntos están los bins. *En música:* por qué en un analizador el sub de un 808 se
  ve como un manchón y los armónicos agudos de una voz como picos claros.
- **Serie armónica**: la fundamental y sus múltiplos (×2, ×3, ×4...). *En música:* lo que hace que un A3 de piano y uno
  de guitarra sean "la misma nota" con distinto timbre: la misma serie, con distintos niveles.
- **Balística (de un visor)**: lo rápido que sube y baja lo dibujado. *En música:* la diferencia entre un medidor VU
  (lento) y uno de picos (rápido).
- **Escala logarítmica**: cada octava ocupa lo mismo. *En música:* el teclado del piano: cada octava son 12 teclas,
  aunque en Hz cada una sea el doble de ancha que la anterior.

### Errores comunes

- **Mezclar con los ojos.** El espectro dice qué cambió, no si suena mejor. Decide escuchando; usa el visor para
  entender lo que oíste o para encontrar una frecuencia.
- **Comparar ANTES y DESPUÉS sin igualar el volumen.** Si la salida es 6 dB más fuerte, todo el relleno se ve más arriba
  y parece que "agregó de todo". Con AUTO GAIN (o igualando con Output) lo que sobresale es lo que de verdad se agregó.
- **Esperar que el espectro coincida con el medidor.** Un bin de música marca mucho menos que el medidor (la energía se
  reparte); el pico de un seno sí coincide.
- **Leer graves finos.** Debajo de 100 Hz los bins están a 10.8 Hz: dos notas de bajo cercanas se ven juntas y el monte
  de una fundamental grave ensancha el piso alrededor. Para afinar un 808, mejor el oído (o un afinador).
- **Analizador en mono.** Se mide (L + R) / 2: algo en contrafase entre los canales (un estéreo muy abierto) se ve más
  bajo de lo que suena.
- **La música se ve inclinada.** Casi toda la música tiene mucha más energía en graves que en agudos: un espectro que cae
  hacia la derecha es lo normal, no un problema de la mezcla.
- **Cuándo NO mirarlo:** al decidir cuánto drive "suena bien" en una mezcla. Ahí cierra los ojos, iguala el volumen y
  compara con el bypass.

### Retos completados

(Ninguno todavía.)

## Fase 9 — Presets con notas: recetas que se cargan con un clic

Riptide trae 25 presets de fábrica (más Init) organizados por **para qué pista** sirven: Batería (una pista), Bajo, Voz,
Instrumentos, Bus (un grupo, sobre todo la batería), Master y Creativo. Casi todos son las recetas de este cuaderno,
listas para cargar; cada uno lleva notas que dicen **qué hace cada ajuste y por qué**, cómo probarlo y cuándo no usarlo.
Puedes guardar los tuyos con sus propias notas. El sonido de Riptide no cambia: es la misma Fase 8.

### Conceptos aprendidos

- **Un preset es un punto de partida, no un resultado.** Un preset se hizo con OTRA señal: otro nivel, otra grabación,
  otro tema. Por eso casi todos usan AUTO GAIN y por eso al cargarlo hay que revisar al menos dos cosas: **cuánto llega
  a la curva** (Input, o el nivel de tu pista) y **cuánto se mezcla** (Mix). El resto (el modo, qué zona se satura, los
  cortes) es la idea del preset y suele funcionar tal cual.
- **El preset depende del nivel de entrada.** Una saturación no es lineal: el mismo preset sobre una pista a −30 dBFS
  casi no hace nada y sobre una a −6 dBFS puede destrozarla. Los presets de fábrica están pensados para pistas con gain
  staging (Fase 1): RMS cerca de −18 dBFS. Mira la CURVA al cargarlo: si el tramo brillante no llega a la rodilla, sube
  Input; si se pasa de largo, bájalo.
- **Lo que el AUTO GAIN no compensa.** El auto gain calcula cuánto volumen pierde la CURVA; los filtros (cortes, tilt, la
  cabeza de la cinta) y la mezcla en paralelo son ecualización y se oyen como tal. Por eso algunos presets traen Output:
  Radio AM (+6 dB: los cortes se llevan mucha energía), los paralelos con corte PRE (Caja con Mordida +5.5 dB, Bajo
  Paralelo Tube +6.5 dB, Bus Batería Paralelo +3 dB), o −1 dB donde la cinta o el tilt suman graves. Todos quedan a
  ±1.5 dB del bypass, medido con señales de prueba (`RiptidePresetTests --verbose`: de −1.3 a +0.5 dB).
- **Mix = reparto, no suma.** Con Mix 50 % la original baja a la mitad (−6 dB) y la saturada entra a la mitad. Si la
  saturada no tiene graves (Low Cut PRE), el sub del total baja 6 dB: Output lo devuelve.
- **El bypass no es parte del sonido.** Cargar un preset nunca apaga ni enciende Riptide, y los presets no guardan el
  bypass. Init tampoco es transparente: la cinta ya redondea un poco con Input a 0 dB. Para comparar, siempre BYPASS.
- **Un preset es un archivo de texto con unidades reales.** XML legible: `inputGain` 6 (dB), `highCut` 9000 (Hz),
  `mode` Tube. Lo que no aparece toma su valor por defecto (el de Init): un preset de fábrica se lee como una receta,
  solo con lo que cambia. Ojo: en las listas un número es la **posición** de la opción, no su nombre; `bands` 2 = la
  tercera opción = 3 bandas (por eso el archivo lo aclara con un comentario).
- **El nombre del preset viaja con el proyecto de FL**, con un `*` si lo modificaste. Un proyecto anterior a esta fase
  dice "Sin nombre" (su sonido no viene de ningún preset).
- **Una sola biblioteca de presets para toda la suite.** El sistema de presets de Undertow (formato, navegador, guardar)
  pasó a `libs/presets`: Riptide lo usa tal cual y los próximos plugins también. Por eso el navegador se ve y se usa
  igual en los dos.

### Qué hace cada control

- **◀ ▶** (cabecera): preset anterior / siguiente, en el orden del navegador. Rápido para comparar varios sobre la misma
  pista con el tema sonando.
- **Nombre** (con la categoría en naranja y `*` si tocaste algo): abre el **navegador**. A la izquierda las categorías (y
  "Mis presets"); en el centro la lista y la búsqueda (busca en nombre, categoría y **notas**: prueba "paralelo",
  "envolvente", "808"); a la derecha las notas. Un clic o las flechas del teclado = cargar (para escuchar rápido); doble
  clic o Enter = cargar y cerrar; Esc = cerrar. Los presets tuyos tienen un punto ámbar y se pueden borrar (van a la
  papelera de reciclaje).
- **Guardar**: tu sonido como preset propio, con nombre, categoría, autor y **notas**. Si partiste de uno de fábrica, las
  notas empiezan con "Basado en «...»" para que escribas qué cambiaste y por qué. Se guardan en
  `Documentos\Riptide\Presets` (puedes hacer subcarpetas; el navegador las lee todas).
- **···**: Init (todo por defecto), Guardar como, abrir la carpeta de tus presets y volver a leerla (si copiaste
  archivos a mano).
- Junto al título aparece unos segundos el último mensaje ("Preset: ...", "Preset guardado: ...", avisos).

### Ejercicio de escucha guiado: el mismo bus de batería, cuatro filosofías (con volumen igualado)

1. Pon Riptide en el bus de la batería (o en un loop). Que llegue con unos −18 dBFS RMS (medidor IN).
2. Carga **Bus Batería Cinta**. Alterna BYPASS: más densa, platillos más suaves, mismo volumen.
3. Carga **Bus Batería Paralelo**. Mira el Mix (30 %) y escucha los golpes: pegan igual que en el bypass, pero entre
   golpe y golpe hay más "relleno". Sube el Mix a 100 % un momento para oír la parte saturada sola.
4. Carga **Bus Batería 3 Bandas**. Mira el espectro: tres zonas teñidas. Usa la **S** de cada banda: el bombo con Tube,
   la caja con Transistor, los platillos casi limpios.
5. Carga **Bus Batería Pegada y Cuerpo**. Mira la barra de la ENVOLVENTE y el arco celeste de Input: el drive baja en
   cada golpe y vuelve en la cola.
6. Con ◀ ▶ recórrelos con el tema sonando. Para cada uno, escribe en una línea qué conserva (pegada, sub, platillos) y
   qué agrega. Las cuatro son formas de "pegar" una batería; la que eliges depende de qué tiene que conservar el tema.
7. Al final compara siempre con BYPASS: si alguno te gusta "porque suena más fuerte", revisa el medidor OUT; deberían
   quedar a ±1.5 dB.

### Receta de uso: adaptar un preset a tu pista (y guardarlo como tuyo)

1. Carga el preset más cercano a lo que buscas (p. ej. **808 en el Celular** en tu 808).
2. **Nivel**: mira la CURVA. Si tu 808 llega mucho más bajo o más alto que −18 dBFS RMS, corrige con **Input** hasta que
   el tramo brillante llegue a la rodilla sin pasarla de largo.
3. **La zona**: el preset satura alrededor de 100 Hz (Emphasis). Pasa el mouse por el espectro sobre el monte de tu 808 y
   lleva **Emph Freq** ahí (para un 808 en F2 de FL, 44 Hz, mejor entre 80 y 100 Hz: el cuerpo, no el sub puro).
4. **El resultado**: alterna BYPASS con el EQ que simula el celular (corte de 150 Hz, receta de la Fase 4). Ajusta Input
   hasta que la nota se entienda.
5. **Guárdalo**: Guardar → nombre ("808 Mi Tema"), categoría Bajo, y en las notas qué cambiaste y por qué ("Input +3: mi
   808 llega más bajo"; "Emph Freq 85 Hz: ahí está su cuerpo"). Dentro de un mes esas notas son lo que te va a servir;
   el preset sin notas, no.
6. ¿Por qué así? El preset trae la IDEA (Tube + emphasis en el cuerpo + sin fizz); tu pista decide los NÚMEROS.

### Reto sin receta

Elige una voz de un tema tuyo y crea **tu propio preset de voz** desde **Init**, sin cargar ninguno de los de Voz, con un
objetivo que escribas antes de empezar (p. ej. "que suene a radio de los 70 pero se entienda" o "más cálida que la
original sin que se note"). Guárdalo con notas que expliquen cada ajuste. Después compara a ciegas (con BYPASS y el
volumen igualado) contra Voz Cálida Tube y Voz Presente Cinta: ¿qué hiciste distinto y por qué? Inténtalo y después
pide pistas.

(Los presets de fábrica no resuelven los retos pendientes de las fases 1, 3, 5, 6, 7 y 8. Los de las fases 2 y 4 ya
tenían su solución en una receta posterior: **808 en el Celular** es la receta de la Fase 4 y **Bajo Gruñido, Sub
Limpio** la variante de la Fase 6. **Master Cinta Sutil** es a propósito la versión simple: sus notas dicen qué le falta
para el reto de la Fase 6, sin resolverlo.)

### Vocabulario

- **Preset**: una foto guardada de los controles de un plugin, con nombre. *En música:* el "patch" de un sintetizador o
  los presets de un amplificador de guitarra digital.
- **Punto de partida (starting point)**: un preset que se carga para ajustarlo, no para dejarlo tal cual. *En música:*
  los ingenieros de mezcla tienen plantillas con los plugins ya puestos y las retocan en cada tema.
- **Gain staging (otra vez)**: el nivel con el que cada pista llega a cada plugin. *En música:* la razón por la que el
  mismo preset de un video de YouTube suena distinto en tu proyecto.
- **Saturación en paralelo**: mezclar la señal saturada con la original. *En música:* la "compresión New York" de las
  baterías de rock y hip hop, pero con armónicos en vez de compresión.
- **Clipper**: un recortador de picos antes del limitador del master. *En música:* parte del volumen de los masters de
  trap y EDM viene de clippers que recortan los golpes del bombo y la caja.
- **Octavador (octave fuzz)**: un efecto que suma una octava arriba rectificando la onda. *En música:* el Octavia de
  Jimi Hendrix en el solo de "Purple Haze".
- **Fundamental ausente**: el oído "escucha" una nota grave aunque no suene, si escucha sus armónicos. *En música:* por
  qué un 808 saturado se entiende en el celular.

### Errores comunes

- **Cargar un preset y no tocar nada.** Se hizo con otra señal. Mínimo: revisa la CURVA (nivel) y el Mix.
- **Elegir el preset que suena "más fuerte".** Están igualados a ±1.5 dB con señales de prueba, pero con tu pista pueden
  quedar más arriba o más abajo. Compara con BYPASS y el medidor OUT.
- **Usar el preset de otra categoría sin pensar.** Uno de Master (sutil, oversampling alto) en un bajo casi no se nota;
  uno de Bus en el master puede ser demasiado. La categoría dice para qué pista y cuánta "intervención" se pensó.
- **Guardar sin notas.** En tres semanas no vas a recordar por qué el Cruce 1 estaba en 135 Hz. Las notas son la mitad
  del preset.
- **Apilar presets.** Tres instancias de Riptide con tres presets "de color" en la misma pista suman densidad hasta
  aplanarla. Una saturación por pista y otra en el bus suele bastar.
- **Cuándo NO usar un preset:** cuando no sabes qué le falta a la pista. Primero escucha y nombra el problema (no se oye
  en el celular, la caja no corta, la voz está lejos); después busca el preset que lo resuelve.

### Retos completados

(Ninguno todavía.)

## Fase 10 — Pulido: menos CPU, pruebas de estrés y comparar contra referencias sin engañarse

Esta fase no agrega controles. Riptide suena **exactamente igual** que en la Fase 9 (comprobado bit a bit), pero gasta
**la mitad de CPU** (el peor caso, 4 bandas a 16x, bajó de 16.6 % a 9.3 % de un núcleo; Init, de 1.55 % a 0.98 %), la
interfaz deja de redibujarse cuando nada cambia, pasó una prueba de tortura con miles de cambios al azar, y hay una
herramienta para **comparar Riptide contra otros saturadores con el volumen igualado**, incluso a ciegas.

### Conceptos aprendidos

- **El presupuesto de tiempo real.** Con un buffer de 256 muestras a 48 kHz, FL le pide audio a cada plugin cada
  5.3 ms; si la cadena entera no termina a tiempo, el audio se corta (clics, "underruns"). "% de un núcleo" = qué parte
  de ese tiempo se come el plugin. Riptide en Init: ~1 %, o sea unas 100 instancias por núcleo. 16x con 4 bandas: ~9 %.
- **Qué cuesta en un saturador**: casi todo es el **oversampling**. A 16x la curva se calcula 16 veces por muestra y cada
  etapa ×2 tiene dos filtros halfband (subir y bajar). Cada **banda** es un saturador completo: 4 bandas ≈ 4 veces. El
  resto (tono, modulación, auto gain, medidores) es un coste fijo pequeño.
- **Medir antes de optimizar.** La primera idea ("las curvas son caras, hay que vectorizarlas") era falsa: midiendo cada
  pieza por separado, una etapa halfband costaba lo mismo con 12 coeficientes que con 40. El tiempo no se iba en las
  cuentas, sino en **esperar a la memoria**: el filtro escribía una muestra y la volvía a leer enseguida, y el
  procesador tiene que esperar a que esa escritura termine (unos 10 ciclos) antes de leerla en grupo de 4 (SIMD).
- **Procesar por bloques.** La solución: en vez de llevar cada muestra por toda la cadena antes de pasar a la siguiente,
  Riptide hace tramos de 32 muestras en tres pasadas: (1) todo lo de antes de la curva, muestra a muestra; (2) las curvas
  con oversampling, el tramo entero de una vez (escribe primero las 32 muestras, después las lee: ya no espera); (3) todo
  lo de después de la curva. Se puede porque lo de antes de la curva no depende de lo que sale de ella.
- **Optimizar sin cambiar el sonido.** Las tres pasadas hacen **las mismas cuentas en el mismo orden**: el resultado es
  idéntico bit a bit. Se comprobó compilando el motor viejo y el nuevo y renderizando 384 escenarios (4 sample rates,
  bloques de 1 a 4096, mono y estéreo, con cambios de modo, factor, bandas, bypass y modulación a mitad): 190 MB
  idénticos byte a byte. Es el **null test** llevado al extremo: si la resta es silencio digital, no hay nada que oír.
- **Denormales.** Cuando la señal se apaga, los filtros recursivos siguen dividiendo su "memoria" hasta números diminutos
  (menos de 10⁻³⁸) que el procesador calcula por un camino lentísimo. Medido: el peor caso de Riptide pasa de 9 % a
  **60 %** de CPU cuando la pista queda en silencio, si no se hace nada. El plugin activa "flush to zero" al empezar cada
  bloque (convierte esos números en 0: están 700 dB por debajo de lo audible). Por eso un plugin mal hecho puede
  disparar la CPU justo cuando la pista se calla.
- **La interfaz también gasta CPU** (la de la interfaz de FL, no la del audio). Antes la curva, el espectro y los
  medidores se redibujaban 30 veces por segundo aunque nada cambiara; ahora solo si algo se movió de forma visible. Con la
  canción parada, Riptide abierto ya no gasta.
- **Pruebas de estrés.** pluginval (strictness 10, 4 sample rates, bloques de 1 a 4096, valores al azar, 3 repeticiones)
  y una prueba propia de "tortura": todos los parámetros cambiando al azar cada pocos bloques, con bloques de tamaño al
  azar, sobre música, en 4 sample rates. Ni un NaN, ni una reserva de memoria en el audio, ni un filtro que explote.
- **El oído cree que lo más fuerte es mejor.** Una diferencia de 0.5–1 dB ya no se oye como "más fuerte" sino como "más
  lleno, más brillante, más cerca". Casi todos los saturadores suben el volumen, así que comparar sin igualar es comparar
  volúmenes. Igualar por **pico** no sirve (la saturación baja los picos y sube la energía); por **RMS** es mejor; por
  **sonoridad (LUFS)** es lo más parecido a cómo oímos: pesa menos los subgraves y un poco más los agudos, como el oído.
- **Prueba ciega.** Saber cuál es cuál cambia lo que oímos (si sabes que es el plugin caro, te va a gustar más). Con
  archivos con nombres al azar, el oído decide solo.

### Qué hace cada control

No hay controles nuevos. Lo que cambia es **cuánto cuesta cada uno** (CPU a 48 kHz, estéreo, en este equipo):

| Ajuste | CPU | Cuándo |
|---|---|---|
| Oversampling Off | ~0.6 % | Monitorear mientras grabas; modos suaves (Tape) con poco drive |
| 2x / **4x** | ~0.8 / ~1.0 % | **4x**: el punto de partida de siempre |
| 8x / 16x | ~1.4 / ~2.2 % | Hard Clip, Fold o Rectify con mucho drive; el master; al exportar |
| 4 bandas a 4x / 16x | ~3.2 / ~8.5 % | Buses y master; no en cada pista |
| BYPASS | lo mismo que activo | El motor sigue calculando para que al volver no haya saltos: para ahorrar CPU, quita el plugin |

Y la herramienta nueva, `scripts/level_match.py` (en la carpeta del proyecto):

- `python scripts/level_match.py <carpeta>`: mide cada WAV de la carpeta y escribe en `<carpeta>/igualados/` copias a la
  **misma sonoridad** que la referencia (el archivo que empiece con `dry`, `original` o `seco`). La tabla dice:
  - **LUFS**: sonoridad (la de Spotify y YouTube); **pico**: el máximo en dBFS;
  - **PLR** (pico menos sonoridad): cuánto sobresalen los golpes. La saturación lo **baja**: menos PLR = más denso, menos
    pegada;
  - **sube**: cuánto más fuerte dejó el procesado al audio (eso es lo que te engañaría al comparar);
  - **balance por octavas**, ya igualado: dónde agrega (+) o quita (−) energía cada versión. Arriba de 4 kHz es brillo y
    "fizz"; 125–500 Hz, cuerpo o barro; 31–63 Hz, subgraves.
- `--ciego`: además copia todo (la referencia también) como `ciego/A.wav`, `B.wav`... y la clave en `ciego/clave.txt`.

### Ejercicio de escucha guiado: Riptide contra los saturadores de FL, a ciegas (con volumen igualado)

1. Un loop de batería de 8 compases en un insert del mixer. Exporta el insert **sin efectos** (`File > Export > Wave
   file`, 32 bits float) con el nombre `dry-bateria.wav`, en una carpeta nueva.
2. Pon **Riptide** con **Bus Batería Cinta** y exporta `riptide-cinta.wav`. Quítalo.
3. Pon **Fruity Blood Overdrive** (o **Fruity Soft Clipper**, o **Fruity Waveshaper** con una curva en S suave) y
   ajústalo de oído hasta que **sature más o menos lo mismo** que Riptide (sin preocuparte por el volumen). Exporta
   `fl-overdrive.wav`. Si tienes otro saturador (Softube Saturation Knob y Klanghelm IVGI son gratuitos; Decapitator o
   Saturn 2 si los tienes), exporta uno por cada uno.
4. Abre una terminal en la carpeta del proyecto y corre `python scripts/level_match.py <tu carpeta> --ciego`.
5. Mira la columna **sube**: ¿cuánto más fuerte dejó cada uno el loop? Eso es lo que habrías "preferido" sin igualar.
6. Escucha `ciego/A.wav`, `B.wav`... en loop (arrástralos a FL, uno por pista, y usa solo). Para cada uno escribe: ¿cuál
   es el original?, ¿cuál tiene más pegada?, ¿cuál tiene platillos más ásperos?, ¿cuál prefieres? **Después** abre
   `clave.txt`.
7. Compara tus notas con la tabla: lo que oíste como "más brillante" debería verse en las octavas de 4–16 kHz; "más
   denso", en un PLR más bajo.

### Receta de uso: decidir el oversampling (y la CPU) de un proyecto grande

1. Mientras produces, todas las instancias en **4x** (o Off en lo que grabas en vivo, para menos latencia).
2. Si una pista con Hard Clip, Fold o Rectify suena "sucia" en los agudos de una forma que no debería (tonos que no
   siguen la nota), súbele el oversampling **solo a esa**: es aliasing (Fase 2).
3. Las multibanda (3–4 bandas) en los buses, no en cada pista: cada banda es un saturador entero.
4. Mira el medidor de CPU de FL (barra superior) con el tema sonando y con la canción parada: con Riptide el número no
   debería subir cuando la pista se calla (denormales: Riptide ya está protegido; si otro plugin lo hace, ya sabes por
   qué).
5. Para el render final puedes subir a 8x o 16x las instancias del bus y del master: exportar no es tiempo real.
6. ¿Por qué así? El oversampling alto solo se nota con curvas duras y mucho drive; con Tape a 4x la diferencia con 16x
   está por debajo de lo audible y cuesta el doble.

### Reto sin receta

Elige un saturador de referencia (uno de FL o uno que tengas) y **haz que Riptide suene lo más parecido posible** a él
sobre un bajo: mismo material, misma sonoridad. Usa `level_match.py` para medir: la meta es que el **balance por
octavas** de las dos versiones quede a ±1 dB en todas las octavas y que en una prueba ciega no aciertes cuál es cuál más
de la mitad de las veces. Anota qué controles de Riptide te acercaron (modo, Emphasis, tilt, cortes, Mix) y qué no
pudiste imitar. Inténtalo y después pide pistas.

### Vocabulario

- **Buffer / underrun**: el bloque de audio que el host le pide a los plugins de una vez; si no llega a tiempo, se oye un
  clic o un corte. *En música:* los "crujidos" de un proyecto pesado en una laptop; subir el buffer en Audio Settings de
  FL los quita a costa de más latencia.
- **CPU (% de un núcleo)**: cuánto del tiempo de un núcleo del procesador necesita un plugin para no cortarse. *En
  música:* por qué un proyecto con 40 instancias de un plugin pesado se traba y con uno ligero no.
- **Denormal**: un número tan pequeño que el procesador lo calcula muy lento. *En música:* plugins viejos que
  disparaban la CPU en los silencios del final de un tema (las colas de reverb).
- **Null test**: restar dos versiones; si da silencio, son iguales. *En música:* la forma de comprobar si un plugin de
  "emulación" cambia algo con un ajuste dado, o si dos exportaciones de un tema son idénticas.
- **LUFS**: la unidad de sonoridad de la norma BS.1770, la que usan Spotify, YouTube y Apple Music para igualar los
  temas. *En música:* por eso un master muy aplastado no suena más fuerte en Spotify: lo bajan a ~−14 LUFS.
- **Gain matching (volumen igualado)**: comparar dos versiones a la misma sonoridad. *En música:* los ingenieros de
  mastering igualan el antes y el después para saber si su cadena mejoró algo o solo subió el volumen.
- **PLR (peak to loudness ratio)**: cuánto sobresalen los picos de la sonoridad media. *En música:* un tema de jazz
  acústico tiene PLR alto (golpes vivos); uno de EDM moderno, bajo (todo denso).
- **Prueba ciega (ABX)**: escuchar sin saber qué es cada versión. *En música:* las pruebas que muestran que mucha gente
  no distingue un MP3 de 320 kbps de un WAV.

### Errores comunes

- **Comparar saturadores sin igualar el volumen.** Gana siempre el más fuerte. Iguala por sonoridad, no por pico.
- **Igualar "a ojo" con el fader.** Medio dB de diferencia ya inclina la balanza y no se nota como "volumen".
- **Juzgar un plugin por el analizador.** El balance por octavas dice DÓNDE cambió algo, no si suena mejor. Primero el
  oído (a ciegas), después la tabla para entender por qué.
- **Oversampling 16x en todo "porque es mejor".** Con curvas suaves no se oye y en un proyecto grande multiplica la CPU.
- **Dejar Riptide en BYPASS para ahorrar CPU.** El bypass no ahorra (está pensado para comparar sin saltos). Si no lo
  usas, quítalo del slot.
- **Cuándo NO comparar contra referencias:** cuando todavía no sabes qué le falta a la pista. Elegir saturador es el
  último paso; primero el problema (falta cuerpo, falta presencia, falta pegamento), después la herramienta.

### Retos completados

(Ninguno todavía.)
