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
