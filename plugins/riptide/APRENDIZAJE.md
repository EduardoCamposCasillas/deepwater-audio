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
