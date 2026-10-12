# APRENDIZAJE — Fathom (EQ paramétrico)

Cuaderno de mezcla del EQ. Se completa al final de cada fase (ver `CLAUDE.md`).

## Fase 1 — El lienzo: ver el sonido antes de tocarlo

Fathom todavía no ecualiza: la señal solo pasa por la ganancia de salida. Lo que aprende en esta fase es a **mirar**.
Un EQ es una herramienta para mover volumen *por frecuencias*, y antes de mover nada hay que saber leer dónde está
cada cosa. El visor de esta fase es el lienzo sobre el que, desde la Fase 2, se dibujan las bandas.

### Conceptos aprendidos

- **Frecuencia.** Cuántas veces por segundo vibra algo (Hz). Graves = lento (un 808 en A2 de FL vibra a 55 Hz),
  agudos = rápido (la "s" de una voz, 6–9 kHz). El oído humano va de ~20 Hz a ~20 kHz.
- **Octava = el doble.** 55 → 110 → 220 → 440 Hz son el mismo La en cuatro octavas. El oído oye las frecuencias
  *proporcionalmente*: de 100 a 200 Hz "se siente" igual de lejos que de 5 a 10 kHz.
- **Eje logarítmico.** Por eso el visor no reparte los Hz en línea recta: cada octava ocupa lo mismo. Si fuera lineal,
  todo lo que importa de un bajo (20–250 Hz) cabría en el 1 % izquierdo de la pantalla.
- **Espectro.** Una foto de cuánto nivel hay en cada frecuencia en este momento. Una nota de un instrumento no es una
  sola frecuencia: es una **fundamental** (la nota) más **armónicos** encima (2×, 3×, 4×...), que son el timbre.
- **dBFS.** Nivel digital: 0 dBFS es el máximo antes de recortar; todo lo demás es negativo. La escala de la
  izquierda del visor está en dBFS: un seno de 0 dBFS marca 0.
- **Dos escalas.** La de la derecha (±12 dB) es la del **EQ**: cuánto sube o baja una frecuencia. La de la izquierda
  es la del **analizador**. Son independientes a propósito: un realce de +2 dB (grande para el oído) tiene que verse
  bien aunque el espectro ocupe 90 dB.
- **El mapa de frecuencias** (la franja de abajo; pasa el mouse sobre cada zona):

  | Zona | Rango | Qué vive ahí | De más | De menos |
  |------|-------|--------------|--------|----------|
  | SUB | 20–60 Hz | fundamental del 808, golpe del kick | retumba, se come el headroom | sin peso en el club |
  | GRAVES | 60–250 Hz | cuerpo del bajo y del kick, calidez de voz | pesado | delgado |
  | BARRO | 250–500 Hz | casi todo se acumula aquí | turbio, "embarrado" | hueco |
  | MEDIOS | 500 Hz–2 kHz | caja, nasal; lo que suena en el celular | "de caja", nasal | se pierde en bocinas chicas |
  | PRESENCIA | 2–5 kHz | claridad de voz, ataque de percusión | agresivo, cansa | lejano, apagado |
  | BRILLO | 5–10 kHz | eses, hi-hats, platillos | sibilante, áspero | opaco |
  | AIRE | 10–20 kHz | sensación de espacio | chillón, ruido | cerrado |

- **Frecuencia ↔ nota.** Al pasar el mouse, el visor dice la nota de FL más cercana y cuántos *cents* (centésimas de
  semitono) se aleja. Sirve para saber qué nota resuena: si un 808 retumba en 49 Hz, eso es un G2 de FL.

### Qué hace cada control (y qué muestra la pantalla)

- **ENTRADA** (línea gris): el espectro de lo que llega al plugin.
- **SALIDA** (relleno azul): lo que sale. Lo que cambie entre las dos lo hizo Fathom. Hoy solo cambia con SALIDA: las
  dos curvas tienen la misma forma y la azul sube o baja entera.
- **SALIDA** (perilla, ±24 dB): el volumen al final. Mueve la línea blanca (la curva del EQ): en esta fase la "curva"
  es una recta porque sube o baja todo por igual. Doble clic = 0 dB.
- **±12 dB**: la escala del EQ (6, 12, 24 o 36 dB). No cambia el sonido, solo el zoom vertical de la curva.
- **BYPASS**: escuchar el original, con fundido de 20 ms (sin clics). Es el mismo botón que el de FL.

### Ejercicio de escucha guiado en FL Studio

1. Pon Fathom en el mixer de un loop de dembow completo (kick, 808, percusión, voz si tienes). Reproduce.
2. Mira solo la ENTRADA. Señala con el mouse: ¿dónde está el pico más alto? (casi siempre el 808 o el kick, abajo a la
   izquierda). Lee su nota. ¿Coincide con la tonalidad de la canción?
3. Silencia todo menos el 808 (en el mixer de FL): ¿cuántos picos ves? El primero es la fundamental; los siguientes
   están a 2×, 3×... Ahora solo el hi-hat: ¿en qué zona vive? Ahora la voz.
4. **El sesgo del volumen**: con todo sonando, sube SALIDA a +1.5 dB y alterna con BYPASS con los ojos cerrados (o
   pídele a alguien que lo haga). Casi todo el mundo elige la versión más fuerte como "mejor", aunque no tenga nada
   distinto. Esa trampa es la razón por la que en esta suite se compara siempre con el volumen igualado.

### Receta: el "chequeo de mapa" antes de ecualizar

Antes de tocar un EQ en cualquier pista: (1) mira el espectro con la pista sola y anota en qué zona está su energía
principal; (2) hazlo con la pista que más le choca (kick ↔ 808, voz ↔ acordes); (3) si los dos picos caen en la misma
zona, ahí habrá que tallar espacio (Fases 2–3). Es más rápido que buscar a oído y te entrena a asociar zonas con
sonidos.

### Reto sin receta

Encuentra la **nota** de tu 808 en un beat tuyo usando solo el visor (sin mirar el piano roll). Luego compruébala en el
piano roll. Pistas (si no sale): silencia lo demás; el pico más bajo y alto es la fundamental; si el 808 tiene glide,
pausa y mira el espectro quieto; acuérdate de que A5 de FL = 440 Hz, así que A1 = 27.5 Hz y A2 = 55 Hz.

### Vocabulario

- **Fundamental / armónicos**: la nota y sus múltiplos. En un 808 distorsionado (trap, reggaetón) los armónicos son
  los que hacen que se oiga en el celular aunque la fundamental (40 Hz) no salga por esa bocina.
- **Octava**: el doble de frecuencia. El "bajo una octava más arriba" de muchos drops.
- **Escala logarítmica**: cada octava ocupa lo mismo (como las teclas de un piano).
- **dBFS**: decibelios respecto del máximo digital (0).
- **Headroom**: el margen entre el pico y 0 dBFS. El sub se come el headroom: mucha energía que casi no se oye.
- **Sesgo del volumen (loudness bias)**: lo más fuerte parece mejor. Por eso existen los botones de auto gain.

### Errores comunes (y cuándo NO usarlo)

- **Mezclar con los ojos**: el analizador ayuda a ubicar, pero la decisión es del oído. Un pico enorme en 50 Hz puede
  ser exactamente lo que el beat necesita.
- **Comparar sin igualar el volumen** (ver el ejercicio 4).
- **Esperar que el analizador muestre todo igual de "plano"**: la música real cae hacia los agudos (más energía en
  graves). Un espectro plano sonaría chillón. (En la Fase 5 llega la inclinación de 4.5 dB/oct que compensa eso al
  mirar.)

## Fase 2 — La campana: subir o bajar una zona

Ya ecualiza: una banda **campana** (en inglés *peaking* o *bell*) sube o baja una zona alrededor de una frecuencia y
deja el resto como estaba. Es la forma más usada de un EQ: casi todo lo que se hace en una mezcla con un EQ es una
campana, o varias.

### Conceptos aprendidos

- **Cómo se construye.** El filtro de estado variable (SVF, el mismo de Undertow) separa la señal en graves, una banda
  alrededor del corte y agudos. La campana es la señal original **más** un poco de esa banda (o **menos**, para
  cortar). Cuánto sumar sale de la ganancia; qué tan angosta es la banda, del Q. Es la fórmula del prototipo analógico
  de un EQ de consola, pasada a digital sin perder la estabilidad aunque muevas las perillas en pleno audio.
- **La curva es el sonido.** La línea blanca no es un dibujo aproximado: se calcula con los mismos números que usa el
  audio. Los tests comprueban que lo que mide un seno al pasar por Fathom coincide con la curva en menos de 0.0001 dB.
- **Q y ancho de banda.** El Q dice lo angosta que es la campana; el **ancho en octavas** (lo muestra el panel) es lo
  mismo dicho en musical. Q 0.7 ≈ 2 octavas (ancha, suave), Q 1.41 = 1 octava, Q 4.3 ≈ 1/3 de octava, Q 10 ≈ 1/7 de
  octava (quirúrgica). El ancho se mide donde la campana vale la mitad de su ganancia: una campana de +12 dB con Q 1.41
  vale +6 dB media octava a cada lado del centro.
- **Simétrica.** Subir +6 dB y bajar −6 dB con la misma frecuencia y Q son exactamente inversas: si pones dos Fathom
  seguidos así, la señal sale igual que entró. Y en la escala de octavas la campana es simétrica (mismo dB una octava
  abajo que una arriba), salvo muy cerca de 20 kHz (ver "aplastamiento" en Errores comunes).
- **Sin clics.** Mover la frecuencia, la ganancia o el Q de golpe haría clic (el filtro "recuerda" las muestras
  anteriores, que de pronto no cuadran con los ajustes nuevos). Fathom no salta: recorre el cambio en 20 ms, la
  frecuencia en octavas, la ganancia en dB y el Q en "veces". Apagar la banda baja su ganancia a 0 dB en 20 ms.
- **0 dB = intacto.** Con la ganancia en 0 dB (o la banda apagada) la señal sale **bit a bit** igual: no hay "color"
  escondido por tener el plugin puesto.

### Qué hace cada control (y qué muestra la pantalla)

- **FRECUENCIA** (10 Hz–30 kHz): dónde está el centro. Mueve el nodo "1" a izquierda y derecha. La franja de abajo te
  dice en qué zona estás (SUB, BARRO, PRESENCIA...) y el panel, qué nota de FL es (sirve para cazar la nota que
  retumba en un 808).
- **GANANCIA** (±30 dB): cuánto sube o baja el centro. Mueve el nodo arriba o abajo; la zona rellena es lo que la
  banda cambia. En el analizador, la SALIDA (azul) se separa de la ENTRADA (gris) justo con la forma de la curva.
- **Q** (0.025–40): lo angosta que es. Q bajo = una colina ancha que cambia el "color" general; Q alto = una aguja que
  toca una sola resonancia. El panel lo traduce a octavas.
- **ON**: comparar con y sin la banda (sin clic).
- **Lectura del mouse**: ahora dice también "curva +x dB": cuánto cambia el EQ el volumen en esa frecuencia exacta.

### Ejercicio de escucha guiado en FL Studio

1. **Ver que la curva es el sonido.** En un canal pon el 3xOsc con la forma de onda de ruido (o cualquier sample de
   ruido) y Fathom en su mixer. Toca una nota larga. Sube GANANCIA a +12 dB: la SALIDA azul toma la forma de la curva,
   montada sobre la ENTRADA gris. Mueve FRECUENCIA y Q y mira cómo la siguen.
2. **Ancho vs. angosto.** Sobre un loop de dembow completo: GANANCIA +6 dB en 2 kHz con Q 0.7 y luego con Q 8.
   ¿Cuál cambia más el "carácter" de toda la mezcla? (la ancha). ¿Cuál suena a "un tono" agregado? (la angosta).
3. **Q alto suena (resuena).** Pon solo el kick o un rim/clap. Campana de +18 dB con Q 20 en 1 kHz: el golpe empieza a
   "cantar" una nota. Una campana angosta y alta no solo sube volumen: hace que esa frecuencia **dure** más (por eso
   las subidas se hacen anchas). Baja a +4 dB y Q 1: el golpe vuelve a ser golpe.
4. **Con el volumen igualado.** Haz una subida de +4 dB en 3 kHz con Q 1 sobre la voz. Activa y desactiva ON: ¿mejor?
   Ahora baja SALIDA hasta que con ON y sin ON suene igual de fuerte (unos −1 a −1.5 dB) y vuelve a comparar. Muchas
   veces la "mejora" se achica a la mitad: parte era solo volumen. (En la Fase 6 llega el auto gain que lo hace solo.)

### Receta: "barrer y cortar" una resonancia del 808

1. Pon Fathom en el 808 (después del Riptide si lo saturas). Busca la zona que suena a "cartón" o "caja", que suele
   estar en BARRO o MEDIOS (200–800 Hz).
2. GANANCIA +10 dB, Q 6. Reproduce y mueve FRECUENCIA despacio por esa zona: en algún punto algo "silba" o se pone
   feísimo. Ese es el candidato.
3. Ahora invierte: GANANCIA −4 a −6 dB, mismo Q. Compara con ON/OFF **dentro del beat**, no en solo.
4. Por qué funciona: el 808 distorsionado genera armónicos que se acumulan en los medios-graves; restar una zona
   angosta limpia sin quitarle peso (el sub queda intacto) y le deja sitio al kick y a la voz. **Restar antes que
   sumar**: un corte angosto rara vez se oye como "EQ"; una subida angosta casi siempre sí.

### Reto sin receta

Haz que la voz principal se entienda mejor sobre los acordes **sin subirle el volumen ni tocar la voz**: solo con una
campana en el canal de los acordes. Pistas (si no sale): ¿en qué zona vive la claridad de la voz? (PRESENCIA); ¿qué le
pasa a esa zona en los acordes si la bajas 2–3 dB con un Q ancho (1–1.5)?; compara con ON/OFF con todo sonando, no en
solo; si los acordes se oyen "lejanos", te pasaste.

### Vocabulario

- **Campana / peaking / bell**: la forma de esta fase. En los EQ de consola (SSL, Neve) es la banda "MID".
- **Realce (boost) / corte (cut)**: subir o bajar. "Un corte de 3 dB en 400" = campana de −3 dB en 400 Hz.
- **Q**: lo angosta que es la campana. "Q alto" = angosta. Viene de "factor de calidad" de los circuitos resonantes.
- **Ancho de banda (en octavas)**: el mismo dato que el Q, en musical. "Un tercio de octava" es angosto.
- **Resonancia**: una frecuencia que sobresale y dura (el "boing" de una caja, el silbido de un 808 saturado).
- **Barrido (sweep)**: subir mucho una campana angosta y moverla para encontrar lo que molesta (la receta de arriba).
- **Quirúrgico**: un corte muy angosto que solo toca una resonancia; lo contrario de un EQ "musical" (ancho).

### Errores comunes (y cuándo NO usarlo)

- **Barrer para siempre.** Con +10 dB y Q 6, TODO suena feo en alguna parte: el barrido sirve para encontrar algo que
  ya oías, no para inventar problemas. Si no oías nada raro antes, no cortes.
- **Subidas angostas y altas.** Hacen resonar (ejercicio 3). Para dar carácter, subidas anchas y suaves (Q 0.5–1.5,
  +1 a +4 dB).
- **Ecualizar en solo.** Una voz "delgada" en solo puede ser justo la que se entiende en la mezcla. Decide con todo
  sonando.
- **Comparar sin igualar el volumen** (ejercicio 4).
- **Usar una campana donde va otra forma.** Para quitar los graves que sobran de una voz o un hi-hat, una campana
  ancha en 60 Hz no sirve: hace falta un corte de graves (Fase 3). Para "aire" en los agudos, un shelf (Fase 3).
- **Cuando el problema no es fijo.** Si una resonancia aparece solo en algunas notas del 808 o solo cuando la voz
  canta fuerte, un corte fijo le quita también cuando no hace falta: eso es un EQ dinámico (Fases 9 y 11).
- **El aplastamiento cerca de 20 kHz.** A 44.1 o 48 kHz, una campana en 15 kHz sale un poco más angosta del lado de
  arriba que la de un EQ analógico (es un límite de la forma de digitalizarla). La curva lo muestra tal cual es. En la
  Fase 7 se corrige; mientras, a 88.2/96 kHz casi no pasa.

## Fase 3 — Todas las formas y 24 bandas

Ya no es "una campana": cada una de las **24 bandas** puede tomar diez formas, y las bandas van **en serie** (lo que
una cambia, la siguiente lo recibe; en la curva, sus dB se suman). La campana sirve para una zona; las demás formas
resuelven lo que la campana no puede: quitar todo lo que está debajo de algo, dar "aire" a todo lo de arriba,
inclinar el sonido completo o eliminar un solo tono.

### Conceptos aprendidos

- **Shelf (estante).** Sube o baja *todo* lo que está debajo (shelf de graves) o encima (shelf de agudos) de la
  frecuencia, y se queda plano: son las perillas BASS y TREBLE de un equipo de sonido. La frecuencia marca el **punto
  medio** de la subida: un shelf de agudos de +6 dB en 8 kHz vale +3 dB justo en 8 kHz y casi +6 dB arriba de 15 kHz.
  Con **Q 0.71** sube lo más suave posible; con Q más alto aparece un pequeño "contragolpe" (baja un poco antes de
  subir), que algunos EQ de consola tienen a propósito porque suena más definido.
- **Tilt shelf.** Un balancín: alrededor de la frecuencia, lo que los agudos suben lo bajan los graves (GANANCIA +6 =
  graves −3, agudos +3). Cambia el color sin cambiar mucho el volumen total.
- **Flat tilt.** Una **pendiente pareja** de 20 Hz a 20 kHz, sin ninguna zona marcada (10 dB de GANANCIA = 1 dB por
  octava). Pone la mezcla un poco más "oscura" o más "brillante" de forma natural. Por dentro son 16 shelves diminutos,
  uno por octava, cuyos escalones se funden en una rampa (el mismo truco que convierte ruido blanco en rosa).
- **Cortes (low cut / high cut) y pendiente.** Un corte de graves quita todo lo que está debajo de la frecuencia (el
  famoso *high-pass* o *low cut*); el de agudos, lo de arriba. La **PENDIENTE** dice qué tan rápido cae: con 6 dB/oct,
  una octava más abajo suena a la mitad (suave, casi no se nota que hay filtro); 12 y 24 dB/oct son los más usados;
  48–96 dB/oct, una pared. En la frecuencia del corte vale −3 dB.
- **Butterworth y resonancia.** Los cortes de Fathom son *Butterworth* (lo más plano posible antes del corte). Con **Q
  0.71** es Butterworth puro; con Q más alto aparece un **pico justo antes del corte** (el "golpe" de un filtro de
  sintetizador). En un corte de graves de un bajo, Q 1–1.5 refuerza la nota más grave que queda; Q 0.5 hace la caída
  más redonda.
- **Notch.** Quita **una sola frecuencia por completo** (−100 dB y más): un zumbido de la corriente, una resonancia
  que silba siempre en el mismo tono. El Q dice lo angosto del hueco. A diferencia de una campana de −30 dB, el notch
  no tiene "fondo": en el centro, nada.
- **Pasa banda.** Deja solo la zona alrededor de la frecuencia (en el centro, 0 dB; lejos, nada). Es el efecto
  "teléfono" o "radio vieja" (pasa banda en 1–2 kHz con Q 1).
- **All-pass.** No cambia el **volumen** de ninguna frecuencia (la curva queda plana) sino la **fase**: retrasa unas
  frecuencias respecto de otras. Solo, casi no se oye; sumado con otra señal (dos micros, un kick en capas, una señal
  en paralelo) cambia cómo se suman. Es la primera pista de que un EQ hace algo más que subir y bajar volumen (la
  fase vuelve en la Fase 10).
- **Cambiar de forma sin clic.** No se puede "convertir" un corte en un shelf: Fathom funde la banda a cero en 20 ms,
  cambia la forma y la vuelve a subir. Una banda apagada no gasta CPU.

### Qué hace cada control (y qué muestra la pantalla)

- **Selector de banda** (Banda 1 … 24, las encendidas con ●): qué banda muestra el panel. Un **clic sobre un nodo** del
  visor hace lo mismo. Cada banda tiene su color y empieza en su propia frecuencia (de 30 Hz a 16 kHz), así al
  encender varias no quedan una encima de otra.
- **ON**: enciende la banda (aparece su nodo de color y su relleno).
- **FORMA**: una de las diez. Las perillas que esa forma no usa se atenúan (GANANCIA en los cortes, Q en el flat tilt).
- **PENDIENTE**: solo en los cortes (de 6 a 96 dB/oct). Mira cómo la pared se vuelve vertical.
- **FRECUENCIA / GANANCIA / Q**: lo mismo de la Fase 2, con el significado de cada forma; el texto de la izquierda lo
  traduce (punto medio del shelf, corte a −3 dB, dB/oct del flat tilt…).
- **Visor**: la línea blanca es el resultado **total**; cada banda encendida rellena con su color lo que ella sola sube o
  baja (la elegida, más marcada). Los nodos de cortes, notch, pasa banda y all-pass van en la línea de 0 dB; los de las
  formas con ganancia, sobre su curva (en un shelf, en el punto medio).

### Ejercicio de escucha guiado en FL Studio

1. **Pendientes.** Ruido blanco (3xOsc con la forma de ruido) en un canal con Fathom. Banda 1: corte de graves en 500
   Hz. Pasa por 6, 12, 24, 48 y 96 dB/oct mirando la SALIDA azul: con 6 casi no se siente "filtro"; con 96 parece que
   el sonido se acaba en una pared. Ahora lo mismo sobre un loop de dembow con el corte en 100 Hz.
2. **El corte de graves en la voz.** La voz sola, corte de graves de 24 dB/oct. Súbelo desde 20 Hz despacio: hasta
   ~80–120 Hz (voz masculina) casi no cambia el timbre, pero desaparecen golpes del micro y ruido grave. Sigue
   subiendo: en algún punto la voz se vuelve "delgada". Quédate un poco antes de ese punto y compara dentro del beat
   con ON/OFF.
3. **Shelf vs. campana para el "aire".** En la voz: shelf de agudos +3 dB en 10 kHz (Q 0.71) contra una campana de +3
   dB en 10 kHz (Q 1). La campana suena a "un brillo en un lugar"; el shelf, a "más abierto". Iguala el volumen con
   SALIDA antes de decidir (el shelf sube más energía: bájale ~0.5 dB).
4. **Resonancia del corte.** En el 808: corte de graves en 30 Hz, 24 dB/oct. Q 0.71 contra Q 2: con Q 2 la nota más
   grave que sobrevive "empuja" más (el pico antes del corte se ve en la curva). Más peso, pero también más riesgo de
   retumbar.
5. **Notch vs. campana.** Un tono fijo (3xOsc, seno en A5 = 440 Hz): campana de −30 dB, Q 10 en 440 Hz → todavía se
   oye un poco. Cambia a NOTCH: desaparece. Sube el Q del notch a 40: sigue desapareciendo, y lo demás casi no se toca.
6. **El all-pass "no hace nada"… hasta que sí.** El mismo kick en dos canales del mixer y Fathom en uno solo, con
   all-pass en 80 Hz, Q 0.7. Cada canal solo suena igual que antes; los dos juntos pierden golpe grave: la fase
   desalineada hace que se resten. Por eso la fase importa con capas.

### Receta: limpiar el dembow con cortes y shelves

1. **Hi-hats y percusión aguda:** corte de graves de 12–24 dB/oct en 250–500 Hz. En solo no suenan distinto, pero
   dejan de sumar barro y sub que no aportan nada.
2. **Voz:** corte de graves de 24 dB/oct en 80–120 Hz (masculina) o 120–180 Hz (femenina), y shelf de agudos +2 dB en
   10–12 kHz (Q 0.71) para el aire. El autotune agrega artefactos agudos: el shelf los sube menos que una campana
   angosta y alta.
3. **808:** corte de graves de 24 dB/oct en 25–30 Hz (Q 0.71–1): lo que hay debajo casi ningún sistema lo reproduce y
   le roba headroom al master.
4. **Kick:** si se pelea con el 808, *no* cortes los dos en el mismo sitio. Elige quién manda en el sub: el kick con un
   shelf de graves −2 dB en 60 Hz y el 808 intacto, o al revés.
5. **Bus de acordes / pads:** corte de graves de 12 dB/oct en 150–250 Hz para que no tapen al 808; si suenan lejanos,
   un tilt shelf +2 dB en 2 kHz en vez de tres campanas.

Por qué funciona: cada instrumento ocupa más espectro del que necesita. Los cortes quitan lo que **no aporta pero
suma** (energía que se acumula en el bus y en el master), y los shelves cambian el carácter de una zona entera sin
crear picos que resuenen.

### Reto sin receta

En el master de un beat tuyo, haz que suene "más caro" usando **solo un flat tilt y un corte**, sin campanas. Pistas
(si no sale): una mezcla equilibrada cae suave hacia los agudos (pon una referencia comercial con Fathom en su canal y
mira la inclinación del analizador); ¿tu beat necesita más o menos inclinación que la referencia?; con el flat tilt
basta ±0.3–0.6 dB/oct (GANANCIA ±3–6); el corte va muy abajo (20–25 Hz, 12 dB/oct); compara con el volumen igualado.

### Vocabulario

- **Shelf / estante (low shelf, high shelf)**: sube o baja una zona entera hasta el extremo. "Un shelf de aire" =
  shelf de agudos suave en 10–12 kHz.
- **Tilt**: inclinar todo el espectro alrededor de un punto (la perilla TILT de Riptide hace esto).
- **Low cut / high-pass (HPF)**: corte de graves. **High cut / low-pass (LPF)**: corte de agudos. "High-pass" dice qué
  *pasa*; "low cut", qué *quita*: son lo mismo.
- **Pendiente (slope)**: dB por octava de la caída. "Un HPF de 24" = corte de graves de 24 dB/oct.
- **Butterworth**: el corte más plano antes de la frecuencia de corte; el "estándar".
- **Resonancia**: el pico justo antes del corte; en sintetizadores, el "squelch" del filtro del acid.
- **Notch**: un hueco total y angosto. "Notchear 60 Hz" = quitar el zumbido de la corriente.
- **Pasa banda (band-pass, BPF)**: solo la zona del medio; el "efecto teléfono" de los intros.
- **Fase**: en qué momento llega cada frecuencia. Un all-pass la cambia sin tocar el volumen.

### Errores comunes (y cuándo NO usarlo)

- **Cortar graves "por costumbre" en todo.** Un low cut en cada canal a 100 Hz deja la mezcla delgada, y sumados los
  cortes acumulan giros de fase en los graves. Corta lo que *sobra*, pista por pista, escuchando en contexto.
- **96 dB/oct para todo.** Las pendientes extremas suenan más a "filtro", giran más la fase cerca del corte y pueden
  "anillar" con Q alto. Úsalas cuando de verdad necesitas una pared (quitar el sub de una voz pegada a un 808); para lo
  demás, 12–24 dB/oct.
- **Resonancia alta en el corte de graves del 808.** El pico suma volumen justo en la nota más grave: el limitador del
  master trabaja de más y en el club retumba. Q 0.71–1, salvo que busques ese golpe a propósito.
- **Shelf de agudos con Q alto para "aire".** El contragolpe crea un pico antes de la subida que puede sonar áspero;
  para aire, Q 0.5–0.71.
- **Notch para resonancias que cambian.** Si el tono que molesta cambia con las notas (un 808 con glide), un notch fijo
  falla la mitad del tiempo: eso es dinámica espectral (Fase 11).
- **Flat tilt grande.** Con ±2–3 dB/oct ya es un cambio enorme (20–30 dB de un extremo al otro). En mezcla y master,
  pequeño.
- **Comparar sin igualar el volumen** (siempre): un shelf de agudos casi siempre "gana" la comparación porque suena más
  fuerte.
