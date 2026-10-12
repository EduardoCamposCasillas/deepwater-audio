# APRENDIZAJE — Ebb (ducker para sidechain)

Cuaderno de mezcla y diseño sonoro del ducker. Se completa al final de cada fase (ver `CLAUDE.md`).

## Fase 1 — El reloj: tempo, compás y dónde va la canción

Ebb todavía no le hace nada al sonido: la señal pasa intacta. Lo que aprende en esta fase es **saber en qué momento
musical está**, porque un ducker es, antes que nada, un reloj: tiene que bajar el volumen *justo* cuando entra el
kick. Si no sabe dónde está el kick, todo lo demás da igual.

### Conceptos aprendidos

- **Tempo (BPM).** Negras por minuto. Una negra dura `60 / BPM` segundos: a 95 BPM (tempo típico de reggaetón),
  631.6 ms. Todo lo rítmico de una mezcla (el ducking, el release de un compresor, el pre-delay de una reverb, un
  delay) se puede medir en esa unidad.
- **Compás (time signature).** Cuántos tiempos hay en un compás y qué figura es el tiempo. 4/4 = 4 negras (casi todo el
  reggaetón, el trap y el house); 3/4 = 3 negras (vals); 6/8 = 6 corcheas agrupadas de 3 en 3 (se siente "en 2", con
  balanceo); 7/8 = 7 corcheas (cojo, irregular: muy usado en música experimental).
- **Subdivisiones.** Negra = 1 tiempo en 4/4; corchea = 1/2; semicorchea = 1/4. Un compás de 4/4 tiene **16
  semicorcheas**: son los 16 pasos (*steps*) del Channel Rack de FL. El dembow se escribe en esa cuadrícula.
- **PPQ (posición en negras).** Cómo el host le dice al plugin dónde va la canción: "llevamos 37.5 negras desde el
  inicio". Con eso y el compás se saca todo lo demás: en 4/4, 37.5 negras = compás 10 (empieza en 36), tiempo 2,
  tercera semicorchea.
- **El PlayHead (transporte).** En cada bloque de audio (unos 5–20 ms) FL le pasa a Ebb el tempo, el compás, la
  posición y si está reproduciendo. Ebb no lleva su propia cuenta: **se vuelve a sincronizar con FL en cada bloque** y
  entre dos reportes calcula cada muestra con `posición = inicio + n · BPM / (60 · frecuencia de muestreo)`. Por eso no
  se puede "desfasar" de FL aunque cambies el tempo o hagas loop.
- **Saltos.** Cuando FL hace loop (vuelve al inicio del patrón) o haces clic en la línea de tiempo, la posición salta.
  Ebb lo detecta comparando dónde esperaba estar con dónde dice FL que está. En la Fase 2 eso sirve para que una curva
  de ducking a medias no se arrastre al otro lado del salto.
- **Reloj libre.** Sin host (el Standalone) no hay quién informe la posición: Ebb cuenta solo a 120 BPM desde 0, y lo
  avisa en ámbar. Con FL parado, la posición se queda quieta donde FL la deja.

### Qué hace cada control (y qué muestra la pantalla)

- **BYPASS**: escuchar la señal original. Hoy no cambia nada (la señal ya pasa intacta), pero ya hace el fundido de
  20 ms que hará falta cuando Ebb baje volúmenes: cortar de golpe de una señal atenuada a la original sería un clic.
- **Estado**: ▶ Reproduciendo (sigue a FL), ■ Parado, ○ Reloj libre.
- **Compás . tiempo . semicorchea** (grande): dónde va la canción, contando desde 1. Las **luces** son los tiempos del
  compás (la primera, más brillante, es el "1"): deberían encenderse a la vez que el metrónomo de FL.
- **FL (B:S)**: la misma posición en el formato del contador de FL, compás:paso (paso = semicorchea del compás, 1–16
  en 4/4). Pon el contador de FL en Bar:Step:Tick para compararlos tal cual.
- **Tempo, Compás, Posición** (en negras, con 3 decimales), **Loop** (si FL tiene el loop activo), **Saltos**
  (cuántas veces la posición saltó estando en reproducción) y **Audio** (frecuencia de muestreo y tamaño del bloque
  que manda FL: el bloque cambia según el buffer de tu tarjeta en Options → Audio settings).

### Ejercicio guiado: comprobar que Ebb sigue a FL

1. En un proyecto nuevo, pon un **loop de batería** (o el patrón de dembow de la receta de abajo) y abre Ebb en un
   insert del Mixer (Ebb aparece bajo el fabricante **Deepwater**). Activa el **metrónomo** de FL.
2. Reproduce en **modo Pattern**. Mira las luces de Ebb: la primera se enciende con el "clic" agudo del metrónomo, las
   otras con los graves. El número grande avanza como el contador de FL, y la fila
   **FL (B:S)** coincide con él (pon el contador de la barra de FL en Bar:Step:Tick).
3. Cada vez que el patrón vuelve a empezar, **Saltos** sube 1: eso es un loop visto desde dentro del plugin.
4. Cambia el **tempo** con la canción sonando (de 95 a 100 BPM, arrastrando el número del tempo): la lectura de
   Tempo cambia, la posición sigue coincidiendo y Saltos **no** sube. Un cambio de tempo no es un salto.
5. Pasa a **modo Song**, con la canción parada haz clic en distintos puntos de la línea de tiempo: Ebb dice "Parado" y
   la posición salta a donde hiciste clic. Reproduce y haz clic a mitad de la canción: Saltos sube 1.
6. Cambia el **compás** del proyecto a 3/4 o 7/8 (en Options → Project general settings, o con un marcador de compás en
   la Playlist, según tu versión de FL): las luces pasan a 3 o 7 y la cuenta de tiempos se reinicia en cada compás.
7. Abre el **Standalone** (`Ebb.exe`): estado en ámbar, "Reloj libre", 120 BPM contando solo. Eso es lo que hará Ebb
   cuando no haya nadie que le diga el tempo.

### Receta: medir en tiempo musical (tabla de tu tempo)

Para que un efecto "respire" con la canción, sus tiempos deben medirse en notas, no en milisegundos al azar. La
cuenta es siempre `ms = 60000 / BPM × negras`:

| Figura | Negras | 90 BPM | 95 BPM | 100 BPM |
|---|---|---|---|---|
| Compás (4/4) | 4 | 2667 ms | 2526 ms | 2400 ms |
| Negra (1/4) | 1 | 667 ms | 632 ms | 600 ms |
| Corchea (1/8) | 0.5 | 333 ms | 316 ms | 300 ms |
| Tresillo de corchea (1/8 T) | 1/3 | 222 ms | 211 ms | 200 ms |
| Semicorchea (1/16) | 0.25 | 167 ms | 158 ms | 150 ms |

Uso en mezcla hoy mismo, con cualquier compresor: si quieres que el bajo "vuelva" entre dos kicks que están a una
negra, el release del compresor de sidechain no debe pasar de esa negra (≈600 ms a 100 BPM); si dura más, el bajo nunca
termina de volver antes del siguiente golpe y la mezcla pierde graves. Ebb, desde la Fase 2, medirá su curva
directamente en estas figuras.

El **dembow clásico** en la cuadrícula de 16 pasos (4/4): kick en los pasos 1, 5, 9 y 13 (cada tiempo) y la caja en
los pasos **4, 7, 12 y 15**. En la notación de Ebb, la caja cae en 1.1.4, 1.2.3, 1.3.4 y 1.4.3. Ese "boom — ch-boom —
chick" con la caja adelantada es lo que da el balanceo.

### Reto sin receta

Programa un dembow **experimental** en el Channel Rack: deja la caja en su sitio (4, 7, 12, 15) pero mueve el kick para
que NO caiga en cada tiempo (por ejemplo, quita uno y adelanta otro una semicorchea). Con Ebb abierto y la canción a
94 BPM:

1. Para la canción justo en cada golpe del kick (o avanza paso a paso) y anota en qué posición de Ebb cae cada uno.
2. Calcula cuántos milisegundos hay entre cada par de kicks consecutivos.
3. Responde: ¿hay alguna división fija (1/4, 1/8, 1/16) que dispare un ducking exactamente en todos tus kicks y en
   ningún otro sitio? ¿Qué pasaría si usaras 1/16?

Inténtalo y después pide pistas.

### Vocabulario

- **BPM**: negras por minuto. *En música:* el reggaetón clásico va a 88–100 BPM; el house, a 120–128; el trap, a
  130–150 (aunque se siente a la mitad).
- **Compás / time signature**: cuántos tiempos por compás y de qué figura. *En música:* casi todo el pop es 4/4; "Money"
  de Pink Floyd está en 7/4.
- **Negra, corchea, semicorchea**: 1, 1/2 y 1/4 de tiempo en 4/4. *En música:* los hi-hats del trap van en
  semicorcheas (y en fusas en los "rolls").
- **Tresillo**: tres notas en el espacio de dos. *En música:* el "flow" en tresillos de Migos ("Versace") o los hi-hats
  en tresillo del trap.
- **Paso (step)**: cada semicorchea del Channel Rack; 16 por compás de 4/4.
- **PPQ**: posición en negras desde el inicio de la canción; lo que el host le pasa a los plugins para que se
  sincronicen.
- **Transporte / PlayHead**: el reporte de reproducción del host (play/stop, tempo, posición, loop).
- **Dembow**: el patrón de batería base del reggaetón, que viene del dancehall jamaiquino ("Dem Bow" de Shabba Ranks,
  1990) y del reggae en español panameño. *En música:* "Gasolina" de Daddy Yankee lo usa en estado puro.
- **Sidechain** (adelanto de la Fase 2): una señal controla lo que le pasa a otra. *En música:* el bombeo de los pads en
  "One More Time" de Daft Punk.

### Errores comunes

- **Proyecto a un tempo y el sample a otro.** Ebb sigue el tempo del **proyecto**, no el del audio. Si arrastras un loop
  de 98 BPM a un proyecto de 95 sin estirarlo, sus golpes se van alejando de la cuadrícula y el ducking por tempo (Fase
  2) caerá fuera de lugar. Para eso existirán los modos MIDI (Fase 4) y audio (Fase 5).
- **Kicks humanizados o con swing.** Si el kick está desplazado unos milisegundos de la cuadrícula (a propósito, por
  groove), una curva por tempo lo pisará siempre un poco antes o después. Lo mismo: modo audio.
- **Fiarse de la pantalla al milisegundo.** La GUI se actualiza 30 veces por segundo y el audio que oyes sale con la
  latencia del buffer de la tarjeta: la lectura y el sonido pueden ir unos milisegundos desfasados. El audio, en cambio,
  es exacto a la muestra.
- **Pensar que "Parado" es un error.** Con FL parado no hay tiempo que avanzar: la posición se queda quieta a propósito.

### Retos completados

(Ninguno todavía.)

## Fase 2 — Modo tempo: la curva de ducking

Ahora Ebb sí toca el sonido: en cada golpe de la división elegida **baja el volumen y lo deja volver** siguiendo una
curva. Eso es el *ducking* ("agacharse"): la señal se agacha para dejarle sitio a otra. En el modo tempo el golpe no lo
da un kick real sino el reloj de la Fase 1: Ebb sabe que en 1.1.1, 1.2.1, 1.3.1… toca bajar.

### Conceptos aprendidos

- **Curva de ganancia.** El plugin multiplica cada muestra por un número entre 0 y 1 que cambia en el tiempo: 1 = pasa
  entera, 0.5 = la mitad de amplitud (−6 dB), 0 = silencio. Ese número, dibujado en el tiempo, es la curva que ves en la
  pantalla. Es lo mismo que hace un compresor con sidechain, pero dibujado a mano en lugar de calculado a partir del kick.
- **La curva sale de la posición, no de un contador.** En cada muestra Ebb calcula "¿cuánto llevo desde el último
  golpe?" a partir de la posición de FL. Por eso un loop, un salto o un cambio de tempo no la desfasan nunca.
- **Profundidad en % y en dB.** El % es de amplitud: 50 % deja la mitad de la onda = −6 dB; 30 % ≈ −3 dB; 75 % =
  −12 dB; 100 % = silencio (−∞). Por eso los primeros % "suenan" poco y los últimos mucho: el oído escucha en dB.
- **Duración.** Qué parte del ciclo tarda el volumen en volver. Con 1/4 a 95 BPM (632 ms por negra), duración 50 % =
  vuelve en 316 ms y el resto del tiempo suena entero.
- **Forma de la subida.** Por ahora fija: un *smoothstep* (arranca plana, sube rápido por el medio y llega plana). La
  parte plana del principio es la que deja pasar el kick; la llegada plana evita una "esquina" en el volumen. En la
  Fase 3 llegan más formas y un editor.
- **Por qué un cambio brusco de volumen hace clic.** Si la ganancia salta de 1 a 0.2 de una muestra a la siguiente, la
  forma de onda también salta: un salto vertical contiene todas las frecuencias a la vez y se oye como un "tic". En
  un bajo (onda lenta y redonda) se oye clarísimo; en un hi-hat, casi nada.
- **Suavizado.** Un filtro de 1 polo sobre la ganancia: en cada muestra recorre un porcentaje fijo de lo que le falta.
  "2 ms" = en 2 ms ya hizo el 95 % del cambio. Redondea la caída del golpe sin quitarle el carácter.
- **Glide (cambios sin clic).** Si cambias la división sonando, la curva nueva podría estar en otro sitio (a medio
  subir contra abajo del todo). Ebb mide esa diferencia en el instante del cambio y la desvanece en 30 ms.
  Profundidad y duración van con rampas de 20 y 50 ms. Al **parar** FL el volumen vuelve al 100 % con el mismo glide
  (parado no hay ducking: si no, el bajo quedaría atenuado al previsualizar notas).

### Qué hace cada control

- **DIVISIÓN**: cada cuánto hay golpe. 1/4 = en cada tiempo (el kick "four on the floor" y el dembow clásico). 1/8 y
  1/16 = bombeo rápido, tipo trémolo rítmico. 1/2 y 1 bar = respiraciones largas (pads). T = tresillo: 3 golpes donde
  caben 2 (1/4 T = 6 golpes por compás). La cuadrícula se reinicia en cada compás.
- **PROFUNDIDAD**: cuánto se hunde la curva (la caída en la pantalla y en el medidor de la derecha, en dB).
- **DURACIÓN**: lo ancho que es el "valle" (la pantalla dice en cuántos ms vuelve).
- **SUAVIZADO**: lo redondeada que es la caída. En la pantalla, a 0 ms la caída es vertical; súbelo a 20 ms y verás la
  rampa.
- **Medidor** (barra vertical): cuántos dB está bajando Ebb ahora mismo (0 arriba, −24 abajo). Cae al instante y vuelve
  a 30 dB/s para que se vea cada golpe.

### Ejercicio guiado: oír el ducking (con el volumen igualado)

1. Proyecto a **95 BPM**. Un kick en los pasos 1, 5, 9 y 13 y un **808 o bajo sostenido** (una nota larga, por ejemplo
   un F3 de FL = 87 Hz que dure 2 compases), cada uno en su insert del Mixer. Ebb en el insert del bajo.
2. Escucha **la mezcla (kick + bajo), no el bajo solo**: el ducking existe para que el kick se oiga mejor.
3. **Sutil:** 1/4, profundidad 25 % (−2.5 dB), duración 40 %, suavizado 2 ms. Alterna BYPASS: el bajo casi no cambia,
   pero el golpe del kick se vuelve más nítido y los picos del Master bajan. Como el nivel apenas cambia, no hace
   falta compensar.
4. **Exagerado:** profundidad 80 %, duración 90 %. Ahora el bajo "bombea" y suena más bajo en promedio: para comparar
   en serio con BYPASS, sube el fader del bajo hasta que con Ebb se sienta igual de fuerte que sin él (unos 3–5 dB). Si
   no lo igualas, el oído elige "el más fuerte" y no el mejor.
5. **El clic:** profundidad 100 %, suavizado **0 ms**, el bajo en solo. Cada golpe hace "tic". Sube a 0.5, 1, 3 y
   10 ms: el tic desaparece hacia 1–2 ms, y con 10 ms el golpe pierde fuerza (la caída llega tarde, el kick ya pasó).
6. Con la canción sonando, cambia la **DIVISIÓN** de 1/4 a 1/8 T y mueve **DURACIÓN** rápido: no debe haber ningún
   clic. Haz loop de 1 compás en la Playlist: el golpe del "1" siempre cae con el kick.

### Receta: dembow con el bajo limpio

Kick dembow clásico (pasos 1, 5, 9 y 13) y un 808 largo que ocupa los mismos 40–120 Hz.
- Ebb en el 808: **1/4, profundidad 30–40 % (−3 a −4.4 dB), duración 35 %, suavizado 2 ms.**
- Por qué: el kick dura unos 100–200 ms; con el 35 % de 632 ms el 808 vuelve en ~220 ms, justo cuando el kick se
  apaga. 3–4 dB bastan para que el kick "salga" sin que se oiga el bombeo, y el pico de la suma kick + 808 baja (más
  margen para el limitador del Master).
- Si en tu dembow el kick NO cae en los cuatro tiempos, este modo bajará el volumen donde no hay kick: para eso serán
  los modos MIDI (Fase 4) y audio (Fase 5).

Variación creativa (house francés, pads que respiran): Ebb en un pad, 1/4, **profundidad 85 %, duración 95 %,
suavizado 4 ms**. El pad desaparece en cada tiempo y vuelve justo antes del siguiente: el pumping de Daft Punk.

### Reto sin receta

Sobre el dembow (kick en cada tiempo, caja en 4, 7, 12 y 15) pon Ebb en un **pad o acorde largo** y encuentra una
división y una duración con las que el pad respire **con la caja** y no con el kick. ¿Hay alguna división fija que
caiga en los cuatro golpes de la caja? ¿Cuál se acerca más, y en cuáles falla?

Inténtalo y después pide pistas.

### Vocabulario

- **Ducking**: bajar una señal para dejarle sitio a otra. *En música:* la música de fondo que baja cuando habla el
  locutor de radio.
- **Pumping (bombeo)**: el ducking exagerado convertido en efecto. *En música:* "One More Time" de Daft Punk.
- **Gain reduction (reducción de ganancia)**: cuántos dB se está bajando en un momento dado; lo que marca el medidor.
- **Clic / discontinuidad**: un salto brusco en la forma de onda. *En música:* el "tic" al cortar un sample sin fundido.
- **Polirritmia (3 contra 2)**: un ducking en 1/4 T contra un kick en 1/4 son 3 golpes contra 2. *En música:* el
  tresillo contra el pulso binario está en todo el afrobeat y en muchos breaks de trap.
- **Four on the floor**: kick en los cuatro tiempos. *En música:* el house y el reggaetón clásico comparten ese kick.

### Errores comunes y cuándo NO usarlo

- **Juzgarlo en solo.** Un bajo con ducking en solo suena "raro" a propósito; lo que importa es cómo suena con el kick.
- **Demasiada profundidad para limpiar.** Si se oye el bombeo cuando no querías efecto, baja a 15–30 %.
- **Duración demasiado larga en mezcla.** Con duración 100 % el bajo nunca está entero: pierdes graves y peso.
- **Suavizado alto "para que no haga clic".** Más de ~5 ms ya no quita clics: retrasa la caída y el kick pasa antes de
  que el bajo se aparte. Sube el suavizado desde 0 hasta que desaparezca el clic, y no mucho más.
- **Ducking en secciones sin kick.** El modo tempo baja el volumen aunque no haya kick (un break, una intro): ahí
  automatiza el BYPASS o espera a los modos MIDI y audio.
- **Cuándo NO usarlo:** si el bajo y el kick no se pisan (un 808 que *es* el kick, o un bajo que empieza en 150 Hz); en
  el Master entero con profundidad alta (bombea todo, voces incluidas); y con patrones de kick irregulares, donde el
  modo tempo baja el volumen en los sitios equivocados.

## Fase 3 — Formas de curva y editor

Hasta ahora la caída y la vuelta del volumen tenían una sola forma. Ahora la forma se **dibuja**: es una curva por
puntos que puedes elegir de un menú (8 formas de fábrica) o editar con el ratón. La forma decide el *carácter* del
ducking: con la misma profundidad y la misma duración, una curva "Rápida" casi no se oye y una "Lenta" bombea muchísimo.

### Conceptos aprendidos

- **Curva por puntos (breakpoints).** Una lista de puntos (x, y) unidos por tramos. Es lo mismo que una envolvente
  ADSR o la automatización de FL, pero libre: tantos puntos como quieras (hasta 32). En Ebb, x va del golpe (0) al
  final de la DURACIÓN (1), e y es el volumen (arriba entero, abajo la profundidad completa). Así la misma forma vale
  para cualquier profundidad y duración.
- **Tensión.** Cada tramo puede ser recto o curvarse. La curva es una exponencial normalizada, `(e^(k·t) − 1) / (e^k − 1)`:
  siempre une sus dos puntos, y según el signo de k arranca lenta y termina rápida o al revés. Es la forma en que se
  carga un condensador, y por eso suena "natural" en volumen: el oído escucha en dB (de forma logarítmica), y una
  subida exponencial se percibe más pareja que una recta.
- **Por qué la forma importa tanto.** Lo que el oído percibe como "cuánto bombea" es el área bajo la curva: cuánto
  tiempo pasa el sonido atenuado. "Lenta" se queda abajo casi todo el ciclo (mucha energía quitada: bombeo); "Rápida"
  vuelve enseguida (casi nada quitado: limpia el golpe del kick y ya).
- **Escalón vertical.** Dos puntos en la misma x hacen un salto instantáneo ("Puerta"). Un salto de volumen es un clic,
  así que en esas formas el SUAVIZADO vuelve a ser importante.
- **La forma no se automatiza.** Las perillas son parámetros (FL puede automatizarlas); la curva no, porque tiene un
  número variable de puntos. Se guarda con el proyecto igual, como parte del estado del plugin.
- **Cambiar de forma sonando no hace clic.** Ebb usa el mismo truco que con la división: mide la diferencia entre la
  forma vieja y la nueva en ese instante y la desvanece en 30 ms. Puedes arrastrar puntos con la canción sonando.
- **"Clásica" = la forma de la Fase 2.** Aquella era una fórmula (smoothstep); ahora es una curva de 5 puntos con
  tensiones ajustadas para imitarla (se desvía como mucho 0.03 dB). Tus proyectos de la Fase 2 suenan igual.

### Qué hace cada control

- **FORMA** (menú): carga una forma de fábrica en el editor. Si después la editas, el menú dice "Personalizada".
  - *Clásica*: arranca plana, sube por el medio, llega plana. Todoterreno.
  - *Lineal*: sube a ritmo constante. Sirve de referencia para comparar.
  - *Rápida*: vuelve enseguida. Ducking de mezcla que casi no se oye.
  - *Lenta*: se queda abajo y sube al final. El "pumping" del house.
  - *Retención*: abajo y quieta un tercio de la duración, luego sube. Para kicks largos (un 808 que suena 200 ms).
  - *Entrada suave*: baja en el primer 10 % en vez de al instante. Sin clic aunque el suavizado esté en 0.
  - *Eco*: un segundo golpe más débil a la mitad. Ritmo extra con un solo disparo.
  - *Puerta*: silencio hasta la mitad y vuelve de golpe. Efecto "stutter" / trance gate.
- **Editor** (el panel grande): arrastra los puntos (los de los extremos solo suben y bajan). **Mayús** mientras
  arrastras = se pega a la cuadrícula (octavos de la duración, cuartos de volumen). **Doble clic** en un hueco añade un
  punto; doble clic (o clic derecho) en un punto lo quita. El **círculo hueco** a mitad de cada tramo es su tensión:
  arrástralo hacia arriba o abajo para curvar el tramo hacia ese lado; doble clic lo vuelve recto.
- La **vista previa** (arriba a la derecha) muestra el resultado real: tu forma × profundidad, estirada por la
  duración y con el suavizado. Mírala mientras editas.

### Ejercicio guiado: oír las formas (con el volumen igualado)

1. El montaje de la Fase 2: **95 BPM**, kick en 1, 5, 9 y 13, un **808 largo** (F3 de FL = 87 Hz) y Ebb en el 808.
   División 1/4, profundidad 60 %, duración 70 %, suavizado 2 ms.
2. Cambia la FORMA entre **Rápida, Clásica y Lenta** con la canción sonando (no hay clic). Fíjate en el medidor: el
   pico de reducción es el mismo (60 % = −8 dB), pero el 808 suena cada vez más bajo **en promedio**.
3. **Iguala el volumen**: con *Lenta*, sube el fader del 808 hasta que se sienta igual de fuerte que con *Rápida*
   (suele hacer falta 3–5 dB). Ahora compara: con *Rápida* el kick asoma y el 808 casi no se mueve; con *Lenta* el 808
   "respira" con el tempo. Sin igualar, habrías elegido la más fuerte, no la mejor.
4. **El clic y la Entrada suave**: profundidad 100 %, suavizado **0 ms**, 808 en solo. Con *Clásica* cada golpe hace
   "tic". Cambia a *Entrada suave*: el tic desaparece sin tocar el suavizado, porque la caída dura unos milisegundos.
   El precio: el 808 baja un poco después de que entra el kick.
5. **Dibuja la tuya**: elige *Lineal*, añade un punto con doble clic a mitad de la pantalla y arrástralo abajo a la
   izquierda (x ≈ 25 %, y ≈ 10 %). Acabas de hacer una "Retención" corta. Curva el tramo de subida con el círculo hueco
   y escucha cómo cambia la vuelta.

### Receta: dembow con kick largo (808 de cola larga)

Cuando el kick es un 808 con cola (200–300 ms), "Clásica" empieza a subir mientras el kick todavía suena y el bajo lo
tapa. Ebb en el bajo: **FORMA Retención, 1/4, profundidad 45 % (−5 dB), duración 55 %, suavizado 2 ms.**
- Por qué: a 95 BPM una negra son 632 ms; el 55 % son 348 ms, y la Retención se queda abajo el primer 35 % de eso
  (≈ 120 ms): justo el cuerpo del kick. Luego sube en S y llega entera antes del siguiente golpe.
- Si la cola del kick es más larga, sube la DURACIÓN, no la profundidad: el problema es *cuándo* vuelve el bajo, no
  *cuánto* baja.

Variación creativa (trance gate en un pad): FORMA *Puerta*, división **1/16**, profundidad 100 %, duración 100 %,
suavizado 1.5 ms. El pad se corta en semicorcheas como un "chop". Mueve el punto del escalón (a la izquierda = notas
más cortas, como un staccato).

### Reto sin receta

En el reto de la Fase 2 viste que ninguna división fija cae en los cuatro golpes de la caja del dembow (pasos 4, 7, 12
y 15). Con el editor sí se puede: usa la división **1 bar** y **duración 100 %**, así la curva ocupa el compás entero.
Dibuja una curva que baje **solo en los pasos de la caja**. ¿En qué x tiene que caer cada bajada? (Para empezar: cada
paso es 1/16 del compás.) ¿Qué pasa si en otra sección el patrón cambia?

Inténtalo y después pide pistas.

### Vocabulario

- **Envolvente / breakpoint envelope**: una forma que cambia algo en el tiempo, definida por puntos. *En música:* la
  envolvente de volumen de un pad (ataque lento) o la automatización de un filtro en un build-up de EDM.
- **Tensión / curvatura**: cuánto se dobla un tramo. *En música:* un fade-out exponencial suena más natural que uno
  lineal; casi todos los finales en fade de la radio de los 70 y 80 se hacían así con el fader.
- **Hold (retención)**: mantener un valor quieto un tiempo antes de seguir. *En música:* el hold de la envolvente de un
  sampler de batería, o de un noise gate.
- **Trance gate / stutter**: cortar un sonido sostenido en ritmo. *En música:* los pads cortados en semicorcheas del
  trance de finales de los 90 y los "chops" vocales del future bass.
- **Área bajo la curva**: cuánta energía se quita; lo que el oído percibe como intensidad del bombeo.

### Errores comunes y cuándo NO usarlo

- **Comparar formas sin igualar el volumen.** Las formas que se quedan abajo más tiempo bajan el nivel promedio; si no
  compensas, te engaña la diferencia de volumen y no la forma.
- **Escalones sin suavizado.** *Puerta* o un punto arrastrado en vertical sin suavizado = clics. Sube el SUAVIZADO a
  1–2 ms o usa una pendiente corta en vez de un escalón.
- **Terminar la curva abajo sin querer.** Si el último punto no está arriba, pasada la duración el volumen se queda
  bajo hasta el siguiente golpe: el bajo pierde nivel todo el tiempo. A veces es lo que quieres (un ducking que nunca
  suelta del todo); casi siempre, no.
- **Formas complicadas para limpiar una mezcla.** Para que el kick pase limpio basta con *Rápida* o *Clásica* y poca
  profundidad. Las formas con varias bajadas son efectos, no limpieza.
- **Dibujar el patrón del kick en el editor como solución general.** Funciona en un loop fijo (el reto), pero si el
  kick cambia de sección a sección tendrías que redibujar: para eso llegan los modos MIDI (Fase 4) y audio (Fase 5).

## Fase 4 — Modo MIDI: que el ducking siga al kick de verdad

En el modo tempo la curva cae en una cuadrícula fija: cada 1/4, cada 1/8… Pero en el dembow experimental el kick se
mueve: cae en el 1, en el paso 7, se salta un tiempo, cambia en el coro. En el **modo MIDI** la cuadrícula desaparece:
**cada nota MIDI que le llega a Ebb dispara la curva**, justo en la muestra en la que cae la nota. Si le mandas las
mismas notas que tocan el kick, el ducking cae siempre con el kick, siga el patrón que siga.

### Conceptos aprendidos

- **Disparo (trigger).** Un evento que arranca algo: aquí, una nota MIDI (note-on) que reinicia la curva. Ebb ignora el
  note-off y la duración de la nota: solo importa *cuándo* empieza. Es como pegarle a un pad de batería.
- **Exacto a la muestra.** FL le pasa a Ebb el audio en bloques de unos 5–20 ms, y cada nota viene con su posición
  dentro del bloque. Ebb no espera al bloque siguiente: arranca la curva en esa muestra exacta (a 44.1 kHz, con un
  error de 0.02 ms). Por eso el ducking cae igual de justo con un buffer de 64 que de 4096.
- **El largo de la curva.** Sin cuadrícula, DIVISIÓN pasa a llamarse **LARGO**: cuánto dura la curva después de cada
  nota, en figuras musicales (1/4 = una negra a tu tempo). Con DURACIÓN la recortas: largo 1/4 y duración 50 % = la
  curva dura media negra. Sigue ligada al tempo: si subes el BPM, la curva se acorta con la canción.
- **Redisparo.** Si llega otra nota antes de que la curva termine, vuelve a empezar **desde donde esté el volumen**, no
  desde arriba (el suavizado se encarga de que no haya clic). Dos kicks seguidos = dos bajadas, aunque estén a una
  semicorchea.
- **Entre notas.** Antes de la primera nota no hay ducking. Cuando la curva termina, el volumen se queda en el valor
  del último punto hasta la siguiente nota (con las formas de fábrica, arriba del todo). Al **parar** FL, la curva que
  sonaba se suelta con un fundido. Con FL parado las notas siguen disparando: puedes probarlo tocando un teclado.
- **Velocidad (velocity).** La fuerza con la que se toca la nota (1–127). Con **VELOCIDAD** encendido, la profundidad
  se multiplica por ella: una nota a 64 baja la mitad que una a 127. Sirve cuando el kick tiene golpes fantasma más
  suaves: el bajo se aparta menos para ellos.
- **Filtro de nota.** Si mandas todo el patrón de batería por el mismo canal (kick en C5, caja en D5…), con **NOTA
  MIDI** eliges cuál dispara. FL nombra las notas con una octava más que otros programas: el do central (MIDI 60) es
  **C5** en FL y C4 en Ableton o en los libros.

### Cómo mandar MIDI a Ebb en FL Studio (el ruteo)

Ebb está en el Mixer y el Mixer no tiene notas: las notas viven en el Channel Rack. Se conectan con un **puerto MIDI**
interno de FL, un "cable" numerado:

1. Pon Ebb en el insert del bajo (como en las fases anteriores) y ponlo en **MODO MIDI**.
2. En la ventana de Ebb, abre el menú del envoltorio de FL (el **engranaje / flecha** arriba a la izquierda de la
   ventana del plugin) → **Settings** (o la pestaña *Processing*, según tu versión). En la sección **MIDI**, pon
   **Input port** en un número libre, por ejemplo **10**.
3. En el **Channel Rack**, añade un canal **MIDI Out** (botón **+** → MIDI Out). En su ventana, pon **Port** en el
   **mismo número (10)**. El MIDI Out no suena: solo manda notas por ese puerto.
4. Escribe en el MIDI Out las notas del kick: en el step sequencer, enciende los mismos pasos que el kick; o en el
   Piano roll del kick, selecciona todo (Ctrl+A), copia (Ctrl+C) y pégalo en el Piano roll del MIDI Out (Ctrl+V).
5. Reproduce: la luz de **ACTIVIDAD MIDI** de Ebb se enciende en verde agua con cada kick. Si se enciende en **ámbar**,
   las notas llegan pero no disparan (Ebb está en modo Tempo, o NOTA MIDI no es esa nota). Si **no se enciende**, los
   números de puerto no coinciden.

**Truco: escribir el kick una sola vez.** Crea un canal **Layer** (+ → Layer) y pon el kick y el MIDI Out como sus
hijos (selecciona los dos en el Channel Rack y, en la ventana del Layer, pulsa *Set children*). Ahora programas las
notas solo en el Layer y suenan el kick y el disparo de Ebb a la vez: si cambias el patrón en el coro, el ducking
cambia solo.

En el **Standalone** (Ebb.exe) puedes probar con un teclado MIDI: Options → Audio/MIDI Settings → activa tu teclado
en *Active MIDI inputs*.

### Qué hace cada control

- **MODO**: *Tempo* (la cuadrícula de las fases anteriores) o *MIDI* (cada nota dispara). Cambiar de modo con la
  canción sonando no hace clic.
- **NOTA MIDI**: *Todas* o una sola nota. Atenuado en modo Tempo.
- **LARGO** (la DIVISIÓN en modo MIDI): cuánto dura la curva tras cada nota, al tempo de FL. La vista previa de arriba
  muestra un solo golpe y cuántos ms tarda en volver.
- **ACTIVIDAD MIDI**: la luz (verde agua = disparó, ámbar = llegó sin disparar) y la última nota recibida.
- **USAR ÚLTIMA NOTA**: pone en NOTA MIDI la última nota que llegó. Toca (o reproduce) el kick y pulsa: así no tienes
  que saber cómo se llama la nota.
- **VELOCIDAD**: la velocidad de cada nota escala la profundidad. Atenuado en modo Tempo.
- **PROFUNDIDAD, DURACIÓN, SUAVIZADO y FORMA** hacen lo mismo que en modo tempo.

### Ejercicio guiado: tempo contra MIDI sobre un dembow desplazado (con el volumen igualado)

1. **95 BPM**. Un kick "experimental": pasos **1, 7, 11 y 15** del compás (el segundo kick se adelanta al "y" del 2;
   el tercero y el cuarto caen a destiempo). Un **808 largo** (F3 de FL = 87 Hz) en su insert, con Ebb.
2. **Modo Tempo**, 1/4, forma Clásica, profundidad 50 %, duración 40 %, suavizado 2 ms. Escucha la mezcla: el 808 se
   agacha en los tiempos 1, 2, 3 y 4, pero solo el primero coincide con un kick. En los demás, el 808 baja *donde no
   hay kick*, y los kicks de los pasos 7, 11 y 15 chocan con el 808 entero. Mira el medidor: baja en el sitio
   equivocado.
3. Haz el ruteo de arriba (MIDI Out con los pasos del kick) y pasa a **Modo MIDI** con LARGO 1/4. Ahora cada bajada
   cae con su kick. Alterna entre Tempo y MIDI con la canción sonando: es el mismo ducking, puesto en el sitio correcto
   o en el equivocado.
4. **Iguala el volumen** antes de juzgar: los dos modos bajan una cantidad parecida de energía, pero compruébalo con el
   BYPASS y el fader del 808 (súbelo 1–2 dB en el modo que suene más bajo).
5. **Redisparo:** añade un kick extra en el paso **8** (justo después del 7). Con LARGO 1/4 la segunda bajada llega
   cuando la primera todavía está subiendo: fíjate en que no hay clic y en que el 808 se queda abajo un poco más.
6. **Velocidad:** pon el kick del paso 15 a velocidad 50 % (en el MIDI Out, o en el Layer) y enciende VELOCIDAD. Ese
   golpe ahora baja el 808 la mitad. Apágalo y compara.

### Receta: dembow experimental con el bajo limpio

Kick irregular que cambia por sección, 808 largo. Ebb en el 808: **Modo MIDI, notas del kick vía Layer (kick + MIDI
Out), LARGO 1/4, FORMA Clásica (o Retención si el kick tiene cola), profundidad 35–45 %, duración 45 %, suavizado
2 ms, VELOCIDAD encendido si el kick tiene golpes fantasma.**
- Por qué MIDI: el 808 solo se aparta cuando de verdad hay un kick, y el patrón puede cambiar en cada sección sin
  tocar Ebb.
- Por qué LARGO 1/4 × 45 %: a 95 BPM son unos 285 ms, lo que dura un kick de reggaetón. Si dos kicks están a una
  semicorchea (158 ms), el segundo redispara a mitad de camino y el 808 se queda abajo durante la ráfaga, que es lo
  que quieres.
- Si el kick es un 808 de cola larga, sube el LARGO a 1/2 en lugar de la profundidad: el problema es *cuándo* vuelve
  el bajo.

Variación creativa: Ebb en un pad, Modo MIDI, forma *Puerta*, LARGO 1/16, profundidad 100 %. Escribe en el MIDI Out un
ritmo *distinto* del kick (por ejemplo, el de la caja): el pad se corta siguiendo ese ritmo. Un "trance gate"
programado nota a nota.

### Reto sin receta

En el reto de la Fase 3 dibujaste la curva para que un pad respirara con la caja del dembow (pasos 4, 7, 12 y 15)
usando "1 bar". Hazlo ahora con el **modo MIDI**: ¿qué notas pones en el MIDI Out, qué LARGO eliges para que el pad
vuelva antes del siguiente golpe de caja, y qué pasa con el hueco largo entre el paso 7 y el 12? Después, cambia el
patrón de la caja en la segunda mitad de la canción: ¿qué tuviste que tocar en Ebb esta vez?

Inténtalo y después pide pistas.

### Vocabulario

- **MIDI**: el idioma de las notas. No es sonido, son instrucciones (qué nota, cuándo, con qué fuerza). *En música:*
  todo lo que escribes en el Piano roll de FL es MIDI hasta que un instrumento lo convierte en sonido.
- **Note-on / note-off**: el mensaje de "empieza la nota" y el de "suéltala". Ebb solo escucha el note-on.
- **Velocity (velocidad)**: la fuerza de la nota, 1–127. *En música:* los hi-hats "humanizados" del trap, con
  velocidades alternadas, suenan vivos; todos a 127 suenan a máquina.
- **Ghost note (nota fantasma)**: un golpe muy suave entre los principales. *En música:* los kicks y cajas fantasma del
  funk y del R&B (y del perreo más "sucio").
- **Trigger / retrigger (disparo / redisparo)**: arrancar un evento y volver a arrancarlo antes de que termine. *En
  música:* el "stutter" de un sample de voz redisparado en semicorcheas en el EDM.
- **Puerto MIDI**: un canal numerado por donde viajan las notas de un sitio a otro. En FL conecta un MIDI Out con un
  plugin del Mixer.

### Errores comunes y cuándo NO usarlo

- **Puertos distintos.** El error más común: la luz de Ebb no se enciende. El *Input port* de Ebb y el *Port* del MIDI
  Out tienen que ser el mismo número.
- **Dejar Ebb en Modo Tempo.** Las notas llegan (luz ámbar) pero no disparan.
- **Que el MIDI y el kick se separen.** Si editas el patrón del kick y te olvidas del MIDI Out, el ducking cae donde ya
  no hay kick. Usa el Layer para escribir las notas una sola vez.
- **Filtro en la nota equivocada.** Con NOTA MIDI en una nota concreta y el MIDI Out tocando otra, la luz se queda en
  ámbar. Usa USAR ÚLTIMA NOTA.
- **LARGO demasiado largo con kicks seguidos.** Si los kicks están muy juntos y la curva es larga, el bajo casi nunca
  vuelve arriba: pierdes graves en toda la sección. Acorta LARGO o DURACIÓN.
- **Cuándo NO usarlo:** si el kick es un audio grabado (un loop, un sample de batería entera) y no tienes sus notas
  MIDI, lo que necesitas es el modo audio (Fase 5). Si el kick es "four on the floor" de principio a fin, el modo
  tempo hace lo mismo con menos ruteo.

## Fase 5 — Modo audio: que el kick mismo dispare el ducking

El modo MIDI sigue al kick, pero necesita sus notas. Si el kick es un **audio** (un sample suelto en el Playlist, un
loop de batería, un kick grabado o con swing humano), no hay notas que copiar. En el **modo audio** Ebb *escucha* el
kick por una segunda entrada, la **entrada sidechain**, y dispara la curva en la muestra exacta en que empieza cada
golpe. Es lo que hace un compresor sidechain clásico, pero con la curva dibujada de Ebb en lugar de la reacción de un
compresor.

### Conceptos aprendidos

- **Entrada sidechain.** Ebb tiene dos entradas: la principal (el bajo, que se procesa y sale) y la sidechain (el kick,
  que *solo se escucha*: no sale por Ebb). Una señal controla lo que le pasa a otra: eso es literalmente "sidechain".
- **Envolvente.** El kick oscila cientos de veces por segundo; para saber "cuándo hay un golpe" Ebb lo convierte en una
  curva lenta de *qué tan fuerte suena* (la barra del medidor). Sube al instante con cada pico y baja en unos 50 ms.
- **Umbral (threshold).** El nivel a partir del cual hay golpe. Cuando la envolvente **cruza el umbral hacia arriba**,
  Ebb dispara la curva justo en esa muestra.
- **Histéresis (rearme).** Después de un golpe, Ebb no vuelve a disparar hasta que la envolvente baja **6 dB por debajo
  del umbral** (la línea punteada del medidor). Sin esto, la cola del kick, que ronda el umbral, dispararía diez veces.
  La consecuencia: entre dos kicks el nivel tiene que bajar de la punteada. Si la cola es tan larga que no baja, el
  kick siguiente no dispara (sube el umbral).
- **Retención (hold).** Tiempo mínimo entre dos golpes: lo que llegue antes se ignora. Algunos kicks tienen dos ataques
  (el "click" del principio y el cuerpo un instante después) o vienen en flam; la retención hace que disparen una vez.
- **Lookahead (mirar adelante).** El detector solo se entera del kick cuando ya sonó, y la ganancia tarda un par de ms
  en bajar (SUAVIZADO): el ataque del kick y el del bajo chocan un instante. Con LOOKAHEAD, Ebb **retrasa el bajo**
  unos ms y la curva empieza antes de que el bajo llegue: cuando suena el kick, el bajo ya está abajo.
- **Latencia y compensación (PDC).** Ese retraso es *latencia*. Ebb se la informa a FL y FL **atrasa todas las demás
  pistas lo mismo** (compensación de retardo, *plugin delay compensation*), así que en la mezcla nada se desalinea. Por
  eso el lookahead no se automatiza: cambiarlo obliga a FL a recolocar todo. Funciona en los tres modos: en tempo y en
  MIDI también adelanta la curva.

### Cómo configurar el sidechain en FL Studio (el ruteo)

1. Pon el kick en su propia pista del Mixer (por ejemplo, la **1**) y el bajo en otra (la **2**) con Ebb en un slot.
2. **Selecciona la pista del kick** (clic en ella). En la pista del **bajo**, haz **clic derecho en la flecha de envío**
   (el triangulito de abajo de la pista) → **Sidechain to this track**. El cable que aparece es "solo para escuchar":
   el kick no suena dos veces.
3. Abre Ebb y, en el envoltorio de FL, el **engranaje** (arriba a la izquierda) → pestaña **Processing**. En
   **Connections**, en la entrada **Sidechain** (o en el segundo puerto de entrada de Ebb), elige la pista del kick
   (**1**). Solo aparecen las pistas que mandaste con "Sidechain to this track". Según la versión de FL el nombre puede
   cambiar un poco; lo que importa es que la entrada 2 de Ebb sea el kick.
4. Pon Ebb en **MODO Audio** y reproduce. La **barra del medidor SIDECHAIN** tiene que moverse con cada kick. Si no se
   mueve, el sidechain no llega: revisa el paso 2 (que la pista del kick esté seleccionada al hacer clic derecho) y el 3.
5. **Ajusta el umbral** arrastrando la línea ámbar (o con UMBRAL) hasta que la luz se encienda en verde agua con
   **cada** kick y con nada más.

En el **Standalone** no hay sidechain que configurar (Options → Audio/MIDI Settings solo da la entrada principal); el
modo audio se prueba en FL.

### Qué hace cada control

- **MODO → Audio**: los golpes los detecta Ebb en el sidechain. El LARGO, la FORMA, la PROFUNDIDAD, la DURACIÓN y el
  SUAVIZADO funcionan igual que en MIDI.
- **Medidor SIDECHAIN**: la barra es el nivel del kick (verde agua cuando pasa el umbral). Línea **ámbar** = umbral
  (arrástrala); **punteada** = rearme (umbral − 6 dB). La luz: verde agua = disparó la curva; ámbar = se detectó un golpe
  pero Ebb no está en modo Audio. El medidor funciona en cualquier modo: sirve para comprobar el ruteo antes de cambiar.
- **UMBRAL**: −60 a 0 dB. Atenuado fuera del modo Audio.
- **RETENCIÓN**: 0 a 250 ms (30 por defecto). Atenuado fuera del modo Audio.
- **LOOKAHEAD**: 0 a 10 ms (0 por defecto = sin latencia). En los tres modos.

### Ejercicio guiado: MIDI contra audio sobre un kick con swing (con el volumen igualado)

1. **95 BPM**, el dembow desplazado de la Fase 4 (pasos 1, 7, 11 y 15), el **808 largo** con Ebb y el ruteo MIDI de la
   Fase 4 funcionando. Profundidad 50 %, LARGO 1/4, duración 40 %, suavizado 2 ms.
2. Haz además el ruteo sidechain de arriba. Alterna **MIDI ↔ Audio** con la canción sonando: deberían sonar casi
   igual. Si el audio suena distinto, mira la luz: ¿dispara en todos los kicks?
3. **Humaniza el kick:** en el Piano roll del kick, *Tools → Randomize* solo la posición (unos pocos ticks), o mueve a
   mano el kick del paso 11 un poco tarde. **No toques el MIDI Out.** En modo MIDI el ducking cae donde estaba la nota
   antigua (el 808 se agacha un instante antes del kick, y vuelve cuando el kick llega). En modo audio, sigue al kick
   donde esté. Ese es el caso del modo audio: el kick real manda.
4. **El umbral:** súbelo hasta −6 dB: algunos kicks dejan de disparar (luz apagada en esos golpes, el 808 no se
   aparta). Bájalo hasta −50 dB con un loop de batería entero en el sidechain: la caja y los hats también disparan.
   Busca el punto donde solo dispara el kick.
5. **El lookahead:** pon SUAVIZADO en 8 ms (una bajada lenta). Sin lookahead, el principio del kick choca con el 808
   (el kick pierde "pegada"). Sube LOOKAHEAD a 5 ms: el 808 ya está abajo cuando llega el kick. Compara con BYPASS e
   **iguala el volumen** con el fader del 808 antes de decidir.

### Receta: kick grabado (o sample con swing) y 808, limpio

Ebb en el 808: **Modo Audio, sidechain desde la pista del kick, UMBRAL unos 12–18 dB por debajo del pico del kick,
RETENCIÓN 30–50 ms, LOOKAHEAD 2–3 ms, LARGO 1/4, duración 40–50 %, profundidad 35–50 %, suavizado 2 ms.**
- Por qué el umbral tan abajo del pico: el golpe se detecta en los primeros ms del ataque (el disparo es más
  temprano) y aguanta que algunos kicks suenen más suaves que otros.
- Por qué 30–50 ms de retención: más que el doble ataque de un kick (10–20 ms), menos que la distancia mínima entre dos
  kicks del dembow (una semicorchea a 95 BPM = 158 ms).
- Por qué 2–3 ms de lookahead: lo justo para que el suavizado termine antes del ataque del kick. Más no mejora nada y
  suma latencia (molesta si tocas en vivo un instrumento que pasa por esa pista).

### Reto sin receta

Tienes un **loop de batería entero** (kick, caja y hats en un solo audio) y quieres que el bajo se aparte solo con el
kick. Ebb no tiene filtro en el sidechain (todavía). ¿Cómo consigues que la caja y los hats no disparen, usando solo el
Mixer de FL y Ebb? Pista de por dónde empezar: el kick vive en los graves y los demás no; ¿qué pasaría si la señal que
manda la pista del loop al sidechain no fuera el loop tal cual?

Inténtalo y después pide pistas.

### Vocabulario

- **Sidechain**: usar una señal para controlar el procesamiento de otra. *En música:* el bajo que "respira" con el kick
  en casi todo el EDM desde los 2000 (Daft Punk, Eric Prydz "Call on Me").
- **Envolvente (envelope)**: la forma del volumen de un sonido en el tiempo (ataque, cuerpo, cola). *En música:* un
  kick de 808 tiene un ataque corto y una cola larga; un kick de house, ataque y cola cortos.
- **Umbral (threshold)**: el nivel donde algo empieza a actuar. *En música:* lo tienen los compresores, los gates y los
  "triggers" de batería (los que cambian el kick grabado de una batería por un sample).
- **Histéresis**: tener un umbral para "encender" y otro más bajo para "apagar", para no parpadear en el borde. *En
  música:* los gates de batería la usan para no cortar a medias la cola de un tom.
- **Retención (hold)**: tiempo en que un detector ignora todo después de actuar. *En música:* el *hold* de un gate o el
  *retrigger time* de un reemplazador de batería.
- **Lookahead**: retrasar la señal para que el procesamiento "vea el futuro". *En música:* los limitadores de
  mastering lo usan para bajar el volumen antes de que llegue el pico.
- **Latencia / PDC**: el retraso que añade un plugin y la compensación que hace el DAW para alinear las pistas.

### Errores comunes y cuándo NO usarlo

- **El medidor no se mueve.** El sidechain no llega: falta "Sidechain to this track" o la entrada sidechain de Ebb en
  *Processing* no apunta a la pista del kick.
- **La luz se enciende en ámbar.** El kick llega pero Ebb sigue en modo Tempo o MIDI.
- **Disparos de más.** Umbral demasiado bajo con otros instrumentos en el sidechain (o un kick con doble ataque y la
  retención en 0). Sube el umbral o la retención; mejor aún, manda al sidechain solo el kick.
- **Kicks que no disparan.** Umbral demasiado alto, o la cola del kick anterior no baja de la línea punteada antes del
  siguiente. Mira el medidor: entre kicks la barra tiene que pasar a la izquierda de la punteada.
- **Retención más larga que la distancia entre kicks.** Con 200 ms y kicks a una semicorchea (158 ms), uno de cada dos
  no dispara.
- **Lookahead de más.** No suena mejor y suma latencia a todo el proyecto. Si grabas o tocas en vivo, déjalo en 0.
- **Cuándo NO usarlo:** si tienes las notas del kick y el patrón no tiene swing humano, el modo MIDI es más exacto (no
  depende de un umbral). Si el kick es "four on the floor" fijo, el modo tempo no necesita ningún ruteo.

## Fase 6 — Ducking por bandas: apartar solo el sub

Hasta ahora Ebb bajaba el volumen de **todo** el bajo. Pero el kick y el bajo solo se pelean en los graves (40–120 Hz):
el resto del bajo (el "gruñido" de un 808 saturado, el ataque de un bajo de dedo, los armónicos de un reese) no le
estorba al kick, y al bajarlo junto con el sub es cuando se **oye** el bombeo. Con el ducking por bandas Ebb parte la
señal en dos (graves y agudos), aplica la curva solo a una y las vuelve a juntar: el kick gana su hueco en el sub y el
bajo sigue sonando presente arriba.

### Conceptos aprendidos

- **Crossover (cruce).** Un par de filtros que parte la señal en dos bandas: un *pasa bajos* (lo de abajo del cruce) y
  un *pasa altos* (lo de arriba). La perilla CRUCE es la frecuencia donde se parten.
- **Linkwitz-Riley (LR4, 24 dB/oct).** Un crossover cualquiera tiene un problema: alrededor del cruce las dos bandas
  salen con **fases** distintas, y al sumarlas aparece un pozo o una joroba (el sonido cambia aunque no hagas nada).
  Linkwitz-Riley es el tipo de crossover cuyas bandas suman **exactamente plano**: con el ducking quieto, graves +
  agudos = la señal original, solo con un pequeño giro de fase alrededor del cruce (que no se oye como color). Es el
  mismo que usa el multibanda de Riptide (está en la librería compartida).
- **24 dB por octava.** Lo empinado del corte: una octava por debajo del cruce, la banda de agudos ya está 24 dB más
  baja (casi nada). Entre el cruce y una octava a cada lado las dos bandas se solapan: un cruce a 120 Hz afecta un
  poco de 60 a 240 Hz.
- **Mix dry/wet (mezcla en paralelo).** Mezclar la señal procesada con la original. En un efecto que solo cambia el
  volumen, mezclar al 50 % es **lo mismo** que bajar la profundidad a la mitad: (1 − mix)·x + mix·g·x = (1 − mix·(1 − g))·x.
  Ebb lo hace así por dentro, y además la original queda siempre en fase con la procesada (si se mezclara la original
  sin pasar por el cruce, en modo bandas habría un filtro de peine). Para qué sirve si equivale a la profundidad:
  para **automatizarlo** (meter o sacar el efecto en una sección) sin tocar la curva ni la profundidad que ya ajustaste.

### Qué hace cada control

- **BANDA**: *Todo* = como hasta la Fase 5 (la señal entera baja; con mix 100 % suena idéntico, bit a bit). *Graves* =
  solo lo de abajo del CRUCE: el uso normal (bajo o 808 contra kick). *Agudos* = solo lo de arriba: para que respiren
  los platillos, un pad brillante o la cola de una reverb sin tocar su sub. Cambiarla sonando hace un fundido de 20 ms
  (sin clic).
- **CRUCE**: 20 Hz a 5 kHz (120 Hz por defecto). Atenuada en *Todo*. Se mueve en octavas, sin chirridos al girarla.
- **MIX**: 0–100 % (100 por defecto). 0 % = sin ducking.
- **Vista BANDAS** (arriba del panel): la respuesta en frecuencia. La línea gris de arriba es la señal sin ducking
  (plana: las bandas suman 1); la de color es la respuesta en lo más hondo del golpe (PROFUNDIDAD × MIX), y lo relleno
  entre las dos es **lo que el ducking quita**. La línea blanca fina se mueve con cada golpe (en vivo). La punteada
  ámbar es el cruce. En *Todo* baja todo el espectro por igual; en *Graves* solo la parte izquierda.
- La vista previa de la curva y el medidor de reducción ya cuentan el MIX (muestran lo que suena).

### Ejercicio guiado: todo contra graves sobre un 808 con gruñido (con el volumen igualado)

1. **95 BPM**, el dembow de siempre, Ebb en el **808** con el modo que ya te funciona (MIDI o Audio). Ponle al 808 algo
   de saturación **antes** de Ebb (Riptide, modo bulbo, drive medio) para que tenga armónicos arriba. Profundidad 80 %,
   LARGO 1/4, duración 50 %, suavizado 2 ms. BANDA *Todo*.
2. Escucha el 808 **solo** (solo en el mixer) y luego con el kick: bombea de forma evidente, también el gruñido.
3. Cambia a **Graves** con CRUCE en 120 Hz. Mira la vista: ahora solo baja la parte de la izquierda. Escucha: el sub
   se sigue apartando del kick, pero el gruñido queda quieto. El 808 ya no "bombea", solo deja pasar al kick.
4. **Mueve el CRUCE** sonando: a 50 Hz el ducking solo toca el sub profundo (el kick vuelve a chocar con el cuerpo del
   808, hacia 80–100 Hz); a 400 Hz se come también parte del gruñido y vuelve el bombeo. Busca el punto más bajo en
   el que el kick suena limpio: suele estar entre 90 y 150 Hz.
5. **Iguala el volumen** antes de comparar *Todo* contra *Graves*: con *Todo* el 808 pierde más energía en total y
   suena más bajo; si no lo compensas con el fader, *Graves* "gana" solo por sonar más fuerte.
6. **El MIX:** deja *Graves* y baja el MIX a 50 %: la caída del sub es la mitad (mira la vista: el relleno se reduce).
   Automatiza el MIX en FL (clic derecho → *Create automation clip*) de 0 % en la intro a 100 % cuando entra el kick.

### Receta: 808 de dembow con gruñido, limpio sin que se note

Ebb en el 808 (después de la saturación): **Modo MIDI o Audio, BANDA Graves, CRUCE 100–130 Hz, profundidad 70–90 %,
LARGO 1/4, duración 35–50 %, suavizado 2–3 ms, MIX 100 %.**
- Por qué tanta profundidad: solo afecta al sub, que es justo lo que choca con el kick; los armónicos (lo que hace que
  el 808 se oiga en un celular) no se tocan, así que el efecto no se nota como bombeo.
- Por qué el cruce cerca de 120 Hz: el sub y el cuerpo del kick de reggaetón viven por debajo; el gruñido del 808 está
  por encima.
- Por qué la saturación va **antes** de Ebb: si va después, el saturador volvería a generar armónicos con el sub
  "agachado" y el gruñido bombearía igual.

### Reto sin receta

Tienes un **pad** con mucho cuerpo (200–500 Hz) y un brillo arriba. Quieres que **solo el brillo** "respire" con el
kick como efecto rítmico, sin que el cuerpo del pad se mueva, y que el efecto **entre poco a poco** durante el
pre-coro y esté completo en el coro. ¿Qué BANDA, qué CRUCE y qué haces con el MIX? Pista para el cruce: mira dónde
termina el cuerpo del pad con el analizador de Riptide (o el Wave Candy de FL) antes de girar la perilla.

Inténtalo y después pide pistas.

### Vocabulario

- **Crossover / frecuencia de cruce**: el punto donde se parte el espectro en bandas. *En música:* los altavoces de dos
  vías tienen uno (el woofer recibe los graves, el tweeter los agudos).
- **Multibanda**: procesar cada banda de frecuencias por separado. *En música:* los compresores multibanda de
  mastering (el "pumping" solo en los graves en el EDM de festival) y el modo multibanda de Riptide.
- **Linkwitz-Riley**: el tipo de crossover cuyas bandas suman plano. *En música:* el estándar en los crossovers de los
  sistemas de PA de conciertos.
- **dB por octava (pendiente)**: lo rápido que corta un filtro. 24 dB/oct es un corte firme; 6 dB/oct, uno muy suave.
- **Dry / wet (mix)**: señal original / procesada. *En música:* el knob "mix" de casi cualquier reverb o delay; en
  compresión se llama *compresión en paralelo* (la "New York compression" de las baterías).

### Errores comunes y cuándo NO usarlo

- **Cruce demasiado alto.** A 400 Hz o más, la banda de graves se lleva la "voz" del bajo y vuelve el bombeo que
  querías evitar. Empieza en 120 Hz.
- **Cruce demasiado bajo.** A 40–50 Hz solo se aparta el sub profundo; el cuerpo del kick (80–100 Hz) sigue chocando.
- **Esperar que el cruce "corte en seco".** Con 24 dB/oct, un cruce a 120 Hz también toca un poco 60–240 Hz. Es normal
  y suena natural.
- **Mix y profundidad a la vez sin saber que son lo mismo.** Si bajas los dos, el efecto se reduce dos veces (80 % ×
  50 % = 40 %). Ajusta con la profundidad y usa el mix para automatizar.
- **Cuándo NO usarlo:** en un bombeo creativo de un pad o un bus entero (house francés), *Todo* es el efecto que
  buscas: el bombeo **es** la gracia. Y si el bajo es casi solo sub (un 808 limpio, una senoide), *Graves* y *Todo*
  suenan casi igual: no hay nada arriba que salvar.

## Fase 7 — Ver el ducking: visor en vivo, cursor y luz de golpe

Hasta ahora el ducking se ajustaba sobre todo de oído y con la vista previa, que muestra lo que **harán** los ajustes,
no lo que está pasando. El visor EN VIVO muestra lo que **pasa de verdad** por Ebb, compás a compás: dónde cae cada
golpe del kick, cuánto baja el bajo y si vuelve a tiempo antes del golpe siguiente. Con un dembow irregular eso vale
oro: se ve al instante si un kick no disparó, si la curva es demasiado larga o si el hueco llega tarde.

### Conceptos aprendidos

- **Osciloscopio de barrido sincronizado.** Un osciloscopio dibuja la señal en el tiempo. El de Ebb no "corre" hacia
  la izquierda como el Wave Candy de FL: un **cursor barre** de izquierda a derecha una ventana de 1, 2 o 4 compases y
  va reescribiendo lo que había. Como la ventana empieza siempre en un inicio de compás, lo que se repite cada compás
  (el patrón del kick, la curva del modo tempo) se queda **quieto** en pantalla, y lo que cambia (un kick que se
  movió, una variación) salta a la vista. Es la misma idea del visor de LFOTool o ShaperBox.
- **Columnas y picos.** La ventana se divide en 512 columnas; cada una resume unos milisegundos de audio con su **pico**
  (el valor más alto). El pico de un trozo muy corto puede no "ver" un ciclo entero de un sub de 40 Hz (25 ms), así
  que el visor muestra el mayor pico de las columnas vecinas (±12.5 ms). Por eso el dibujo del bajo se ve lleno y no a
  rayas.
- **Escala automática.** La altura de la señal se escala a lo más fuerte que se vio (sube al instante, baja en unos
  segundos): un bajo a −18 dBFS llena el visor igual que uno a −3. El visor sirve para ver **formas y tiempos**, no
  para medir niveles (para eso, el medidor de reducción en dB de la vista previa).
- **Comunicación audio → pantalla sin esperar.** El hilo de audio no puede esperar a la pantalla (si espera, se oyen
  cortes). Cada columna son unos números atómicos que el audio sobrescribe y la pantalla lee 30 veces por segundo. Si
  justo lee una columna a medio escribir, la ve mezclada durante un cuadro: no pasa nada. Sin la pantalla abierta, el
  visor ni se alimenta.

### Qué hace cada control (y qué muestra la pantalla)

- **Visor EN VIVO** (arriba a la izquierda):
  - **gris** = la señal que entra (antes del ducking); **verde agua** = la que sale. Lo gris que queda por encima del
    verde es lo que el ducking quitó.
  - **línea blanca** = el volumen (arriba = entero, abajo = silencio), tal como sonó: con el suavizado, el mix y el
    bypass incluidos.
  - **ámbar** = el sidechain (el kick que entra por el bus auxiliar, con su propia escala).
  - **triángulos verde agua** = dónde empezó una curva: cada golpe MIDI o del sidechain; en modo Tempo, cada ciclo.
  - **números** = compás y tiempo (3, 3.2, 3.3...); las marquitas de abajo son las semicorcheas (la cuadrícula del
    dembow).
  - El **cursor blanco** es "ahora". Detrás de él, lo recién escrito; delante (después del hueco negro), la vuelta
    anterior.
- **1 / 2 / 4 compases** (botón del visor): cuántos compases caben. 1 para ver el patrón de cerca; 2 o 4 para patrones
  que cambian de un compás a otro. Se guarda con el proyecto.
- **Luz GOLPE**: parpadea con cada curva que empieza (en cualquier modo).
- **Cursor del editor de la curva**: el punto blanco sobre la FORMA dice por dónde va la curva que está sonando. Al
  terminar la duración se queda, apagado, en el final, esperando el golpe siguiente.
- **Zoom** (botón "100 %" de la cabecera, o arrastrando la esquina de abajo a la derecha): 70–200 %. Se guarda con el
  proyecto y como preferencia para las ventanas nuevas. La primera vez se elige el mayor que cabe en la pantalla.
- **Reloj del host**: ahora es una tira en la cabecera (estado, posición, luces del compás, tempo, B:S de FL, loop,
  saltos y audio).
- **Fila de controles**: a la derecha aparece lo del modo: NOTA MIDI, VELOCIDAD y ACTIVIDAD MIDI en Tempo y MIDI; el
  medidor del SIDECHAIN, UMBRAL y RETENCIÓN en Audio. El menú FORMA pasó al título del editor de la curva.

### Ejercicio guiado: diagnosticar un ducking con los ojos (y confirmarlo con el oído)

1. **95 BPM**, dembow desplazado (kick en los pasos 1, 7, 11 y 15), Ebb en el 808 en **modo Tempo 1/4**, profundidad
   80 %, duración 50 %. Visor en **1 compás**. Dale play.
2. Mira los triángulos: caen en 1, 2, 3 y 4 (la cuadrícula). Pero los kicks reales están en el 1 y a mitad de los tiempos 2,
   3 y 4 (pasos 7, 11 y 15): **se ve** que los huecos del 808 no coinciden con los kicks. Esa es la razón de los modos
   MIDI y Audio.
3. Cambia a **modo MIDI** (con el ruteo de la Fase 4): los triángulos se mueven bajo cada kick, y los huecos de la
   línea blanca también.
4. **La duración, a ojo:** sube la DURACIÓN hasta que la línea blanca **no llegue arriba** antes del triángulo
   siguiente (entre el kick del paso 7 y el del 11 hay solo 4 semicorcheas). En ese punto el bajo nunca vuelve entero:
   escúchalo, el 808 pierde cuerpo. Baja la duración hasta que la línea toque arriba justo antes de cada golpe.
5. **Modo Audio + lookahead:** con el sidechain conectado, mira la línea ámbar (el kick) y el hueco blanco. Sin
   lookahead, el hueco empieza un pelo **después** de que sube el ámbar (el detector necesita ver el golpe). Sube el
   LOOKAHEAD a 3–5 ms y mira cómo el hueco se adelanta y queda debajo del ataque del kick (usa 1 compás para verlo).
6. Con el **volumen igualado** (como siempre), confirma de oído cada cambio: el visor dice dónde mirar, pero el que
   decide es el oído.

### Receta: ajustar el ducking de un dembow en 30 segundos

Visor en **1 compás** (o 2 si el patrón cambia), modo **MIDI o Audio**, y:
- Cada kick tiene que tener **su triángulo**. Si falta uno: en MIDI, revisa la nota (NOTA MIDI); en Audio, baja el
  UMBRAL o la RETENCIÓN.
- El hueco blanco tiene que empezar **bajo el ataque** del kick (en Audio: LOOKAHEAD 2–5 ms).
- La línea blanca tiene que **volver arriba antes** del kick siguiente más cercano (DURACIÓN / LARGO).
- Profundidad: la mínima con la que el kick se oye limpio (en *Graves* se puede ir hondo sin que se note).
- Por qué funciona: son las tres cosas que hacen que un ducking suene "pegado" al kick: que dispare siempre, que
  llegue a tiempo y que suelte a tiempo.

### Reto sin receta

En un dembow con variaciones, el último compás de cada 4 tiene un **redoble de kick** (cuatro semicorcheas seguidas al
final). Con el visor en **4 compases**, ¿qué ves en la línea blanca durante el redoble con LARGO 1/4 y duración 70 %?
¿El bajo llega a volver entre golpes? ¿Qué cambiarías (LARGO, DURACIÓN, FORMA o la VELOCIDAD) para que el redoble suene
marcado sin que el bajo desaparezca? Pista: mira dónde está el punto blanco del editor cuando llega cada golpe del
redoble.

Inténtalo y después pide pistas.

### Vocabulario

- **Osciloscopio**: aparato (o plugin) que dibuja una señal en el tiempo. *En música:* el Wave Candy de FL en modo
  osciloscopio; los visores de LFOTool y ShaperBox.
- **Barrido (sweep)**: un cursor que recorre la pantalla y reescribe lo de la vuelta anterior. *En música:* los
  osciloscopios analógicos funcionan así; en plugins, el visor de Kickstart 2 y el de ShaperBox.
- **Pico (peak) / envolvente de picos**: el valor más alto de un trozo de señal; la línea que une los picos. *En
  música:* la forma de onda que ves en la lista de reproducción de FL es una envolvente de picos.
- **Reducción de ganancia (gain reduction)**: cuánto baja el volumen un procesador, en dB. *En música:* la aguja de un
  compresor 1176 o el medidor GR de Pro-C 2; en Ebb, la línea blanca y el medidor de la vista previa.
- **Latencia y lookahead, vistos**: el retraso que se ve entre el ámbar (el kick) y el hueco blanco. *En música:* los
  limitadores de mastering usan lookahead para que el volumen ya esté abajo cuando llega el pico.

### Errores comunes

- **Mezclar con los ojos.** Una curva "bonita" en el visor puede sonar mal (y al revés). Úsalo para encontrar el
  problema, decide con el oído y con el volumen igualado.
- **Leer la altura como volumen real.** La escala es automática: el visor no dice si el bajo está fuerte o bajo, solo
  la forma. Para niveles, el medidor de reducción en dB y los medidores de FL.
- **Visor en 4 compases para ajustar el ataque.** Cada columna dura más y los detalles finos (los primeros ms del
  golpe) se aplastan. Para el ataque y el lookahead, 1 compás.
- **Esperar ver huecos con FL parado en modo Tempo.** Parado no hay ducking (por diseño): el visor muestra la señal
  pasando sin huecos. En MIDI o Audio, tocar el kick sí dispara y se ve.

## Fase 8 — Presets: recetas que se guardan y se explican

Hasta ahora cada receta de este cuaderno había que montarla a mano, perilla por perilla. Ahora Ebb trae **19 presets
de fábrica** (dembow, trap, house, mezcla y efectos creativos) y puedes guardar los tuyos. Cada preset es una receta
completa (también la forma de la curva) con **notas** que explican qué hace cada ajuste, por qué, qué ruteo necesita y
cuándo NO usarlo. No son "sonidos mágicos": son puntos de partida que se ajustan a tu kick y a tu tema.

### Conceptos aprendidos

- **Un preset es una foto de los ajustes que dan forma al sonido.** Se guarda como un archivo XML legible
  (`.ebpreset`, en `Documentos\Ebb\Presets`), en unidades reales: "depth 35" son 35 %, "crossover 120" son 120 Hz. Lo
  que el preset no escribe toma el valor de **Init**; así un preset de fábrica se lee como una receta (solo lo que
  cambia) y sigue funcionando cuando Ebb gane controles nuevos.
- **La curva también viaja con el preset**, aunque no sea un parámetro (un parámetro es un número que FL puede
  automatizar; la curva tiene un número variable de puntos). En el archivo va aparte, como `<Curve shape="Lenta"/>` (una
  forma de fábrica por su nombre) o punto a punto (`<Point x="0.3" y="0.85" tension="-0.5"/>`), igual que en el proyecto
  de FL. Abre `Rebote` en el Bloc de notas para ver una curva escrita a mano.
- **El BYPASS no es parte del preset.** Elegir un preset no enciende ni apaga Ebb.
- **El asterisco (\*)**: aparece junto al nombre en cuanto tocas algo (una perilla o un punto de la curva) y se va si
  lo devuelves a como estaba. Viaja con el proyecto: al reabrirlo sabes si ese sonido es el preset tal cual o una
  versión tuya.
- **Lo que un preset NO puede saber**: el nivel de tu kick (el UMBRAL de los presets de Audio es un punto de partida),
  la nota MIDI de tu kick (quedan en "Todas") ni el tempo de tu tema (las notas dicen a qué tempo se pensaron los ms).

### Qué hace cada control

- **◀ ▶**: preset anterior / siguiente, en el orden del navegador. Sirve para recorrer una categoría con el tema
  sonando.
- **Nombre del preset** (con la categoría a la izquierda): abre el **navegador**. A la izquierda, las categorías y "Mis
  presets"; en el centro, la lista y la búsqueda (busca también dentro de las notas: prueba "808" o "MIDI"); a la
  derecha, las notas del preset elegido. Un clic o las flechas del teclado ya lo cargan (para escuchar rápido con el
  tema sonando); doble clic o Enter lo cargan y cierran el navegador; Esc lo cierra.
- **Guardar**: guarda lo que suena como preset tuyo, con nombre, categoría, autor y notas. Si partiste de uno de
  fábrica, las notas empiezan con "Basado en «…»": escribe ahí qué cambiaste y por qué (tu yo de dentro de seis meses
  te lo agradece).
- **···**: Init (empezar de cero), guardar como, abrir la carpeta de tus presets en el Explorador, volver a leerla.
- **Pie de la ventana**: durante unos segundos muestra, en ámbar, qué preset se cargó o si algo del archivo no se
  entendió.
- **Las familias de presets**, en lenguaje musical:
  - **Dembow**: limpiar el grave del bajo/808 con el kick del reggaetón, en los tres modos (Tempo para el clásico, MIDI
    para el kick que se mueve, Audio para un kick grabado), con el 808 de cola larga y con gruñido (solo el sub), y un
    efecto: acordes que respiran con el kick real.
  - **Trap**: el 808 que deja pasar el golpe del kick (corto, solo el sub), un sample que se aparta 3 dB y un pad en
    tresillos contra el pulso.
  - **House**: el bombeo clásico (efecto) y el sub del bajo apartado en cada tiempo (limpieza).
  - **Mezcla**: ducking que no se oye (2 dB en el bus de la música; 6 dB solo por debajo de 80 Hz).
  - **Creativo**: el ducking ES el efecto (trance gate, stutter por MIDI, eco rítmico, rebote, oleaje de un compás).

### Ejercicio guiado: los tres modos del dembow, preset contra preset (con el volumen igualado)

1. **95 BPM**, dembow desplazado (kick en los pasos 1, 7, 11 y 15), un **808 largo**. Deja preparados los dos ruteos:
   el MIDI Out con las notas del kick (Fase 4) y "Sidechain to this track" del kick al 808 (Fase 5).
2. Ebb en el 808, preset **Dembow Bajo Limpio** (Tempo). Mira el visor en 1 compás: hay huecos en los cuatro tiempos,
   pero el kick está en 1, 7, 11 y 15. Escucha los pasos 5, 9 y 13: el 808 baja sin que haya kick (un "hipo" del bajo).
3. Pasa a **Dembow MIDI Kick Irregular** con ▶ ◀ o el navegador. Ahora cada hueco tiene su triángulo debajo de un kick.
   El hipo desapareció.
4. Pasa a **Dembow Audio Kick Grabado**. Ajusta el UMBRAL a tu kick (el medidor del SIDECHAIN: cada golpe por encima de
   la línea ámbar). Compara con el anterior: suena casi igual, pero ahora mover el kick en el Playlist (o ponerle swing)
   también mueve el hueco, sin MIDI.
5. Entre preset y preset, **iguala el volumen** con el fader del 808 antes de juzgar: los de más profundidad suenan más
   bajos y el oído tiende a preferir el más fuerte.
6. Por último, **Dembow 808 Gruñido Graves** contra **Dembow MIDI Kick Irregular**: el primero baja mucho más (80 %),
   pero solo el sub; con el 808 saturado se oye más limpio y menos "bombeado". Míralo en el panel BANDAS.

### Receta: tu preset de dembow, a medida

Parte del preset del modo que usas (MIDI o Audio) y ajústalo a TU kick y TU 808:
- **El disparo**: en MIDI, USAR ÚLTIMA NOTA con el kick sonando; en Audio, el UMBRAL hasta que cada kick tenga su
  triángulo y, entre golpe y golpe, el nivel baje de la línea punteada.
- **La vuelta**: LARGO × DURACIÓN para que la línea blanca vuelva arriba justo cuando se apaga el kick (visor en 1
  compás). Si el kick tiene cola, forma Retención antes que más duración.
- **Cuánto**: la profundidad mínima con la que el kick se oye limpio; en Graves se puede ir más hondo.
- **Guardar** con un nombre que diga para qué es ("808 Perreo 95 Audio") y en las notas: qué kick, a qué tempo, qué
  cambiaste y por qué.
- Por qué funciona: los presets de fábrica no conocen tu kick; un preset tuyo sí. La próxima vez que uses ese kick (o
  ese pack), cargas el preset y está listo en un clic, y las notas te recuerdan el porqué.

### Reto sin receta

Cambias el kick del dembow por un **808 de cola larga** (suena unos 400 ms) y usas **Dembow Audio Kick Grabado**. En el
visor ves que el kick del **"1"** a veces no tiene triángulo: el 808 no se aparta justo en el golpe más importante del
compás. Los demás kicks sí disparan. ¿Por qué precisamente el del "1"? ¿Qué control de Ebb lo arregla, y en qué
dirección hay que moverlo (al revés de lo que dice la intuición)? Pista para empezar: mira cuántos pasos hay entre el
kick del paso 15 y el del "1" siguiente, y qué hace la línea punteada del medidor del SIDECHAIN en ese hueco.

Inténtalo y después pide pistas.

### Vocabulario

- **Preset**: un juego de ajustes guardado con nombre. *En música:* los "patches" de los sintetizadores de los 80 (el
  piano eléctrico "E.PIANO 1" del Yamaha DX7, un preset de fábrica, suena en medio pop de esa década).
- **Init (inicializar)**: todos los controles en su valor por defecto; el punto de partida "en blanco". *En música:* el
  "Init Patch" de Serum o Vital, del que salen la mayoría de los sonidos de diseño propio.
- **Trance gate**: cortar un sonido sostenido en un ritmo fijo con una puerta de volumen. *En música:* los pads
  cortados en semicorcheas del trance de los 2000 (el sonido típico de las intros de ATB o Tiësto).
- **Stutter**: repetir o cortar un sonido en ráfagas cortas como un "tartamudeo". *En música:* los cortes de voz del
  EDM y del pop electrónico, y los "chops" de voz del reggaetón más electrónico.
- **Plantilla (template)**: un proyecto de FL ya preparado con tus ruteos y plugins. *En música:* casi todos los
  productores trabajan con una; en la tuya, el MIDI Out del kick y el sidechain al 808 ya conectados, con Ebb y tu
  preset cargados.

### Errores comunes y cuándo NO usar un preset

- **Cargar y no escuchar.** Un preset está pensado para un tempo, un kick y un nivel. Siempre revisa el disparo (los
  triángulos), la vuelta (la línea blanca) y la profundidad con TU material.
- **Preset de Audio sin ruteo, o de MIDI sin notas.** Sin sidechain o sin MIDI no hay golpes: Ebb no hace nada (el pie y
  las notas te recuerdan qué ruteo necesita cada uno).
- **Comparar presets sin igualar el volumen.** El que baja menos suena más fuerte y "gana" sin ser mejor.
- **Elegir por el nombre del género.** "Trap 808 Deja Pasar el Kick" puede ser justo lo que necesita un dembow con un
  808 que golpea con el kick; los géneros son una pista, no una regla. Lee las notas.
- **Guardar sin notas.** Dentro de unos meses no sabrás por qué ese preset tenía 2.5 ms de lookahead.
- **Cuándo NO usar un preset:** cuando ya sabes qué quieres (montarlo desde Init es igual de rápido y aprendes más), o
  cuando el problema no es de ducking (un bajo que choca con el kick en TODO momento, no solo en el golpe, se arregla
  con EQ o con el arreglo, no con Ebb).

## Fase 9 — Pulido: CPU, tempos y cambios de tempo en vivo

Esta fase no agrega controles: revisa que Ebb aguante cualquier tempo, la automatización de tempo de FL y cualquier
tamaño de buffer, y mide cuánto procesador gasta. Encontró y arregló un fallo real: con el tempo **bajando** (un
ritardando automatizado) y buffers grandes, un golpe del modo Tempo podía sonar **dos veces**, con un clic en medio.

### Conceptos aprendidos

- **El audio de Ebb casi no gasta nada.** Medido en este portátil (`EbbTests --cpu`): 0.24 % de un núcleo con Init y
  0.38 % en el peor caso (Audio + lookahead + Graves con el cruce moviéndose + mix + curva de 32 puntos + GUI abierta).
  Cabrían cientos de Ebb antes de que FL lo note. Por cada muestra hace pocas cuentas: la posición en el compás, la
  curva, dos filtros de cruce por canal y el detector del sidechain.
- **Lo que sí gasta es la GUI.** Dibujar el visor en vivo 30 veces por segundo costaba mucho más que todo el audio: con
  la ventana de Ebb abierta, 7 % de un núcleo con la canción PARADA y 12 % sonando. Ahora la GUI solo se repinta si algo
  cambió, y solo la parte que cambió: parada, 0.8 %. Sonando sigue en unos 11 %: casi todo es el coste fijo de dibujar
  un cuadro en Windows (Direct2D), que se paga aunque cambie un solo píxel. Cerrar la ventana de Ebb lo deja en cero.
- **El medidor de CPU de FL mide el hilo de audio**, no la GUI. Si la PC va lenta con muchas ventanas de plugins
  abiertas, el culpable suele ser el dibujo (otro hilo), no el procesamiento. Ciérralas cuando no las uses.
- **Cómo sabe Ebb dónde está en la canción**: FL le dice, al principio de cada buffer, la posición y el tempo; dentro
  del buffer Ebb extrapola con ese tempo. Si el tempo está automatizado y cambia DENTRO del buffer, la posición real se
  aleja un poquito de la extrapolada (más cuanto más grande el buffer y más empinada la rampa).
- **El arreglo: recuperar poco a poco en vez de saltar.** Antes, al buffer siguiente Ebb saltaba a la posición real de
  FL. Si el tempo subía, la curva saltaba un poco hacia adelante (un clic pequeño); si bajaba, ¡la posición retrocedía!
  Y si justo antes había cruzado un golpe, lo volvía a cruzar: el golpe sonaba dos veces. Ahora Ebb sigue desde donde
  iba y alcanza a FL avanzando hasta un 10 % más rápido o más lento durante unas muestras, como un músico que se
  adelantó un poco y frena sin pararse. La posición nunca retrocede ni salta; con el tempo fijo no cambia nada
  (comprobado: el sonido es idéntico bit a bit en 336 escenarios).
- **Cuánto se puede desviar un golpe con el tempo automatizado** (medido): con una rampa de 90 a 180 BPM en 8 compases,
  0.3 ms como mucho, incluso con buffers de 4096; con una rampa brutal (200 → 60 BPM en 1 segundo) y buffers de 4096 a
  44.1 kHz, 4.5 ms; con buffers de 1024 o menos, medio milisegundo. Inaudible en la práctica (un flam se empieza a
  notar hacia los 10 ms).
- **Los modos MIDI y Audio no dependen de la posición**: el golpe lo marca la nota o el kick. El tempo solo decide el
  LARGO de la curva: a 70 BPM dura el doble de milisegundos que a 140.

### Qué hace cada control

No hay controles nuevos. Lo que cambia es cómo se comporta lo que ya había:
- **Modo Tempo con el tempo automatizado**: los huecos siguen cayendo en la cuadrícula mientras el tempo sube o baja
  (un "speed up" antes del drop, un ritardando al final), sin golpes dobles ni clics.
- **DIVISIÓN y DURACIÓN** se miden en tiempos musicales: si el tempo cambia, la curva se estira o se encoge con él (a
  la mitad de tempo, el hueco dura el doble de milisegundos).
- **El visor y el reloj de la cabecera** quedan quietos con la canción parada (antes se repintaban sin parar).

### Ejercicio guiado: Ebb con el tempo automatizado (con el volumen igualado)

1. Proyecto a **95 BPM**, el dembow de siempre, Ebb en el 808 con **Dembow Bajo Limpio** (Tempo) y el visor en 1
   compás.
2. Automatiza el tempo: clic derecho sobre el tempo de la barra de transporte → *Create automation clip*. Dibuja una
   rampa de **95 a 140 BPM en 4 compases** (un speed up) y después vuelve de golpe a 95.
3. Dale play y mira el visor: los cuatro huecos por compás se quedan sobre las líneas de los tiempos durante toda la
   rampa, y el reloj de la cabecera sigue a FL. Escucha el 808: el hueco se hace más corto (en ms) a medida que sube el
   tempo, porque la curva dura una fracción del tiempo musical.
4. Ahora un ritardando: rampa de **140 a 70 BPM en 2 compases**, y sube el buffer de FL al máximo (Options → Audio
   settings → Buffer length). Antes de esta fase, aquí podía aparecer un golpe doble con un clic; ahora no.
5. Repite con **Dembow MIDI Kick Irregular**: los golpes siguen a las notas (que FL también mueve con el tempo) y el
   largo de la curva sigue al tempo.

### Receta: drop a medio tempo con el mismo ducking

Un recurso del reggaetón y del trap: después del build, la sección siguiente va a medio tempo (de 140 a 70) o el
patrón pasa a "halftime".
- **Tempo cambiado de verdad** (automatización de 140 a 70): Ebb en modo Tempo sigue cayendo en cada negra, pero cada
  negra dura el doble: el bombeo se vuelve lento y pesado sin tocar nada. Si quieres el mismo ritmo de antes en
  segundos, pasa la DIVISIÓN de 1/4 a 1/8 en esa sección con una automatización.
- **Halftime sin cambiar el tempo** (el kick toca la mitad de veces): usa MIDI o Audio y el ducking sigue al kick nuevo
  sin automatizar nada; con Tempo tendrías que pasar de 1/4 a 1/2.
- Por qué: en Tempo, Ebb piensa en tiempos musicales; en MIDI y Audio, en golpes. Elegir el modo según qué cambia (el
  tempo o el patrón) te ahorra automatizaciones.

### Reto sin receta

Un tema a 100 BPM baja a 75 en el puente con una rampa de 2 compases, y el 808 tiene Ebb en modo Tempo con DIVISIÓN 1/4
y DURACIÓN 80 %. Durante la rampa y en el puente el 808 suena "hundido": casi nunca recupera su volumen entre golpe y
golpe. ¿Por qué pasa al bajar el tempo y no a 100? ¿Qué dos controles podrías automatizar (o qué modo usar) para que
el 808 respire igual que antes? Pista para empezar: calcula cuántos milisegundos dura la curva a 100 y a 75 BPM.

Inténtalo y después pide pistas.

### Vocabulario

- **Automatización de tempo**: que el tempo del proyecto cambie a lo largo de la canción. *En música:* los "speed ups"
  antes de un drop de EDM, o el tempo que se frena al final de muchas baladas.
- **Accelerando / ritardando**: acelerar / frenar el tempo poco a poco (términos clásicos). *En música:* el ritardando
  final de casi cualquier bolero; el accelerando de "Hava Nagila".
- **Halftime (medio tiempo)**: el patrón a la mitad de velocidad sobre el mismo tempo (o el tempo a la mitad). *En
  música:* los drops halftime del dubstep y del trap, y los puentes lentos del reggaetón moderno.
- **Buffer (tamaño de bloque)**: cuántas muestras procesa el plugin de una vez. Más grande = menos CPU y más latencia al
  tocar en vivo. *En FL:* Options → Audio settings → Buffer length.
- **Carga de CPU**: qué fracción del tiempo disponible usa el procesador para calcular el audio. Si llega al 100 % hay
  cortes ("underruns"). *En FL:* el medidor de CPU de la barra superior.

### Errores comunes

- **Culpar al audio cuando es la GUI.** Diez ventanas de plugins con visores animados pueden poner lenta la PC aunque el
  medidor de CPU de FL esté bajo. Cierra las que no estés mirando.
- **Automatizar el tempo y olvidar que el ducking también cambia de duración.** En Tempo, la curva se mide en tiempos:
  a la mitad de tempo, el hueco dura el doble de milisegundos.
- **Dejar el buffer alto para grabar.** Para tocar y grabar MIDI, buffer bajo (poca latencia); para mezclar un
  proyecto pesado, alto. Ebb funciona igual con cualquiera.
