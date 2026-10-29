# Referencia del lenguaje Pila LiveCoding ByteBeat

Referencia palabra por palabra del lenguaje. Compañera del `readme.md` (que cubre instalación, arquitectura y flujo de trabajo); acá está exclusivamente **qué hace cada token y qué deja en la pila**. La fuente de verdad ante cualquier discrepancia es `src/parser.c` (TOKEN_TABLE) + `src/interpreter.c`.


## Cómo se leen las fichas

**Firma**: notación tipo Forth `( entrada -- salida )`, con operandos nombrados. El lado izquierdo es el orden de push (el primer nombre se apila primero); el derecho, lo que queda después. `( -- )` significa que la palabra no deja nada; `( x -- x )` que inspecciona sin consumir.

Convenciones globales que aplican a todas las fichas y no se repiten en cada una:

- **Booleanos**: los comparadores y condiciones usan `-1` para verdadero y `0` para falso. Cualquier valor ≠ 0 cuenta como verdadero en `?[`, `?`, `->`.
- **Anomalía = no-op que no toca la pila**: operación que no se puede completar (pila insuficiente, división por cero, canal de `OUT` fuera de rango, índice de string inválido). No es fatal: la palabra queda como no-op esa ejecución y suma al contador que el loop reporta agrupado cada ~2 segundos por stderr. Cuando una palabra no puede completarse (pila insuficiente, divisor cero, canal fuera de rango), se saltea entera: no consume ni produce, solo suma al contador. `1 0 /` deja el `1 0` en la pila; planificá la limpieza en consecuencia.
- **Modos**: con `VINT` activo (default), `+ - *` y las operaciones de bits envuelven a int32 (overflow bytebeat auténtico) y `/ %` son enteras. Con `0 VINT`, todo eso opera en double (bits sigue siendo entera pero sin wrap). Las fichas lo indican donde importa.
- **La pila nace vacía en cada ejecución** de un bloque: no hay estado de pila entre muestras ni entre bloques. Lo persistente vive en variables (`=:`), diccionario, strtab y módulos.
- **Strings**: un literal `"texto"` se interna en strtab en parseo y llega al runtime como un **índice entero**. Las palabras que "consumen un string" (`_:`, `smpload`, `recfile`, `SCOLOR`) esperan ese índice.
- **Literales numéricos**: solo enteros (con signo opcional). No existen literales flotantes: `3.5` es un token desconocido. Fracciones se construyen (`7 2 /` en modo float).
- **Cast a entero por truncamiento**: todo índice o contador que sale de la pila (`nd`, `nr`, shifts, canal de `OUT`, slots de samples, límites y paso de `@[`, `SSTROKE`, etc.) se convierte a int truncando hacia cero: `2.9` cuenta como `2`.
- **Valores no finitos (NaN/Inf)**: el sistema los contiene en cuatro puntos:
  1. la etapa de salida los vuelve silencio antes de los filtros, así un NaN no envenena DCB/LP/delay para siempre;
  2. `sample`/`samplepitch` con posición o frecuencia no finita devuelven silencio en vez de leer fuera del buffer;
  3. `VMIN`/`VMAX`/`T` rechazan no-finitos con anomalía;
  4. las operaciones de bits (`& | ^ ~`) convierten operandos no finitos a 0 antes de operar, y los desplazamientos usan operandos saneados con conteos clampetados;
  5. en modo entero, un resultado no finito de trigonométricas, osciladores o samples se convierte en silencio del rango en vez de castearse como NaN.
  6. en modo entero, `+ - *` tratan un operando no finito como 0, y `/` `%` lo tratan como divisor 0 (anomalía). Los índices y contadores no finitos (`nd`, `nr`, canal de `OUT`, slots, límites de `@[`, cuentas de shift) se convierten a INT_MIN de forma definida, así los chequeos de rango los vuelven anomalía: el bug se ve, no se esconde.
- **Watchdog de instrucciones**: cada ejecución de bloque tiene un presupuesto de instrucciones (`RUN_LIMIT_SAMPLE` 100k para `{s}`, `RUN_LIMIT_BUFFER` 500k para `{b}`/`{b.}`, `RUN_LIMIT_APP` 5M para `{a}`/`{a.}`). Al alcanzarlo se suma una anomalía y se abandona esa ejecución: un loop infinito o un salto hacia atrás ya no cuelgan el proceso, degradan el audio y llenan el contador de anomalías (y el `anomlog`, con el pc del bucle culpable). Si un programa legítimo queda chico, la señal es una tormenta de anomalías: reestructurá (mové el trabajo a `{b}`) o subí el define.


## Límites del sistema

Todos los límites del lenguaje están definidos en `src/limits.h` y son ajustables en tiempo de compilación:

### Diccionario de palabras

- **MAX_WORDS = 2048**: espacio único compartido por funciones (`: nombre ... ;`), alias (`nombre := destino`) y variables (`valor =: nombre`). Si el total de definiciones supera este límite, las nuevas se ignoran con aviso.

### Programas compilados

- **MAX_INST = 2048**: instrucciones por bloque de contexto (`{a}`, `{b}`, `{s}`, etc.). Si un bloque excede este límite, se trunca con aviso.
- **STACK_SIZE = 128**: profundidad máxima de la pila de datos. Operaciones que exceden este límite generan anomalía.
- **MAX_LOOP_DEPTH = 16**: anidamiento máximo de loops `@[ ]@`.
- **MAX_ANCHORS = 64**: cantidad máxima de anclas `#nombre` y jumps `->nombre` por programa.

### Buffer de fuente

- **RPN_BUF_SIZE = 16384**: tamaño del buffer de lectura de archivos `.p` (16 KB). Si un archivo excede este límite:
  - **Modo normal** (livecoding): se trunca con aviso por stderr, el código se compila parcialmente.
  - **Modo `--checkscript`**: se aborta con error fatal. Útil para validar scripts completos en CI/CD.

### Audio

- **MAX_CHANNELS = 16**: tope de canales que acepta `CH`.
- **DEFAULT_SAMPLE_RATE = 8000**: sample rate por defecto (bytebeat clásico).

### Scope

- **SCOPE_RING_SIZE = 65536**: muestras en el ring buffer del scope (≈0.74 s a 88.2 kHz).

### Samples

- **MAX_SMP_SLOTS = 16**: slots de samples disponibles.

### Strings

- **MAX_STRINGS = 256**: strings literales interned.
- **MAX_STRING_LEN = 256**: largo máximo de cada string.

### Diagnósticos

- **MAX_DIAG_MSGS = 12**: mensajes de diagnóstico por carga de archivo.
- **DIAG_MSG_LEN = 120**: largo máximo de cada mensaje.


## Índice global

En el orden de categorías del enum de `opcodes.h`. Las fichas completas están en la sección de su categoría.

| Palabra                     | Categoría       | Descripción                                         |
|-----------------------------|-----------------|-----------------------------------------------------|
| *(literal)*                 | Pila            | Apila un entero                                     |
| `e`                         | Pila            | Descarta el tope                                    |
| `d`                         | Pila            | Duplica el tope                                     |
| `nd`                        | Pila            | Pop n, copia el valor a profundidad n               |
| `r`                         | Pila            | Intercambia los dos del tope                        |
| `nr`                        | Pila            | Pop n, mueve el valor a profundidad n al tope       |
| `cs`                        | Pila            | Vacía la pila                                       |
| `ls`                        | Pila            | Apila la profundidad (incluyéndose)                 |
| `=: nombre`                 | Variables       | Guarda el tope en variable persistente              |
| `nombre`                    | Variables       | Apila el valor de la variable                       |
| `::`                        | Informadores    | Imprime la pila completa                            |
| `.:`                        | Informadores    | Imprime el tope sin consumirlo                      |
| `_:`                        | Informadores    | Imprime un string por índice                        |
| `?`                         | Control         | Ternario                                            |
| `?[` `?!` `]?`              | Control         | If / if-else                                        |
| `@[` `@!` `]@`              | Control         | Loop / salir del loop / cierre                      |
| `i` `j`                     | Control         | Índice del loop / del loop exterior                 |
| `#nombre`                   | Control         | Declara un ancla                                    |
| `->nombre`                  | Control         | Salta al ancla si el tope ≠ 0                       |
| `+` `-` `*`                 | Aritmética      | Suma, resta, multiplicación (wrap int32 en VINT)    |
| `/` `%`                     | Aritmética      | División y módulo (enteras o float según modo)      |
| `gsin*` `gcos*` `gtan*`     | Trigonométricas | sin/cos/tan en grados × amplitud                    |
| `rsin*` `rcos*` `rtan*`     | Trigonométricas | sin/cos/tan en radianes × amplitud                  |
| `hsin*` `hcos*` `htan*`     | Trigonométricas | sinh/cosh/tanh × amplitud                           |
| `logn*` `exp*` `sqrt*`      | Funciones       | log / exp / sqrt × amplitud                         |
| `gsin` `gcos` `gtan`        | Trigonométricas | sin/cos/tan en grados                               |
| `rsin` `rcos` `rtan`        | Trigonométricas | sin/cos/tan en radianes                             |
| `hsin` `hcos` `htan`        | Trigonométricas | sinh/cosh/tanh                                      |
| `logn` `exp` `sqrt`         | Funciones       | log / exp / sqrt                                    |
| `abs`                       | Funciones       | Valor absoluto                                      |
| `min` `max`                 | Funciones       | Mínimo / máximo de dos                              |
| `&` `\|` `^` `~`            | Bits            | AND, OR, XOR, complemento                           |
| `<<` `>>`                   | Bits            | Desplazamientos                                     |
| `==` `!=` `<` `<=` `>` `>=` | Comparadores    | Devuelven -1 / 0                                    |
| `t`                         | Getters audio   | Contador global de muestras                         |
| `sr` `ch` `buf`             | Getters audio   | Sample rate, canales, frames por buffer             |
| `vmin` `vmax`               | Getters audio   | Rango de salida actual                              |
| `T`                         | Setters audio   | Fija el contador `t`                                |
| `SR` `CH` `BUF`             | Setters audio   | Sample rate, canales, buffer (reabren stream)       |
| `VMIN` `VMAX`               | Setters audio   | Rango de salida                                     |
| `VINT`                      | Setters audio   | Conmuta aritmética entera / float                   |
| `OUT`                       | Setters audio   | Escribe la muestra en un canal                      |
| `MUTE`                      | Setters audio   | Silencia la salida sin afectar scope ni grabación   |
| `DCB` `SAT` `LP`            | Setters audio   | DC blocker, saturación, pasa-bajos                  |
| `SCOPE`                     | Scope           | Enciende/apaga el visualizador                      |
| `SZOOM`                     | Scope           | Ventana temporal en ms                              |
| `SCOLOR`                    | Scope           | Color "#rrggbb" por canal                           |
| `SSTROKE`                   | Scope           | Grosor de trazo por canal                           |
| `STYPE`                     | Scope           | Modo: columnas / XY / espectrograma / intercalado   |
| `SRES`                      | Scope           | Pixels por columna                                  |
| `SFFT`                      | Scope           | Tamaño de FFT del espectrograma                     |
| `SBGCOLOR`                  | Scope           | Color de fondo de la ventana                        |
| `SWINX` `SWINY`             | Scope           | Posición de la ventana en pantalla                  |
| `SWINW` `SWINH`             | Scope           | Tamaño de la ventana                                |
| `SFULLSCR`                  | Scope           | Fullscreen desktop on/off                           |
| `SDECOR`                    | Scope           | Decoración (bordes) on/off                          |
| `SWINTOP`                   | Scope           | Siempre arriba on/off                               |
| `scrw` `scrh`               | Scope           | Tamaño del display (getters)                        |
| `winx` `winy`               | Scope           | Posición actual de la ventana (getters)             |
| `winw` `winh`               | Scope           | Tamaño actual de la ventana (getters)               |
| `DELAY`                     | Delay           | Enciende/apaga                                      |
| `DTIME`                     | Delay           | Tiempo en ms                                        |
| `DFEED`                     | Delay           | Ganancia de feedback 0-100                          |
| `DMIX`                      | Delay           | Mezcla seca/húmeda 0-100                            |
| `DFLO` `DFHI`               | Delay           | Filtros del loop de feedback                        |
| `smpload`                   | Samples         | Carga `.i16`/`.f32` en un slot                      |
| `smplive`                   | Samples         | Captura audio procesado en vivo hacia un slot       |
| `smpremove` `smpreset`      | Samples         | Libera un slot / todos                              |
| `smploop`                   | Samples         | Recorta al zero-crossing                            |
| `smpgain`                   | Samples         | Multiplica todas las muestras                       |
| `smpmix`                    | Samples         | Suma un slot sobre otro con clip                    |
| `smpreverse`                | Samples         | Invierte el sample                                  |
| `sample`                    | Samples         | Lee posición con wrapping e interpolación           |
| `samplepitch`               | Samples         | Lee el sample como instrumento a una frecuencia     |
| `smplen`                    | Samples         | Largo del slot en muestras                          |
| `SMPFADE`                   | Samples         | Crossfade en el punto de loop                       |
| `sine` `triangle`           | Osciladores     | Senoidal y triangular                               |
| `saw` `sawpb`               | Osciladores     | Diente de sierra naive / band-limited               |
| `square` `squarepb`         | Osciladores     | Cuadrada naive / band-limited                       |
| `noise`                     | Osciladores     | Ruido blanco                                        |
| `recfile`                   | Grabación       | Path de salida                                      |
| `recstart` `recstop`        | Grabación       | Inicia / detiene y cierra                           |
| `bye`                       | Aplicación      | Corta el loop principal                             |
| `reload`                    | Aplicación      | Fuerza recarga del archivo principal                |
| `default`                   | Aplicación      | Restablece toda la configuración del sistema        |
| `anomlog`                   | Aplicación      | Log detallado de anomalías a archivo por N segundos |

## Sintaxis (elementos que no son palabras)

No pasan por el TOKEN_TABLE: los resuelve el parser en sus fases 0 y 2. Sin embargo condicionan todo lo demás, así que van documentados con el mismo rigor.

- Palabras con I/O pesado (`smpload`, `recfile`, `recstart`, `anomlog`) no están prohibidas en `{s}`, pero ejecutan ese I/O una vez por muestra: casi siempre es un error de diseño, no un efecto.


### Bloques de contexto — `{a` `{a.` `{b` `{b.` `{s` `}`

| Marcador       | Contenido  | Cuándo corre                                |
|----------------|------------|---------------------------------------------|
| `{a`           | setup      | Una vez al cargar/recargar                  |
| `{a.`          | teardown   | Antes de recargar y al salir                |
| `{b`           | por buffer | Una vez por buffer, antes de las muestras   |
| `{b.`          | por buffer | Una vez por buffer, después de las muestras |
| `{s`           | audio      | Una vez por muestra                         |
| *(sin bloque)* | audio      | Ídem: es el bloque `{s}` implícito          |

- `}` cierra el bloque abierto. **No anidan**: un `{` dentro de un bloque no abre nada nuevo.
- Un `{` suelto sin marcador reconocido abre un **bloque apagado** (ver abajo).
- Cada bloque compila a un Program propio de hasta 2048 instrucciones; si un bloque excede el límite de tokens de ruteo, se trunca con aviso.

### Bloques apagados — `{marcador-desconocido ... }`

Cualquier `{` seguido de un marcador que no sea `a`, `a.`, `b`, `b.`, `s` (por ejemplo `{c`, `{x`, `{a-`, `{s-`) descarta **todo** su contenido hasta el primer `}` que no esté dentro de un string. Se elimina en la fase 0, a nivel de carácter: ni sus tokens ni sus strings llegan a existir. Es la forma de "comentar" código que contiene bloques o paréntesis desbalanceados adentro. Si queda sin `}` de cierre, se avisa en la carga.

### Comentarios — `( ... )`

Todo el contenido entre paréntesis se elimina en la fase de tokenización (antes de compilar). No llegan al código ejecutable ni cuestan en runtime.

- **Anidables**: `(( comentario con ( anidado ) dentro ))` funciona correctamente. El parser lleva la cuenta de la profundidad.
- **Multilínea**: un comentario puede abarcar varias líneas.
- No cuestan nada en runtime (no llegan a `program[]`).
- Los strings literales que aparezcan adentro **no** se internan en strtab y no corren los índices de los strings reales.


```
( Este es un comentario simple )
( Comentarios ( anidados ) funcionan )
(
  Comentario
  multilínea
)
```

### Fin de código (`...`)

El token `...` corta la lectura del programa. Todo lo que sigue se ignora por completo (no se tokeniza). Útil como separador de bloques para `send-blocks.py` y el envío por `s-e` desde Emacs, que parten el archivo exactamente por esas líneas.

**Uso típico**: en livecoding, ponés `...` al final del fragmento que estás probando para que el parser no intente leer código incompleto o experimental que sigue más abajo.

```
{a 22050 SR 2 CH }
t 440 * sine 0 OUT
...
( Este código no se ejecuta hasta que quites el ... de arriba o envies el bloque específico )
t 880 * sine 1 OUT
```


### Strings — `"texto"`

Un string entre comillas dobles se almacena en una tabla interna persistente (strtab) y el token resultante es un **índice numérico** (entero común y corriente en la pila). El mismo contenido siempre devuelve el mismo índice (interning: dos literales idénticos son "el mismo" string).

La distinción entre "índice de string" y "número común" la hace la palabra que lo consume: `_:` lo interpreta como texto para imprimir, `recfile` como path de archivo, `SCOLOR` como color hex, `smpload` como ruta de sample; cualquier operación aritmética lo trata como el entero que es.

- **Persistencia**: la strtab sobrevive entre recargas del código fuente. Un string definido en una sesión sigue disponible aunque cambies el archivo `.p`.
- **Límites**: máximo 256 strings distintos, 255 caracteres por string (se avisa por stderr si se alcanza el límite).
- **No hay escape de comillas**: si necesitás una comilla dentro de un string, no se puede (limitación del parser simple). Para mensajes con comillas, usá apóstrofes o evitá el carácter.
- Se reconocen como unidad en tokenize: se internan en strtab y el token resultante es el **índice en decimal**.
- Strtab persistente entre recargas (máx. 256 entradas): el mismo contenido reusa índice.
- El índice es un número común: podés apilarlo, guardarlo en variables y pasarlo a `_:`/`smpload`/`recfile`/`SCOLOR` cuando quieras.

Ejemplos:

```
"salida.wav" recfile recstart    ( grabar a archivo )
"#ff0088" 0 SCOLOR               ( color magenta para canal 0 )
"/dev/shm/anom.log" 10 log       ( log de anomalías por 10 segundos )
"hola" _:                        ( imprime "hola" por stderr )
```


### Definiciones de función — `: nombre cuerpo ;`

- Se resuelven en parseo (fase 2): cada uso de `nombre` se sustituye por el cuerpo ya resuelto (las referencias internas se expanden recursivamente **en el momento de la definición**).
- Persistentes entre recargas durante la sesión. Redefinir con el mismo nombre y mismo tipo reemplaza; con tipo distinto (función vs alias vs variable) se rechaza con aviso.
- `nombre` no puede ser una palabra reservada ni un literal numérico.
- Límites: 128 definiciones totales (funciones + alias + variables), 256 tokens de cuerpo.
- Un `;` sin `:` de apertura se descarta con aviso; una función sin `;` consume hasta el final y se avisa.
- **Las definiciones no funcionan dentro de cuerpos de función**: `=:`, `:=` y `: ;` anidados dentro de un `: ... ;` se copian como tokens sueltos al cuerpo y llegan al compilador como tokens desconocidos ("Token desconocido: =:"), dejando la pila desbalanceada. Las definiciones solo se procesan en el flujo principal de cada bloque, nunca dentro de una función. Si una función necesita estado, componela con pila (`d`, `nd`, `nr`) o usá variables definidas afuera.
- **No hay palabra para borrar entradas del diccionario ni de la strtab**: viven hasta que termina el proceso. Redefinir con el mismo nombre y mismo tipo reemplaza; cambiar de tipo se rechaza con aviso.
- **Las definiciones no cuestan runtime y pueden ir en cualquier lado**: `: ;` y `:=` se consumen en la fase de parse (fase 2), antes del ruteo a bloques: no emiten instrucciones a `{a}`, `{b}` ni `{s}`, no consumen el presupuesto de 1024 instrucciones de ningún bloque y no figuran en el conteo `>>>` de la carga. Escribirlas fuera de bloques o dentro de `{a}` es exactamente equivalente; elegí por organización, no por performance.
- **El orden textual importa**: una definición solo rige para los usos que aparecen *después* en el mismo archivo (el diccionario se construye mientras se recorre el flujo). Como el diccionario persiste entre recargas, en la segunda carga de un archivo un uso anterior a su definición sí resuelve (con el cuerpo de la carga previa): si algo "empieza a funcionar" tras un reload sin cambios, depende del orden de cargas.
- **`=:` es la excepción**: el registro de la variable es de parse, pero el store es una instrucción de runtime que queda en el bloque donde apareció. `exp =: v` fuera de bloques storea una vez por muestra; en `{b}`, una vez por buffer; en `{a}`, una vez por carga. En `{s}` no es un error: es el patrón natural de estado por muestra (acumuladores, fases); para setup, usá `{a}` o `{b}`.

### Alias — `nombre := destino`

Registra `nombre` como sinónimo: el cuerpo del alias es el cuerpo **ya resuelto** de `destino` en el momento del alias. Mismas reglas de persistencia y reservas que las funciones.

### Variables de runtime — `expresión =: nombre`

Tienen ficha propia en la categoría Variables (son opcodes reales: `OP_VAR_STORE` / `OP_VAR_LOAD`), pero conviene saberlo acá: el slot se asigna una sola vez en parseo y el valor persiste entre recargas.

### `,_ ` — cita: no inlinizar, llamar en runtime

`,_ nombre` le dice al preparser que **no inlinee** la próxima palabra: el token sobrevive al parse y compila a una llamada en runtime (`OP_CALL`) que ejecuta **la definición vigente en ese momento** (late binding). Es la válvula de escape del inlining: el cuerpo citado se resuelve en cada ejecución, no en cada carga.

- **Solo funciones y alias**: `,_ variable` da diagnóstico de carga (las variables ya son dinámicas por slot; citarlas no agregaría nada). Nombre inexistente: diagnóstico de carga. El token omitido no rompe el resto del programa.
- **Recursión habilitada**: como la resolución es en runtime, una función puede citarse a sí misma o mutuamente: `: fact dup 1 > ?[ dup 1 - ,_ fact * ]? ;`. Profundidad máxima de llamadas: 16 niveles (anomalía al exceder).
- **BCASC: las citas no envejecen**: un bloque retenido que cita `,_ voz` adopta automáticamente las redefiniciones futuras de `voz`. El aviso de redefinición de bcasc cubre solo los usos inlineados.
- **Watchdog compartido**: la llamada consume del mismo presupuesto de pasos de la ejecución que la contiene; una recursión runaway muere por watchdog con una sola anomalía, y el reporte de `anomlog`/STRICT apunta al `OP_CALL` culpable.
- **Performance**: una cita es una llamada por uso (frame de ejecución propio con sus loops); el inlining sigue siendo más barato. Regla práctica: inlineá en el hot path de `{s}`; citá donde necesites late binding, recursión, o que bloques retenidos vean redefiniciones.
- **Plegado**: `fold_constants` no toca las citas (`2 3 ,_ suma` no se pliega: el resultado puede cambiar entre cargas).
- **Dentro de cuerpos**: `,_ nombre` se guarda como cita en el cuerpo de la función que la contiene (no se resuelve al definir); se resuelve al compilar, con el diccionario completo, por eso también admite referencias hacia adelante en el mismo archivo.


## Pila

La pila es de 64 doubles (`STACK_SIZE`) y **nace vacía en cada ejecución** de un bloque: no hay arrastre entre muestras ni entre bloques. Todo lo que sigue asume eso.

### Literal numérico — `( -- n )`

Un token numérico apila su valor. **Solo enteros**, con signo opcional: el tokenizer no reconoce punto decimal, así que `3.5` es un token desconocido (aviso en carga y omisión). Las fracciones se construyen en runtime (`7 2 /` en modo float).

- **Límites**: el valor se guarda como `int` en la instrucción (`atoi`); literales fuera de rango int32 quedan truncados por `atoi`. Anomalía si la pila está llena.
- **Nota**: las operaciones puras entre literales se pliegan en parseo cuando el resultado es idéntico en ambos modos (ver Aritmética/Bits/Comparadores), así que `2 3 +` en realidad compila a un solo push de `5`.
- El valor se convierte a `int` (32 bits) con wraparound natural, **para cualquier largo y cualquier base**: la acumulación interna es unsigned de 64 bits (definida), así que un literal de 19+ dígitos o una base alta envuelven a 32 bits igual que el resto de la aritmética entera del lenguaje, sin comportamiento indefinido.

```
{a 40 2 + .: }        ( imprime 42: se pliego en parseo )
```

### Decimales estándar

`123`, `-42`, `0`. Se parsean como siempre: enteros en base 10 con signo opcional.

### Con base explícita (`<dígitos>'<base-1>`)

Formato compacto para representar enteros en cualquier base de 2 a 62. El sufijo `'X` indica la base: `base = valor(X) + 1`, donde `X` es un dígito del mismo conjunto que el cuerpo.

**Mapeo de caracteres a valor:**

| Carácter | Valor |
|----------|-------|
| `0`–`9`  | 0–9   |
| `a`–`z`  | 10–35 |
| `A`–`Z`  | 36–61 |

**Bases comunes y su sufijo:**

| Sufijo | Base | Ejemplo     | Equivalente decimal |
|--------|------|-------------|---------------------|
| `'1`   | 2    | `1010110'1` | 86                  |
| `'3`   | 4    | `1203003'4` | 18778               |
| `'7`   | 8    | `777'7`     | 511                 |
| `'9`   | 10   | `255'9`     | 255 (redundante)    |
| `'f`   | 16   | `ff00b1'f`  | 16711857            |
| `'z`   | 36   | `zz'z`      | 1295                |
| `'Z`   | 62   | `10'Z`      | 62                  |

**Reglas:**

- El sufijo es **exactamente un carácter** después del `'`. `'15` no es válido (usar `'f` para base 16).
- **Signo al inicio**: `-ff'f` es -255. No se admite signo en el medio.
- Un dígito del cuerpo con valor ≥ base **rechaza** el literal, que cae como token desconocido. Esto preserva nombres de funciones o alias que contengan `'`.
- El valor se convierte a `int` (32 bits) con wraparound natural. Coherente con la aritmética bytebeat.

**Uso típico**: patrones de bits legibles (`1010110'1` vs 86), máscaras hexadecimales (`ff00ff'f` vs 16711935), constantes con estructura visible en su base natural.

```
{a
  11111111'1 =: mask8        ( 255 )
  ff00ff'f   =: magenta      ( 16711935 )
  10101010'1 8 >> =: alt8    ( 170 )
}
t mask8 & 0 OUT
```

### `e` — `( x -- )`

Descarta el tope. Anomalía con pila vacía.

```
{a 1 2 3 e :: }       ( Pila: 1 2 )
```

### `d` — `( x -- x x )`

Duplica el tope. Anomalía con pila vacía o llena.

```
{a 1 2 3 d :: }       ( Pila: 1 2 3 3 )
```

### `nd` — `( xn ... x0 n -- xn ... x0 xn )`

Pop `n`, copia el valor a profundidad `n` (0 = el tope) y lo apila. Es el PICK de Forth; `1 nd` equivale al over clásico.

- **Límites**: anomalía si `n < 0`, si `n >= profundidad` o si la pila está llena.

```
{a
  1 2 3 0 nd :: cs    ( Pila: 1 2 3 3 )
  1 2 3 1 nd ::       ( Pila: 1 2 3 2 )
}
```

### `r` — `( a b -- b a )`

Intercambia los dos del tope. Anomalía con menos de 2 elementos.

```
{a 1 2 3 r :: }       ( Pila: 1 3 2 )
```

### `nr` — `( xn ... x0 n -- x(n-1) ... x0 xn )`

Pop `n`, extrae el valor a profundidad `n` y lo mueve al tope (ROLL de Forth). Anomalía si `n < 0` o `n >= profundidad`.

```
{a 1 2 3 2 nr :: }    ( Pila: 2 3 1 )
```

### `cs` — `( x1 ... xn -- )`

Vacía la pila por completo. **Nunca falla** (con pila vacía es no-op). Útil al final de secciones experimentales dentro de un bloque, para que los residuos no contaminen lo que sigue (ramas de `?[ ]?` que dejan distinta cantidad de valores, loops cortados con `@!`).

```
{a 1 2 3 :: cs 0 :: } ( Pila: 1 2 3  y luego  Pila: 0 )
```

### `ls` — `( x1 ... xn -- x1 ... xn n+1 )`

Apila la profundidad de la pila **incluyendo el valor que se está apilando**: con pila vacía apila 1. Sirve como contador para macros que recorren o vacían la pila.

- **Límites**: solo puede fallar con la pila ya llena (64), caso prácticamente imposible.

```
{a 0 1 2 ls :: }            ( Pila: 0 1 2 4 )
: vaciar ls 1 - @[ e ]@ ;   ( vacia usando ls como contador )
```

## Variables de runtime

### `expresión =: nombre` — `( x -- )`

Pop del tope y guardado en el slot persistente de `nombre`. El slot se asigna **una sola vez en parseo**, la primera vez que aparece el `=: nombre`; el valor persiste entre recargas durante la sesión.

- **Bloque**: cualquiera; lo típico es `{a}`/`{b}` para setup y `{s}` para estado que evoluciona por muestra (fases, contadores).
- **Límites**: el nombre no puede ser palabra reservada ni numérico; no puede coexistir como función o alias (se rechaza con aviso); tope de 128 variables y de 128 definiciones totales (variables + funciones + alias compartidas). Anomalía en runtime con pila vacía.

```
{a
  2 =: a  3 =: b
  a b *  a b +  a b -  a b /  b a /  ::
}
```

### `nombre` — `( -- x )`

Apila el valor actual de la variable. Es el mismo token del nombre: se resuelve en parseo a una carga de slot.

- **Límites**: leer una variable nunca asignada devuelve **0** (los slots nacen en cero); no hay forma de distinguir "no asignada" de "vale 0". Anomalía solo con pila llena.

```
{b t 10 >> 13 % =: a }   ( escribe en {b} )
{s a 255 & 0 OUT }       ( lee en {s} )
```

## Informadores

Imprimen por stderr **sin consumir**. Los tres están pensados para `{a}`/`{b}`: en `{s}` imprimirían hasta 8000 veces por segundo y te inundan la terminal.

### `::` — `( x1 ... xn -- x1 ... xn )`

Imprime toda la pila (`Pila: v1 v2 ...`). Con pila vacía no imprime nada.

```
{a 1 2 3 :: }
```

### `.:` — `( x -- x )`

Imprime el tope (`Valor: %g`) sin consumirlo. Anomalía con pila vacía.

```
{a 1 2 3 .: }           ( Valor: 3, la pila queda 1 2 3 )
```

### `_:` — `( i -- i )`

Imprime el string de strtab cuyo índice es el tope (`Texto: ...`). No consume.

- **Límites**: índice inválido imprime `Texto: <indice invalido: N>` y suma anomalía. Recordá que los índices los asigna el orden de interning: los strings de comentarios y bloques apagados **no** consumen índice (se eliminan antes de tokenizar), pero los de librerías cargadas antes sí.

```
{a "abc" _: "xyz" _: 0 _: 1 _: }
{b t 2 % ?[ "impar" ?! "par" ]? _: }
```

## Control de flujo

### `?` — `( a b cond -- c )`

Ternario: si `cond ≠ 0` queda `a`, si no queda `b`.

```
{a 2 3 -1 ?  2 3 1 ?  2 3 0 ? :: }   ( Pila: 2 2 3 )
```

### `?[` — `( cond -- )`

Abre un if: pop de la condición; si es 0 salta al `?!` pareado o al `]?` si no hay else.

- **Límites**: anomalía con pila vacía. Cercos desbalanceados (`?[` sin `]?`) se avisan en la carga y el salto queda sin mapa (el cuerpo correría siempre).

### `?!` — `( -- )`

Separador if/else: salta al `]?` pareado. Solo el **primer** `?!` de cada `?[` se mapea; uno de más queda como no-op inofensivo.

### `]?` — `( -- )`

Cierra el if. No-op en runtime.

```
{b t 2 % ?[ "impar" ?! "par" ]? _: }
```

### `@[` — `( fin inicio paso -- )`

Abre un loop con el orden de push **fin, inicio, paso**. Semántica:

- `paso == 0` → el cuerpo no se ejecuta.
- El signo del paso se autocorrige a la dirección inicio→fin.
- `inicio == fin` → loop vacío.
- El índice `i` arranca en `inicio`, el cuerpo corre, y el avance/condición se evalúan en `]@` (condición: `i < fin` con paso positivo, `i > fin` con negativo).

- **Límites**: anomalía con menos de 3 elementos; anidamiento máximo 16 (pasado eso el frame no se pusha y el `]@` correspondiente genera anomalía).
- **Pasos extremos**: un paso de INT32_MIN se convierte a INT32_MAX al corregir el signo (su negación no es representable); si el avance de `i` desbordara int32, el loop se cierra en vez de envolver. Ambos casos quedan definidos.

```
{a 9 0 1 @[ i :: ]@ }              ( imprime 0 .. 8 )
{a
  8 1 2 @[
    4 -1 1 @[ i j * :: ]@
  ]@
}
```

### `]@` — `( -- )`

Cierra el loop: avanza `i` en `paso` y evalúa la continuación; si sigue, salta al `@[`; si no, popea el frame. Anomalía fuera de un loop.

### `@!` — `( -- )`

Sale del loop activo inmediatamente (break). Anomalía fuera de un loop.

```
{a
  10 1 1 @[
    i :: d 2 >= ?[ @! ]?
  ]@
}
```

### `i` — `( -- n )` / `j` — `( -- n )`

`i` apila el índice del loop actual; `j` el del loop exterior (nivel 2). Anomalía fuera de loop (`i`) o con menos de 2 niveles (`j`).

### `#nombre` — `( -- )`

Declara un ancla en la posición actual. No-op en runtime; el marcador existe en compile-time.

- **Límites**: no se puede declarar dentro de `@[ ]@` ni `?[ ]?` (aviso de compilación y omisión); ancla repetida se ignora (queda la primera); 64 anclas por programa.


### `->nombre` — `( cond -- )`

Pop de la condición; si es ≠ 0 salta al ancla `#nombre`. Pensado para saltos largos tipo switch dentro de `{s}`.

- **Límites**: mismas restricciones de ubicación que `#`; si el ancla no existe, aviso en carga y **anomalía en runtime** cada vez que la condición es verdadera; anomalía también con pila vacía.
- Los `->nombre` pueden aparecer en el fuente ANTES que su `#nombre`: la compilación resuelve anclas en dos pasadas.


```
{s
  t
  t 11 % 2 < ->A
  t 17 % 4 < ->B
  1 ->Z
  #A cs t 3 << 1 ->Z
  #B cs t 13 >> t 5 << & 1 ->Z
  #Z 1 >> 255 % 0 OUT
}
```

## Aritmética

### `+` `-` `*` — `( a b -- c )`

Suma, resta, multiplicación. **Dependen del modo**: con `VINT` activo operan en int64 y envuelven el resultado a int32 (el overflow bytebeat auténtico, que es parte de la estética); con `0 VINT` operan en double.

- **Límites**: anomalía solo con pila insuficiente. En modo entero el wrap es silencioso y deliberado.
- **Fold**: se pliegan en parseo si ambos operandos son literales y el resultado no desborda int32.

```
{a 2 3 + ::  2 3 - ::  2 3 * :: }
2000000000 2000000000 + .:    ( modo entero: wrap a -294967296 )
```

### `/` `%` — `( a b -- c )`

División y módulo. Modo entero: división entera con truncamiento hacia cero y `%` entero; modo float: división double y `fmod`.

- **Límites**: divisor 0 → anomalía (no-op), en ambos modos. **No se pliegan** en parseo porque el resultado depende del modo activo en runtime.
- **Signo en modo entero**: sigue la regla de C: cociente truncado hacia cero y resto con el signo del dividendo (`-7 2 %` → -1). En float, `fmod` conserva la misma convención de signo.

```
{a
  0 VINT 2 3 /      ( 0.666667 )
  1 VINT 2 3 /      ( 0 )
  ::
}
```

## Trigonométricas y hiperbólicas

Todas comparten firma `( ang amp -- val )`: aplican la función al ángulo y **multiplican por la amplitud**, que es la forma de escalar el resultado al rango que estés usando (`128`, `127`, `10000`...). En modo entero el resultado pasa por `to_int32`; en float queda double. Ninguna se pliega en parseo.

### `gsin*` `gcos*` `gtan*` — ángulo en **grados**

sin/cos/tan de grados × amplitud.

```
{a 10000 =: m  90 m gsin*  45 m gsin*  30 m gsin*  0 m gsin* :: }
( Pila: 10000 7071 5000 0 )
```

### `rsin*` `rcos*` `rtan*` — ángulo en **radianes**

sin/cos/tan de radianes × amplitud.

```
{a
  10000 =: m  314159 =: pi
  pi 2 / m rsin*  pi 4 / m rcos* ::
}
```

### `hsin*` `hcos*` `htan*` — hiperbólicas (sin conversión de ángulo)

hsin/hcos/htan del valor × amplitud. `htan*` es la favorita para suavizar señales con forma de S (y es la misma curva de la saturación `SAT`).

- **Límites compartidos de la familia**: `gtan*`/`rtan*` tienen asíntotas (valores enormes cerca de 90°/π/2, que en modo entero wrappean a cualquier cosa); `hsin*`/`hcos*`/`exp*` explotan exponencialmente con entradas grandes. Es ruido útil si lo buscás, basura si no.

```
t 200 % 100 - 5 / 90 htan* 128 + d 0 OUT 1 OUT
```

### `gsin` `gcos` `gtan` — ángulo en **grados**

sin/cos/tan de grados.

```
{a 10000 =: m  90 m gsin  45 m gsin  30 m gsin  0 m gsin :: }
( Pila: 10000 7071 5000 0 )
```

### `rsin` `rcos` `rtan` — ángulo en **radianes**

```
{a
  10000 =: m  314159 =: pi
  pi 2 / m rsin  pi 4 / m rcos ::
}
```

### `hsin` `hcos` `htan` — hiperbólicas (sin conversión de ángulo)

hsin/hcos/htan del valor. `htan` es la favorita para suavizar señales con forma de S (y es la misma curva de la saturación `SAT`).

- **Límites compartidos de la familia**: `gtan`/`rtan` tienen asíntotas (valores enormes cerca de 90°/π/2, que en modo entero wrappean a cualquier cosa); `hsin`/`hcos`/`exp` explotan exponencialmente con entradas grandes. Es ruido útil si lo buscás, basura si no.

```
t 200 % 100 - 5 / 90 htan 128 + d 0 OUT 1 OUT
```

## Funciones varias

### `abs` — `( x -- |x| )`

Valor absoluto: `llabs` en modo entero, `fabs` en float.

```
{a 1 abs -1 abs :: }    ( Pila: 1 1 )
```

### `min` `max` — `( a b -- m )`

Mínimo y máximo de dos. Comparan como doubles en ambos modos (no hay wrap involucrado).

```
{a 1 2 min -1 1 max :: }
```

### `logn*` `exp*` `sqrt*` — `( x amp -- val )`

log natural, exponencial y raíz cuadrada, con el mismo esquema de escala que las trigonométricas.

- **Límites**: dominio inválido (log de ≤ 0, sqrt de negativo) produce -inf/NaN en modo float, y en modo entero el casteo de NaN es comportamiento indefinido: **evitalo** con un `max` o un `?[ ]?` guardian. No se pliegan.

```
{a 2 1000000 logn  2 100 exp  2 10 sqrt :: }
```

### `logn` `exp` `sqrt` — `( x -- val )`

log natural, exponencial y raíz cuadrada, con el mismo esquema de escala que las trigonométricas.

- **Límites**: dominio inválido (log de ≤ 0, sqrt de negativo) produce -inf/NaN en modo float, y en modo entero el casteo de NaN es comportamiento indefinido: **evitalo** con un `max` o un `?[ ]?` guardian. No se pliegan.

```
{a 2 1000000 logn  2 100 exp  2 10 sqrt :: }
```


## Bits

Siempre operan como enteros (castean a int64), en ambos modos. La diferencia es el wrap: en modo entero el resultado se envuelve a int32; en float queda como double del int64.

### `&` `|` `^` — `( a b -- c )`

AND, OR, XOR. Se pliegan en parseo entre literales.

- **Operandos no finitos**: NaN/Inf se convierten a 0 antes de operar (casteo definido, sin UB); magnitudes fuera de rango int64 se clampetan a ±9.2e18 y truncan. `1000 1 fe 1 &` da 0, no basura indefinida.

```
{a 1 1 &  0 1 &  0 0 & :: }
```

### `~` — `( a -- c )`

Complemento a uno (NOT). Unary: se pliega entre literales.

- **Operandos no finitos**: NaN/Inf se convierten a 0 antes de operar (casteo definido, sin UB); magnitudes fuera de rango int64 se clampetan a ±9.2e18 y truncan. `1000 1 exp 1 &` da 0, no basura indefinida.

```
{a -1 ~ 0 ~ 1 ~ :: }   ( Pila: 0 -1 -2 )
```


### `<<` `>>` — `( a n -- c )`

Desplazamientos. El shift se clampeta a 0..63.

- **La cuenta depende del modo**:
  - Modo entero (`1 VINT`, bytebeat): la cuenta se **mascara a 5 bits** (`& 31`), como JavaScript y la tradición bytebeat: `1 32 <<` es `1 << 0` = 1, `1 33 <<` es 2, y las cuentas negativas también envuelven (`1 -1 <<` es `1 << 31`).
  - Modo float (`0 VINT`): la cuenta se clampeta a 0..63 y con ≥ 32 **satura**: `<<` da 0 y `>>` da 0 o -1 según el signo del operando.
- `<<` es **lógico sobre 32 bits**: el operando se casta a `uint32` (vía int32); los bits que salen por arriba se pierden, no se propagan.
- `>>` es **aritmético sobre int32**: conserva el signo (los negativos llenan con unos).
- Se pliegan en parseo solo cuando las dos semánticas coinciden (cuentas 0..31, u operando 0); con cuentas ≥ 32 o negativas el plegado se abstiene y decide el runtime según el modo.
- **Operandos no finitos**: NaN/Inf se convierten a 0 antes de operar (casteo definido, sin UB); magnitudes fuera de rango int64 se clampetan a ±9.2e18 y truncan. `1000 1 exp 1 &` da 0, no basura indefinida.


```
{a 1 1 <<  1 2 << :: }
{a 4 1 >>  4 2 >> :: }
```

### Portar expresiones de bytebeat JS/C a RPN

Los operadores infijos de JS/C tienen precedencias que reagrupan todo al traducir a mano. Reglas que este lenguaje heredó de comparar contra Dolchan:

1. **`%` y `*` atan más fuerte que `>>`/`<<`**: `t>>1%128` es `t >> (1%128)` = `t >> 1`, no `(t>>1) % 128`. Y el operando derecho de un shift es una expresión aditiva completa: `x>>14*t` es `x >> (14*t)`.
2. **Las cuentas de shift variables son la textura**: en `((t>>1%128)+20)*3*t>>14*t>>18` el shift del medio es por `(14*t) & 31`, que cambia muestra a muestra y barre el valor por todos los desplazamientos. Traducirlo como multiplicaciones por `t>>14` y `t>>18` (rampas de segundos) produce el clásico "suena mucho más lento".
3. **JS trunca recién en el operador de bits**: los productos corren en double y se envuelven a int32 solo cuando un `>>`/`&` los fuerza. Nuestro modo entero envuelve en cada `+ - *`. Para fidelidad exacta, hacé la cadena de productos con `0 VINT` y volvé a `1 VINT` antes de los shifts.
4. **Cuentas de shift**: en modo entero ya se enmascaran `& 31` solas (no agregues `31 &` a mano: es redundante, aunque inofensivo); en float saturan.
5. **Máscara de salida**: Dolchan recorta a 8 bits de su lado; cerrá la expresión con `255 &` (o el `% 256` de la pieza) antes de `OUT`, y verificá el sample rate del reproductor de referencia (8000 vs 11025/22050 cambia altura y velocidad de todo).

Ejemplo — harism 2011-10-09, `((t>>1%128)+20)*3*t>>14*t>>18`:

```
{s
  t 1 >> 20 + 3 * t * 14 t * >> 18 >> 255 & 0 OUT
}
```

y la versión de fidelidad JS exacta (productos en double):

```
{s
  0 VINT t 1 >> 20 + 3 * t * 1 VINT 14 t * >> 18 >> 255 & 0 OUT
}
```


## Comparadores

### `==` `!=` `<` `<=` `>` `>=` — `( a b -- flag )`

Devuelven `-1` (verdadero) o `0` (falso). Comparan como **doubles en ambos modos**: no hay versión entera, porque la comparación no depende del wrap. Se pliegan en parseo entre literales.

- **Cuidado en modo entero**: comparás el valor *ya wrappeado* de las operaciones previas, que es exactamente lo que querés en bytebeat (preguntar por el overflow es parte del lenguaje).

```
{a 1 1 == .:  1 0 == .: }      ( Valor: -1  y  Valor: 0 )
t 100 % 50 < ?[ 255 ?! 0 ]? 0 OUT
```


## Getters de audio

### `t` — `( -- n )`

Apila el contador global de muestras (`sample_index`): cuántas muestras se emitieron desde que arrancó el proceso (o desde el último `T`). Es la fase de todo: en `{s}` crece de a 1 por ejecución, así que cualquier patrón periódico se construye modulándolo.

- **Límites**: se apila como double tal cual (int64 → double, sin wrap): el envolvimiento int32 que da la estética bytebeat aparece después, al pasarlo por `+ - *` en modo entero. No depende de `VINT`.
- **Bloque**: típicamente `{s}`; en `{b}`/`{a}` para depuración o para calcular duraciones.

```
{b t .: }                      ( imprime el contador una vez por buffer )
t d 5 >> 1 nd 8 >> | * 255 & 0 OUT
```

### `sr` `ch` `buf` `vmin` `vmax` — `( -- n )`

Apilan los parámetros actuales del stream y del rango de salida: sample rate en Hz, cantidad de canales activos, frames por buffer, y mínimo/máximo del rango de salida como doubles.

- **Límites**: leen el valor **ya aplicado**, no el pendiente: después de un `SR` el nuevo valor recién se ve cuando el stream se reabre entre buffers.
- **Uso típico**: construir duraciones independientes del sample rate (`t 2 * sr /`), o escalas relativas al rango (`vmin vmax + 2 /` = silencio).

```
{a sr .: ch .: buf .: vmin .: vmax .: }
```

## Setters de audio

### `T` — `( v -- )`

Fija el contador `sample_index` a `v`. Reinicia fases y patrones sin tocar el stream.

```
{a 0 T }
{b t .:  t 10000 >= ?[ 0 T ]? }   ( loop de 10000 muestras )
```

### `SR` `CH` `BUF` — `( v -- )`

Sample rate, cantidad de canales (1..16) y frames por buffer. **No aplican de inmediato**: marcan un cambio pendiente que el loop principal efectúa entre buffers, cerrando y reabriendo el stream de PortAudio. Eso produce un **corte breve inevitable**, y de paso resetea DCB/LP/delay/scope (sus estados internos) porque dependen del sample rate.

- **Límites**: `SR` y `BUF` con valor ≤ 0 se ignoran en silencio (sin anomalía); `CH` fuera de 1..16 también. `BUF` se clampeta a 16..4096 al aplicar. Valores absurdos de `SR` los rechaza PortAudio con error por stderr.
- **Bloque**: `{a}`. Modularlos en `{b}` es posible (tus tests lo hacen como efecto), pero cada cambio es un corte: usalo deliberadamente, no por accidente.

```
{a 8000 SR 2 CH 256 BUF }
{b t 12 >> 19 & LP }              ( esto no reabre: LP es inmediato )
```

### `VMIN` `VMAX` — `( v -- )`

Fijan el rango de salida del programa. Aplican **de inmediato**. Definen tres cosas a la vez: el mapeo de normalización (rango → [-1,1]), el punto de silencio (`(vmin+vmax)/2`, que es lo que queda en los canales sin `OUT`), y la escala de osciladores y samples (que devuelven valores ya dentro del rango).

- **Límites**: si `vmax == vmin`, la normalización devuelve 0 (silencio total) en vez de dividir por cero. Cambiarlos en `{s}` modula el volumen/offset muestra a muestra (efecto válido, pero es AM literal).

```
{a 0 VMIN 255 VMAX }        ( bytebeat 8 bits clasico )
{a -1 VMIN 1 VMAX }         ( floatbeat simetrico )
```

### `VINT` — `( v -- )`

Conmuta la aritmética: `0` = modo float (doubles), cualquier otro valor = modo entero (wrap int32 en `+ - *`, división/módulo enteros). Aplica de inmediato.

- **Límites**: no afecta comparadores ni getters (siempre doubles), ni bits/shifts (siempre enteros, pero con o sin wrap según modo). Los literales siguen siendo enteros en ambos modos.

```
{a 0 VINT }   t 220 * sine 0 OUT    ( seno suave )
{a 1 VINT }   t d 5 >> 1 nd 8 >> | * 255 & 0 OUT
```

### `OUT` — `( val ch -- )`

Escribe `val` en `sample_out[ch]` para la muestra que se está evaluando. El canal viene en el tope. Es la única salida de audio del lenguaje.

- **Bloque**: **solo `{s}`**. `sample_out` se limpia al silencio al empezar cada muestra, antes de correr `{s}`: un `OUT` en `{a}` o `{b}` se pisa y se pierde.
- **Límites**: canal fuera de `[0, num_channels)` → anomalía (no-op). Canales sin `OUT` en una muestra quedan en silencio. Con `CH 1`, todo `OUT` a canal ≥ 1 anomalía cada muestra: vas a ver el reporte de anomalías saturado.

```
t 220 * sine 0 OUT
t 330 * sine 1 OUT          ( stereo, requiere 2 CH )
```

### `DCB` `SAT` `LP` — `( v -- )`

Los tres interruptores/procesadores de la etapa de salida, en este orden de pipeline: normalización → **DCB** → **LP** → delay → **SAT**/clamp.

| Palabra | Consume | Efecto                                                                           | Default    |
|---------|---------|----------------------------------------------------------------------------------|------------|
| `DCB`   | `v`     | ≠0 activa el DC blocker (pasa-altos ~6 Hz a 8 kHz, ~35 Hz a 44.1 kHz)            | 1 (activo) |
| `SAT`   | `v`     | ≠0 satura con `tanh`; 0 = clamp duro a [-1,1]                                    | 0 (clamp)  |
| `LP`    | `v`     | Pasa-bajos de un polo, intensidad 0..99 (clampeta; negativo→0)                   | 0 (off)    |
| `MUTE`  | `v`     | 0 = audio activo (default), ≠0 = silencio en parlantes; scope y grabación siguen | 0 (off)    |

- **Límites**: aplican de inmediato y por canal (cada canal tiene su estado de filtro). `DCB` activo por defecto no es decorativo: protege parlantes de la componente continua natural del bytebeat. `SAT 1` con señales fuertes comprime picos y suena "musical"; con `DFEED 100` es tu única red contra el runaway.

```
{a 1 DCB 1 SAT 2 LP }
{a 1 MUTE 1 SCOPE } ( ver sin escuchar )
{b t 10 >> 1 & MUTE } ( mutea cada 1024 muestras )
```

### `MUTE` — `( v -- )`

Conmuta el audio de salida: 0 = audio activo (default), ≠0 = silencio total en los parlantes/auriculares. El scope y la grabación **siguen recibiendo las muestras procesadas**, así que podés analizar visualmente o grabar lo que "sonaría" sin que efectivamente suene.

- **Uso típico**: análisis visual en contextos donde no podés hacer sonar (biblioteca, noche), o cuando el sonido es muy molesto pero querés ver la forma de onda o el espectro.
- **La grabación NO se mutea**: `recfile`/`recstart` capturan la señal procesada aunque `MUTE` esté activo. La combinación `1 MUTE` + grabación es el flujo natural para registrar una sesión que no podías escuchar en el momento.
- **Bloque**: cualquiera. `{a}` para sesiones mudas fijas (análisis visual, contextos donde no podés sonar); `{b}` o `{s}` para muteo condicional.
- **Límites**: no afecta el pipeline de procesamiento (DCB, LP, SAT y delay siguen trabajando y acumulando estado: al desmutear, la cola de eco está viva), ni la grabación, ni el scope. Sin fades: el corte es instantáneo (click posible con señal fuerte).
- **Log**: los cambios de estado se imprimen por stderr (`audio: mute activado/desactivado`) solo cuando el valor efectivamente cambia; setear el mismo valor repetido no loggea. La línea de estado del stream (`audio: stream abierto/reabierto ...`) también incluye el mute vigente.

```
( ver sin escuchar )
{a 1 MUTE 1 SCOPE }
...
( grabar sin sonar )
{a 1 MUTE "/dev/shm/toma.wav" recfile recstart }
```

## Scope

El scope se alimenta del buffer **ya procesado** (post SAT/clamp), el mismo que va a PortAudio y al recorder: lo que ves es exactamente lo que suena y lo que grabás. La ventana se abre perezosamente en el loop principal al detectar `SCOPE` activo, y se cierra sola (dejando `SCOPE` en 0) si la cerrás con la X.

### `SCOPE` — `( v -- )`

≠0 enciende, 0 apaga el visualizador SDL2. Abrir/cerrar repite `SDL_Init`/`SDL_Quit`: funciona, pero si lo toggleás en `{s}` vas a ver parpadeos y costo de inicialización. Dejalo fijo en `{a}`.

Notas:
- Cerrar la ventana con la X pone `SCOPE` en 0: para reabrirla hay que volver a setearlo desde el código; no se reabre sola.
- **Geometría de ventana**: `default` resetea los valores deseados pero respeta la ventana actual si ya está abierta. Esto evita romper layouts que el usuario ajustó manualmente. Si querés forzar un reset completo de geometría, cerrá y reabrí el scope o usá las palabras de geometría explícitamente.

### `SZOOM` — `( ms -- )`

Ventana temporal visible, en milisegundos. Clampeta a 1..2000. Es **temporal**, no muestral: la misma configuración se ve a la misma velocidad a 8 kHz que a 44.1 kHz.

- **Límites**: el ring buffer interno tiene 65536 muestras por canal. A 44.1 kHz, 2000 ms serían 88200 muestras: el display se recorta a lo que haya en el ring (~1486 ms). A 8 kHz el tope no llega a morderte.

### `SCOLOR` — `( "#rrggbb" ch -- )`

Color del trazo (modos 0 y 1) o del teñido del espectrograma (modos 2 y 3) para el canal `ch` (0..15). Consume un literal de string: en la pila llega como índice de strtab.

- **Límites**: formato inválido (sin `#`, largo ≠ 7, hex malo) imprime aviso por stderr y suma anomalía, sin tocar el color anterior. Canal fuera de rango → anomalía. Default: blanco en todos los canales.
- El color se pasa como string literal en formato `"#rrggbb"`. Formato inválido (sin `#`, largo ≠ 7, hex malo) imprime aviso por stderr y suma anomalía, sin tocar el color anterior.

```
{a "#dd0000" 0 SCOLOR  "#0088ff" 1 SCOLOR }
```

### `SSTROKE` — `( w ch -- )`

Grosor del trazo por canal: cada unidad agrega una línea paralela por columna (modo 0) o por segmento (modo 1). Mínimo 1; **sin tope superior** (el clamp se quitó a pedido): valores enormes multiplican las llamadas de dibujo y pueden poner lento el render.

- **Límites**: canal fuera de 0..15 se ignora en silencio. Cambios de grosor fuerzan redibujo completo de la textura.

```
{a 3 0 SSTROKE  1 1 SSTROKE }
```

### `STYPE` — `( v -- )`

Modo de visualización, clampeta a 0..5:

| Valor | Modo                                                                                                                                                                                                                                                                                                                              |
|-------|-----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| 0     | Forma de onda por columnas (min/max por pixel), una fila por canal, render incremental, envelope                                                                                                                                                                                                                                  |
| 1     | XY / Lissajous: canal 0 en X, canal 1 en Y (con < 2 canales cae a 0)                                                                                                                                                                                                                                                              |
| 2     | XY fósforo: la textura persiste y cada frame se apaga con un velo translúcido de `SBGCOLOR` mientras se dibujan solo los segmentos nuevos. Estelas tipo osciloscopio CRT; costo O(buf_size) por frame, plano en sample rate y zoom                                                                                                |
| 3     | Espectrograma por franjas: una banda por canal, frecuencia log (20 Hz–Nyquist), intensidad en dB (-60..0) teñida con el color del canal                                                                                                                                                                                           |
| 4     | Espectrograma intercalado: columnas alternadas por canal compartiendo toda la altura                                                                                                                                                                                                                                              |
| 5     | Espectrómetro circular: ángulo = frecuencia log (20 Hz–Nyquist) alrededor de un perímetro con gap de 12°; la magnitud en dB se dibuja como segmentos centrados en el hilo (gotas que cuelgan hacia adentro y hacia afuera); peak-hold como puntitos exteriores; un círculo por canal en grilla automática con el número al centro |
| 6     | waveform (polilínea interpolada, multicanal)                                                                                                                                                                                                                                                                                      |


Notas:
- Cambiar de modo fuerza redibujo completo. Los números de canal se sobreimprimen solo en modos 0 y 3 (en 4 el color ya identifica; en 1 y 2 no hay filas).
- En el modo 2 no aplican `SZOOM`, `SRES` ni `SFFT`; la persistencia de las estelas es fija (constante de decaimiento en scope.c). Al entrar desde otro modo, el contenido previo se apaga con las estelas en vez de limpiarse: la transición es parte de la estética.
- En el modo 5 no aplican `SZOOM` ni `SRES` (no hay eje temporal ni columnas); aplican `SFFT` (resolución espectral), `SCOLOR` (hilo y gotas por canal), `SSTROKE` (grosor de gota) y `SBGCOLOR`. El suavizado temporal, el decaimiento del peak-hold y la posición del hilo son constantes de compilación (`CIRC_SMOOTH_ALPHA`, `CIRC_PEAK_DECAY`, `CIRC_THREAD_FRAC` en scope.c).

### `SRES` — `( v -- )`

Pixels por columna en el **modo 0** (clampeta 1..16). `4 SRES` dibuja 4× menos líneas: el min/max de cada columna se calcula sobre el rango completo de samples que caen en esos 4 pixels, así que no se pierde información de señal, solo resolución horizontal de dibujo. No afecta a los modos 1, 2 y 3.

### `SFFT` — `( v -- )`

Tamaño de FFT de los espectrogramas (modos 3 y 4). Redondea a la potencia de 2 válida: ≤256→256, ≤512→512, ≤1024→1024, resto→2048. Más grande = mejor resolución frecuencial y peor temporal (y más costo por columna: una FFT por pixel nuevo por canal).

### `SBGCOLOR` — `( "#rrggbb" -- )`

Color de fondo de la ventana del scope. Consume un literal de string en formato `"#rrggbb"`. Aplica en caliente si la ventana está abierta.

- **Límites**: formato inválido imprime aviso por stderr y suma anomalía, sin tocar el color anterior. Default: negro (`"#000000"`).

```
{a "#112233" SBGCOLOR }   ( fondo azul noche )
```

### `SWINX` `SWINY` — `( n -- )`

Fijan la posición de la ventana del scope en coordenadas de pantalla, en pixels. Admiten valores negativos (monitores a la izquierda/arriba del principal). Si la ventana todavía no está abierta, el valor queda guardado y se aplica al abrirla; si no se setean nunca, la ventana se crea centrada.

- **Bloque**: cualquiera; típicamente `{a}` para layouts fijos, o `{b}` para ventanas que acompañan (cada llamado es una llamada SDL, barato pero no gratis).
- **Límites**: en fullscreen la posición la maneja el desktop; el valor guardado se reaplica al salir con `0 SFULLSCR`. Sin anomalías salvo pila vacía.

```
{a 1 SCOPE  100 SWINX  40 SWINY }
```

### `SWINW` `SWINH` — `( n -- )`

Fijan el tamaño de la ventana (ancho y alto en pixels). Clamp a mínimo 1. El valor inicial 0 significa "default" (800×400); cualquier seteo explícito lo reemplaza. Con la ventana abierta, el cambio dispara el redibujo completo del scope (misma ruta que un resize manual).

- **Límites**: en fullscreen desktop el tamaño lo pone el desktop; el valor guardado se reaplica al salir. Cambiar el tamaño en `{s}` es técnicamente posible y técnicamente una mala idea.

```
{a scrw 2 / SWINW  scrh 2 / SWINH }   ( un cuarto de pantalla )
```

### `SFULLSCR` — `( n -- )`

0 = ventana normal; cualquier otro valor = fullscreen **desktop** (borderless sobre el escritorio, sin cambio de modo de video del monitor). Al volver a 0 se reaplican la posición y el tamaño guardados.

- **Límites**: mientras está activo, `SWINX`/`SWINY`/`SWINW`/`SWINH` guardan pero no se ven. Default 0.

```
{a 1 SCOPE 1 SFULLSCR }
```

### `SDECOR` — `( n -- )`

0 = ventana sin bordes ni barra de título (modo vitrina o proyección); ≠0 = con decoración. Default 1. Aplica en caliente si la ventana está abierta.

- **Límites**: sin efecto visible en fullscreen (que ya es borderless por naturaleza).

```
{a 1 SCOPE 0 SDECOR }   ( vitrina limpia )
```

### `SWINTOP` — `( n -- )`

0 = ventana normal; ≠0 = siempre arriba (always-on-top). Default 0. Aplica en caliente si la ventana está abierta.

- **Límites**: requiere SDL ≥ 2.0.16 (Debian bookworm y posteriores lo tienen). En X11 funciona con la mayoría de los WMs (Mutter/GNOME, KWin/KDE, xfwm, i3, sway en XWayland). **En Wayland nativo el compositor puede ignorar el hint** (el protocolo Wayland no tiene el concepto de always-on-top de la misma forma). Si no funciona, no es un bug: es el compositor diciéndote que no.

```
{a 1 SCOPE 0 SDECOR 1 SWINTOP }   ( vitrina flotante siempre visible )
```

### `scrw` `scrh` — `( -- n )`

Ancho y alto del display donde vive la ventana (o del principal si la ventana todavía no existe). Inicializan el subsistema de video si aún no estaba inicializado, así que se pueden usar desde `{a}` antes del primer `SCOPE` activo: son la base de los layouts adaptativos.

- **Límites**: devuelven 0 si no hay display disponible (sesión headless); un script que divida por ellos debe guardarse con un `max` o un `?[ ]?`.

### `winx` `winy` `winw` `winh` — `( -- n )`

Posición y tamaño **actuales** de la ventana del scope, tal como los reporta el window manager. Si la ventana no está abierta, devuelven los valores deseados guardados por `SWINX`/`SWINY`/`SWINW`/`SWINH` (o 800×400 si no fueron seteados).

- **Uso típico**: scripts adaptativos que responden al resize manual, o que posicionan elementos relativos al tamaño actual de la ventana.
- **Límites**: solo fallan con pila llena.

```
{a
  1 SCOPE 0 SDECOR 1 SWINTOP
  scrw 2 / winw 2 / - SWINX   ( centrar a mano respetando el ancho actual )
  scrh 2 / winh 2 / - SWINY
}
```



## Delay

Eco con feedback, procesado **después de LP y antes de SAT/clamp**, por canal. El flujo interno por muestra es:

```
1. delayed  = lectura del buffer circular a DTIME ms
2. delayed  = DFHI(delayed) -> DFLO(delayed)     (si cada filtro es > 0)
3. escritura = seca + delayed * (DFEED/100)
4. salida    = seca * (1 - DMIX/100) + delayed * (DMIX/100)
```

O sea: los filtros afectan **a la vez** lo que realimenta y lo que sale como señal húmeda. El buffer es de 65536 muestras por canal y se resetea al reabrir el stream (`SR`/`CH`/`BUF`).

### `DELAY` — `( v -- )`

≠0 enciende el procesamiento, 0 lo bypassea (el buffer deja de escribirse y la salida queda seca).

### `DTIME` — `( ms -- )`

Tiempo de delay en milisegundos, clampeta a 1..2000.

- **Límites**: el índice de lectura es entero y se recalcula por muestra: modular `DTIME` en `{b}` (o peor, en `{s}`) produce saltos de índice audibles como zipper/click. Si querés delays modulados suaves, modulá por ahora asumiendo ese artifact (tu "berimbao" lo usa como textura). Con sample rates altos el tope real lo pone el buffer: a 44.1 kHz, 2000 ms = 88200 muestras > 65536, así que el delay efectivo se clampeta a ~1486 ms.

### `DFEED` — `( 0-100 -- )`

Ganancia del feedback, clampeta. 0 = eco simple sin repetición; 100 = realimentación total: con `DFHI` en 0 puede acumular DC y crecer hasta el clamp (bombeo). Un `DFHI` chico (5-20 Hz) evita el retumbe sin comerse el cuerpo del eco.

### `DMIX` — `( 0-100 -- )`

Mezcla seca/húmeda, clampeta. 0 = solo señal seca; 100 = solo eco.

- **Nota**: con `DMIX 0` el delay **sigue procesando y realimentando** internamente; solo cambia qué llega a la salida. Subir el mix después recupera la cola acumulada.

### `DFLO` `DFHI` — `( hz -- )`

Filtros del loop de feedback: `DFLO` pasa-bajos y `DFHI` pasa-altos, ambos de un polo, en Hz, clampetados a 0..20000, con **0 = desactivado**. Se aplican en ese orden (alta primero, baja después), igual que el patch de Pure Data de referencia (`hip~ 9` / `lop~ 9999`).

- **Uso típico**: `9 DFHI 9999 DFLO` replica el comportamiento del patch; `DFHI` alto convierte el eco en un susurro sin graves; `DFLO` bajo evita que el feedback se vuelva metálico o sibilante.

```
{a
  2 LP 2 CH
  1 DELAY 23 DTIME 80 DFEED 50 DMIX
  0 DFHI 10000 DFLO
}
{b
  t 4 >> 20 % 10 + DTIME
  t 12 >> 190 % 3000 + DFHI
  t 12 >> 390 % 100 + DFLO
}
{s
  t d 5 >> 1 nd 8 >> | * 56 & 0 OUT
  t d 3 >> 1 nd 7 >> | * 65 & 1 OUT
}
```


## Samples

Dieciséis slots numerados (0..15) que viven en `sample.c` y **persisten entre recargas** durante la sesión: un reload no libera ni recarga nada que vos no recargues explícitamente en `{a}`. Internamente todo se guarda como float normalizado a [-1,1]; al leer, el valor se escala al rango `[VMIN,VMAX]` actual, así que un mismo sample se adapta si cambiás el rango en vivo.

El formato de lectura se decide por extensión: `.i16`/`.rawi16` → int16 little-endian, `.f32`/`.rawf32` → float32 little-endian. Cualquier otra extensión (incluidas `.raw`, `.wav` o ninguna) **fuerza** la interpretación según el `VINT` activo en el momento del `smpload`: entero → int16, float → float32. La forzadura nunca impide la carga: es el efecto de rotura de formato, y avisa por stderr en vez de fallar.

### `smpload` — `( "path" slot -- )`

Carga un archivo raw sin header en el slot. Extensiones explícitas: `.i16` o `.rawi16` → int16 little-endian; `.f32` o `.rawf32` → float32 little-endian (sin distinguir mayúsculas). Cualquier otra extensión fuerza el formato según el `VINT` del momento (1 → int16, 0 → float32) y avisa por stderr; si el archivo empieza con `RIFF` agrega un segundo aviso de que parece un WAV y se leerá con el header incluido como datos. Si el slot ya estaba ocupado, **lo sobreescribe** liberando la memoria anterior.

- **Bloque**: `{a}`. Cada reload re-ejecuta `{a}` y recarga: con samples chicos es instantáneo, con archivos grandes puede notarse un gap breve por reload.
- **Límites**: archivo inexistente, vacío o sin memoria → mensaje por stderr y el slot queda como estaba; **no es anomalía** (salvo índice de string inválido, que sí suma). La extensión forzada nunca impide la carga. La forzadura se decide en el momento del `smpload`: cambiar `VINT` después no reinterpreta lo ya cargado, porque los datos ya viven como float interno.
- **Toca el filesystem sin sandbox**: ver "Modelo de confianza" en el README.

```
{a
  "../aud/sample3.i16"   0 smpload       ( int16 explicito )
  "/dev/shm/toma.rawf32" 1 smpload       ( float32 explicito )
  1 VINT "/dev/shm/toma.raw" 2 smpload   ( .raw no reconocida: fuerza int16 )
}
```

### `smplive` — `( n canal slot -- )`

Captura las últimas `n` muestras del **audio procesado** (después de DCB/LP/SAT/delay/clamp, normalizado a [-1,1]) del `canal` indicado y las publica en `slot` como un sample común: después le podés hacer `smploop`, `smpgain`, `smpreverse`, `smpmix`, o leerlo con `sample`/`samplepitch`. Es el puente entre lo que está sonando ahora y el banco de samples.

- **Atómica**: el buffer se llena fuera del slot y se publica en un solo paso; cualquier lector ve el sample anterior completo o el nuevo completo, nunca un estado intermedio.
- **Historia corta**: si el ring todavía no tiene `n` muestras (sesión recién arrancada), el principio se rellena con silencio.
- **Límites**: `n` se clampeta a 65536 (`SCOPE_RING_SIZE`); `n < 1`, slot fuera de rango, canal ≥ canales actuales o falta de memoria → anomalía (no-op). El ajuste de loop del slot se resetea: recalculalo con `smploop` después si lo necesitás.
- **No requiere scope abierto**: el ring de audio procesado se alimenta aunque la ventana esté cerrada (`0 SCOPE`).
- **Bloque**: `{b}` para capturas periódicas. En `{s}` recapturaría una vez por muestra (malloc + copia por muestra): no lo hagas.

```
{b
  ( congela 1 s del canal 0 en el slot 2, una vez por segundo )
  t sr % 0 == ?[ sr 0 2 smplive ]?
}
{s
  165 2 samplepitch 4 / 0 OUT   ( el momento congelado, vuelto instrumento )
}
```


### `smpremove` — `( slot -- )` / `smpreset` — `( -- )`

Liberan la memoria de un slot o de todos. Slot inválido o ya vacío es no-op silencioso en ambos casos; `smpreset` nunca falla.

```
{a
  "../aud/sample1.i16" 0 smpload
  "../aud/corto.i16"   1 smpload
  0 smplen  1 smplen
  smpreset
  0 smplen  1 smplen     ( ambos devuelven 0 ahora )
  ::
}
```

### `smploop` — `( slot -- nuevo_largo )`

Recorta el largo del sample al **zero-crossing más cercano al final**, buscando desde atrás dentro del último 10%: acepta un cambio de signo entre muestras adyacentes o una muestra con |valor| < 0.001. Es la herramienta quirúrgica contra el click de loop: si el corte termina en cero, la transición al principio es continua sin necesidad de crossfade.

- **Límites**: si no encuentra un crossing razonable deja el largo intacto. Slot inválido devuelve 0. Es destructivo (modifica el largo): combiná con `smplen` para verificar el resultado.

```
{a
  "../aud/sample3.i16" 0 smpload
  0 smploop          ( descarta el residuo final que clickeaba )
}
```

### `smpgain` — `( gain slot -- )`

Multiplica todas las muestras del slot por `gain`, que puede ser fraccionario o **negativo** (gain negativo = espejo de fase, no reverse temporal). No clipea: los valores pueden quedar fuera de [-1,1] en memoria; el recorte aparece recién en el pipeline de salida (SAT/clamp), que es donde tiene que aparecer.

```
{a
  12 0 smpgain       ( amplifica )
  -1 0 smpgain       ( invierte la fase )
}
```

### `smpmix` — `( src dst -- )`

Suma el slot `src` sobre el slot `dst`, muestra a muestra, hasta el mínimo de los dos largos, con clip a [-1,1]. `dst` queda modificado; `src` intacto. Slot inválido o vacío → no-op silencioso.

```
{a
  "../aud/sample1.i16" 1 smpload
  0 1 smpmix         ( mezcla el slot 0 sobre el slot 1 )
}
```

### `smpreverse` — `( slot -- )`

Invierte el sample en el lugar (reverse temporal real, a diferencia de `smpgain` negativo). Slot inválido → no-op.

```
{a 1 smpreverse }
```

### `sample` — `( pos slot -- val )`

Lee el valor del sample en `pos`, con tres capas de procesamiento:

1. **Wrapping por módulo**: posiciones mayores al largo loopean; posiciones negativas reproducen al revés (`t -1 *` es el reverse gratis).
2. **Interpolación lineal** entre las dos muestras adyacentes: las velocidades no enteras (`t 1.5 *`, `t 10 *`) suenan limpias, sin el granulado del truncamiento.
3. **Crossfade `SMPFADE`** aplicado sobre ambos puntos interpolados si caen en la zona de loop.

El resultado se escala a `[VMIN,VMAX]` y se redondea según el modo (`to_int32` en entero). Slot vacío o inválido devuelve **silencio** (el punto medio del rango), no anomalía: un sample que no cargó suena como ausencia, no como error.

- **Límites**: Posición o frecuencia no finita (NaN/Inf) → silencio del rango: nunca lectura inválida ni crash.

```
t 3 * 2 smplen % 2 sample 0 OUT      ( loop a triple velocidad )
t -1 * 2 smplen % 2 sample 0 OUT     ( loop al reves )
t 1500000 % 300 + 0 sample 0 OUT     ( ventana movil de 300 en 300 )
```

### `samplepitch` — `( freq slot -- )`

Convierte el sample en instrumento: calcula internamente `fase = (t * freq / sr) mod 1` y lee en `pos = fase * largo`, igual que los osciladores calculan su fase. El sample completo suena una vez por ciclo de `freq` Hz.

- **Límites**: hereda interpolación, wrapping y crossfade de `sample`. Slot vacío → silencio. A frecuencias altas el sample se comprime tanto que se vuelve un timbre (que es exactamente el punto). Posición o frecuencia no finita (NaN/Inf) → silencio del rango: nunca lectura inválida ni crash.

```
t 8 >> 7 & 5 % 0 samplepitch 0 OUT   ( notas discretas del sample 0 )
```

### `smplen` — `( slot -- n )`

Largo actual del slot en muestras (refleja recortes de `smploop`). 0 si está vacío o es inválido.

- **Cuidado**: `t 0 smplen %` con slot vacío es una división por cero → anomalía cada muestra. Verificá la carga antes de loopear por largo.

```
{a "../aud/corto.i16" 0 smpload  0 smplen .: }   ( Valor: 10932 )
```

### `SMPFADE` — `( n -- )`

Samples de crossfade en el punto de loop: en los últimos `n` samples del buffer, la lectura mezcla progresivamente con la cabeza desplazada en 1, de modo que la última muestra del crossfade sea exactamente `data[0]` y la transición del wrapping sea continua.

- **Límites**: negativo se clampeta a 0; **no tiene tope superior**, pero si `n >= largo del sample` el crossfade se desactiva solo (la condición interna exige `n < largo`). Aplica a todos los slots por igual (es global, no por slot).

```
{a 600 SMPFADE 1 DCB 1 SAT 0 LP 0 DELAY }
t 0 smplen % 0 sample 5 >> 0 OUT
```

## Osciladores

Todos consumen **frecuencia en Hz** (double, incluso en modo entero) y calculan su fase internamente como `fmod(t * freq / sr, 1)`: son autónomos, no hay que alimentarlos con `t`. La salida se escala a `[VMIN,VMAX]` (centro + amplitud) y se redondea con `to_int32` en modo entero. Frecuencia negativa refleja la fase (onda espejada); frecuencia 0 devuelve el valor de fase 0 constante.

### `sine` — `( freq -- val )`

Senoidal pura. La más barata y la única sin discontinuidades ni esquinas: no genera aliasing propio.

```
440 sine 0 OUT
```

### `triangle` — `( freq -- val )`

Triangular: sube de -1 a 1 en la primera mitad del ciclo, baja en la segunda. Sin discontinuidades de amplitud (solo de pendiente), así que tampoco necesita corrección: suena limpia por naturaleza. Buena base para flautas, pads y moduladores suaves.

```
321 triangle 0 OUT
```

### `saw` / `sawpb` — `( freq -- val )`

Diente de sierra de -1 a 1. `saw` es la versión naive: la discontinuidad en el cierre del ciclo genera armónicos infinitos que se pliegan sobre Nyquist (aliasing audible como brillo metálico). `sawpb` le resta un término **PolyBLEP** en la discontinuidad: misma forma, sin el plegado. La diferencia es sutil abajo de ~500 Hz y enorme arriba de 2 kHz.

```
321 saw 0 OUT        ( cruda, estetica digital )
321 sawpb 0 OUT      ( band-limited, suena analogica )
```

### `square` / `squarepb` — `( freq -- val )`

Cuadrada al 50% (±1). Dos discontinuidades por ciclo (en fase 0 y 0.5); `squarepb` aplica PolyBLEP en ambas. Sin corrección el aliasing la vuelve chillona; con corrección queda el hueco clásico de cuadrada.

```
321 square 0 OUT
321 squarepb 0 OUT
```

### `noise` — `( -- val )`

Ruido blanco uniforme en `[VMIN,VMAX]`. **No consume nada**: cada invocación avanza un paso de un LCG (semilla fija al arrancar el proceso). La secuencia es global y continúa entre bloques y recargas, así que dos `noise` en la misma muestra de `{s}` devuelven valores distintos.

- **Límites**: no es reproducible muestra a muestra (depende de cuántas veces lo llamaste desde el arranque, no de `t`): no lo uses donde necesites determinismo por posición temporal.

```
noise 0 OUT
1000 square noise / 10 * 0 OUT    ( cuadrada modulada por ruido )
```

## Grabación

Ciclo de vida en tres palabras; el volcado efectivo ocurre una vez por buffer desde el loop principal, así que inicio y fin tienen precisión de un buffer. El nombre final del archivo lo decide `recstart` según la regla de extensiones de `recfile`: solo `.wav` produce WAV con header; todo lo demás es RAW con sufijo automático de formato.

### `recfile` — `( "path" -- )`

Registra el path de salida. **No abre nada todavía**: solo lo recuerda para el próximo `recstart`, que es quien resuelve el nombre final. Índice de string inválido → stderr + anomalía.

- Consume un índice de string (el literal `"path.wav"` se convierte en índice en parse-time). El formato (WAV o RAW) se infiere de la extensión: `.raw` → RAW sin header; cualquier otra → WAV.
- Regla de formato: si el path termina en `.wav` (sin distinguir mayúsculas) se graba WAV con header; **cualquier otra extensión implica RAW** y el nombre final lleva un sufijo explícito que agrega el sistema: `.rawi16` (int16) o `.rawf32` (float32), según el `VINT` congelado en `recstart`.
- Si el path ya terminaba en `.raw`, `.rawi16` o `.rawf32`, ese sufijo se **reemplaza** por el del modo actual (no se acumula: `toma.raw` no produce `toma.raw.rawi16`). La extensión nunca miente sobre el contenido.
- El log de `recstart` imprime el nombre final ya sufijado: es el nombre que después hay que pasarle a `smpload` para el round-trip.
- Toca el filesystem sin sandbox: ver "Modelo de confianza" en el README.

### `recstart` — `( -- )`

Abre el archivo con el nombre final que dicta la regla de `recfile` y **congela los parámetros del momento**: sample rate, canales, y formato de muestra (int16 si `VINT` está activo, float32 si no). Si ya había una grabación activa, la cierra prolija antes de abrir la nueva. Sin path previo o archivo no abrible → mensaje por stderr y queda sin grabar (no es anomalía).

### `recstop` — `( -- )`

Detiene: si es WAV, parchea el encabezado con los tamaños reales y cierra; informa los bytes escritos por stderr. No-op seguro si no había grabación.

- **Límites compartidos**: no cambies `SR`, `CH` ni `VINT` mientras grabás: el header quedó congelado en `recstart` y el archivo resultante sería inconsistente con sus datos. El patrón canónico es arrancar en `{a}` y cerrar en `{a.}`, que cubre reload y salida:

```
{a
  5 LP 0 SAT
  "/dev/shm/bb-01.wav" recfile
  1 ?[ recstart ?! recstop ]?    ( 1 graba, 0 corta: switch de sesion )
}
```

```
{a
  0 =: cnt
  "registro.wav" recfile recstart
}
{b
  cnt 1 + =: cnt
  cnt 100 > ?[ recstop bye ]?    ( graba 100 buffers y sale )
}
```

Round-trip grabar → cargar sin renombrar, y rotura deliberada:

```
{a 1 VINT "/dev/shm/toma" recfile recstart }   ( abre /dev/shm/toma.rawi16 )
{a. recstop }
...
{a "/dev/shm/toma.rawi16" 0 smpload }          ( vuelve a entrar limpio )
{a 0 VINT "vieja.wav" 0 smpload }              ( avisa RIFF y lee crudo: glitch )
```

## Control de aplicación

### `bye` — `( -- )`

Pone `running` en 0: el loop principal termina y la salida limpia corre `{a.}` del programa actual, cierra la grabación (parcheando el header), libera los slots de samples, cierra el scope y el stream de audio. Es la forma de terminar una sesión desde el propio código (típico: al cumplirse una condición en `{b}`).

```
{a "chau" _: bye }
```

### `reload` — `( -- )`

Fuerza una recarga del archivo principal con el ciclo completo del hot-reload (`{a.}` del programa saliente, reparseo, `{a}` del entrante), **aunque el archivo no haya cambiado en disco**. No consume pila y nunca falla: si se la llama, actúa.

- **Bloque**: `{b}` o condicionales en `{s}`. **Nunca desnuda en `{a}`**: como `{a}` corre en cada recarga, un `reload` incondicional ahí produce un loop de recargas infinito. Desnuda en `{s}` recargaría una vez por muestra: igual de patológico.
- El flag se consume una vez por vuelta del loop principal: varios `reload` en el mismo buffer producen una sola recarga.
- Patrón de un solo disparo: una variable bandera que se autoapaga antes de recargar.

```
{b
  quiero_reset ?[ 0 =: quiero_reset  reload ]?
}
( y en cualquier otro bloque, una sola vez:  1 =: quiero_reset )
```


### `default` — `( -- )`

Restablece toda la configuración del sistema a los valores de arranque del proceso (apéndice A): audio (SR/CH/BUF via reapertura de stream, rango VMIN/VMAX, VINT), procesamiento (DCB, SAT, LP, MUTE), scope (apagado, zoom/tipo/fft/resolución, colores y grosores, fondo negro, geometría de ventana centrada 800x400 con decoración, sin fullscreen ni always-on-top), delay (off con sus defaults) y SMPFADE. Además pone `t` en 0, resetea el contador de anomalías y limpia el estado de filtros y del buffer de delay.

- **No toca**: diccionario persistente (`: ;`, `:=`), valores de las variables (`=:`), strtab, samples cargados ni grabaciones en curso. Es un reset de configuración, no de sesión: tus definiciones y tu contenido sobreviven.
- **Bloque**: típicamente la primera línea de un `{a}` al probar fragmentos en secuencia, para que residuos de configuraciones anteriores no se mezclen.
- **Límites**: nunca falla (sin anomalía). Si el stream ya estaba en los valores default no produce corte innecesario. Como el scope apagado es el default, después de `default` hay que reenabilitalo si lo estabas usando.
- **Scope**: resetea los valores deseados de geometría (posición centrada, tamaño 800×400, con decoración, sin fullscreen ni always-on-top, fondo negro), pero **no fuerza cambios en una ventana ya abierta**. Si la ventana fue redimensionada manualmente o está en modo sticky/fullscreen, queda como está. Para forzar un reset de geometría, usá explícitamente `800 SWINW 400 SWINH scrw 2 / winw 2 / - SWINX scrh 2 / winh 2 / - SWINY`, o cerrá y reabrí el scope (`0 SCOPE 1 SCOPE`).
- **Costo**: cada `default` resetea el buffer de delay (8 MB) y los filtros. En `{a}` es gratis; en `{s}` cuesta ~0.3 ms por muestra, suficiente para robarle CPU al audio sin llegar a colgar nada. Es un impuesto evitable: `default` va en `{a}`.


```
{a
  default ( mesa limpia )
  22050 SR 2 CH 0 VINT
  1 SCOPE 2 STYPE
}
```

### `showlimits` — `( -- )`

Imprime por stdout un resumen de uso contra los límites del sistema (`src/limits.h`): instrucciones y anclas por bloque de contexto, slots de samples y canales activos, profundidad de pila y de loops en el instante de la llamada, y tamaños de fuente, diccionario, variables y strings. Es informativa: no toca la pila y nunca falla.

- Útil al final de un `{a}` o en `{b.}` para vigilar cuán cerca está una composición de los topes duros (bloques de 2048 instrucciones, 64 anclas, 16 slots, pila de 128).
- Los números de bloques/anclas/código reflejan la última carga; los de pila/loops, el instante de la llamada.
- `source N/65535 B` es el medidor de truncamiento: si N toca el tope, el archivo se está leyendo cortado.


### `anomlog` — `( "path" segundos -- )`

Abre (truncándolo) el archivo y registra **una línea por anomalía** hasta que transcurren `segundos` de audio, medidos en muestras desde el momento del llamado y al sample rate vigente en ese momento. Cada línea detalla: contador de muestra y tiempo en segundos, contexto (`app`, `app.`, `buf`, `buf.`, `sample`), posición en el programa (`pc`), nombre del opcode que falló, profundidad de pila y hasta los últimos 4 valores de la pila.

- **Bloque**: cualquiera; típicamente `{a}` o `{b}` para abrir la ventana de captura al recargar.
- **Corte**: con `segundos <= 0` (y cualquier string) cierra el log activo antes de tiempo. Un `anomlog` nuevo con otro activo cierra el anterior y abre el nuevo. Cierre automático al cumplirse el plazo y al salir del programa, con resumen de cantidad de líneas por stderr y al final del archivo.
- **Límites**: índice de string inválido o archivo no abrible → mensaje por stderr + anomalía (no crashea). El archivo se flushea una vez por buffer de audio: si el proceso muere por un crash duro (SIGKILL), las últimas líneas pueden perderse. El log no cambia la naturaleza no-fatal de las anomalías: el programa sigue corriendo mientras se observa.
- El contador agrupado de stderr (`[anomalias] N en los ultimos ~2s`) sigue funcionando en paralelo: ese es la alarma, `anomlog` es el microscopio.
- **Toca el filesystem sin sandbox**: ver "Modelo de confianza" en el README.

```
{a
  default
  "/dev/shm/anom.log" 8 anomlog
}
{s
  1 0 / 0 OUT          ( division por cero: una anomalia por muestra, todas al log )
}
```

El archivo resultante se ve así:

```
# log de anomalias: 8.0 segundos de audio, sr=8000, desde t=0
t:256 (0.032s) ctx:sample pc:3 op:/ sp:2 pila:[ 1 0 ]
t:257 (0.032s) ctx:sample pc:3 op:/ sp:2 pila:[ 1 0 ]
# cierre: 62208 anomalias registradas
```

Con `ctx` sabés en qué bloque ocurre; con `pc` y `op`, exactamente qué palabra falla; con `pila`, con qué valores (el divisor 0, el canal inexistente, el índice inválido).

### `STRICT` — `( v -- )`

Modo estricto: 0 = off (default), ≠0 = on. Con strict activo, **la primera anomalía detiene la sesión** (salida limpia: corre `{a.}`, parchea grabaciones, cierra scope y audio) e imprime por stderr un reporte completo: la línea detallada de la anomalía (t, contexto, pc, opcode, pila), el estado de la sesión (sr/ch/buf/vint/mute/scope/stype/delay) y la pila de loops activa con índice, fin, paso y pc de apertura de cada `@[`.

- **Uso típico**: `1 STRICT` al principio del `{a}` mientras probás un fragmento nuevo; `0 STRICT` (o simplemente no ponerlo) en sesiones terminadas, donde la anomalía como no-op es un recurso legítimo del lenguaje.
- **Con el watchdog**: un loop que agota su presupuesto de instrucciones suma anomalía, así que strict lo detiene con el reporte apuntando al bucle: entre los dos, ni cuelgues ni bugs silenciosos.
- Las anomalías que ocurran durante el shutdown no reimprimen el reporte: solo se cuentan y loguean.
- Los cambios de estado se loguean por stderr.

### `BCASC` — `( v -- )` · getter `bcasc` — `( -- v )`

Modo de bloques en cascada: 0 = off (default: cada carga reemplaza los cinco bloques), ≠0 = on. Persiste entre cargas hasta que algo lo cambie; `default` lo pone en 0; `--bcasc` en la línea de comandos lo deja activo desde el arranque. **Latencia: afecta a la próxima carga, no a la que ejecuta la palabra** (la compilación ocurre antes de que corra `{a}`).

Con BCASC activo, cada carga reemplaza **solo los bloques que el archivo define**; el resto conserva el Program compilado en su propia carga:

| En el archivo                  | Efecto con BCASC on                    |
|--------------------------------|----------------------------------------|
| `{x ... }` con contenido       | reemplaza el bloque con ese código     |
| `{x }` vacío                   | definido vacío: **silencia** el bloque |
| `{x` ausente                   | retiene el Program anterior            |
| código suelto fuera de bloques | define `{s}` (el bloque implícito)     |

- **El ciclo `{a.}`/`{a}` no cambia**: cada recarga corre el `{a.}` saliente y el `{a}` entrante (aunque el entrante sea retenido). Los efectos secundarios de un `{a}` retenido (recstart, smpload, setters) se re-ejecutan en cada recarga cascada.
- **Las variables viven, las funciones no**: un bloque retenido lee el valor *actual* de las variables (`=:`), porque los slots son compartidos; pero conserva el cuerpo de las funciones y alias (`: ;`, `:=`) que inlineó en su propia carga. Si una carga cascada redefine alguna y algo queda retenido, un aviso por stderr lista los nombres. Para adoptar una función nueva: reenviá el bloque consumidor, o definí función y uso en la misma capa.
- **Diagnósticos y `--checkscript`** juzgan solo lo que el archivo define; los bloques retenidos no se revalidan.
- **`...` y truncamiento**: lo que queda detrás del corte no cuenta como definido y se retiene (en modo normal se silenciaría).
- La línea de carga informa qué bloques quedaron retenidos: `| bcasc | retenidos: {b} {s}`.

---

## Apéndice A — Valores por defecto

| Parámetro               | Default                                                                                 | Palabra                        |
|-------------------------|-----------------------------------------------------------------------------------------|--------------------------------|
| Sample rate             | 8000 Hz                                                                                 | `SR`                           |
| Canales                 | 1                                                                                       | `CH`                           |
| Buffer                  | 256 frames                                                                              | `BUF`                          |
| Rango de salida         | 0 .. 255                                                                                | `VMIN` `VMAX`                  |
| Aritmética              | entera (wrap int32)                                                                     | `VINT`                         |
| DC blocker              | activo                                                                                  | `DCB`                          |
| Saturación              | off (clamp duro)                                                                        | `SAT`                          |
| Pasa-bajos              | off                                                                                     | `LP`                           |
| Mute                    | off (audio activo)                                                                      | `MUTE`                         |
| Scope                   | apagado                                                                                 | `SCOPE`                        |
| Zoom del scope          | 100 ms                                                                                  | `SZOOM`                        |
| Tipo de scope           | 0 (columnas)                                                                            | `STYPE`                        |
| FFT del espectrograma   | 512                                                                                     | `SFFT`                         |
| Resolución del scope    | 1 px/columna                                                                            | `SRES`                         |
| Color de fondo          | negro                                                                                   | `SBGCOLOR`                     |
| Posición de ventana     | centrada                                                                                | `SWINX` `SWINY`                |
| Tamaño de ventana       | 800 × 400                                                                               | `SWINW` `SWINH`                |
| Fullscreen              | off                                                                                     | `SFULLSCR`                     |
| Decoración de ventana   | on                                                                                      | `SDECOR`                       |
| Siempre arriba          | off                                                                                     | `SWINTOP`                      |
| Color / trazo           | blanco / 1 px (todos los canales)                                                       | `SCOLOR` `SSTROKE`             |
| Delay                   | off, 200 ms, feed 50, mix 50                                                            | `DELAY` `DTIME` `DFEED` `DMIX` |
| Filtros del feedback    | off                                                                                     | `DFLO` `DFHI`                  |
| Crossfade de samples    | 0                                                                                       | `SMPFADE`                      |
| Modo estricto           | off                                                                                     | `STRICT`                       |
| Modo bloques en cascada | off                                                                                     | `BCASC` / `--bcasc`            |
| Cuenta de `<<`/`>>`     | VINT: máscara `& 31` (tradición JS/bytebeat); float: clamp 0..63 con saturación en ≥ 32 | `OP_LSHIFT`/`OP_RSHIFT`        |


## Apéndice B — Límites duros

| Límite                                   | Valor                                    | Dónde                            |
|------------------------------------------|------------------------------------------|----------------------------------|
| Instrucciones por programa               | 2048                                     | `MAX_INST`                       |
| Profundidad de pila                      | 128                                      | `STACK_SIZE`                     |
| Fuente `.p`                              | 65536 bytes                              | `RPN_BUF_SIZE`                   |
| Tokens crudos / expandidos               | 8192 / 4096                              | parser                           |
| Definiciones (funciones+alias+variables) | 2048                                     | `MAX_WORDS`                      |
| Tokens por cuerpo de función             | 256                                      | `MAX_FUNC_BODY_TOKENS`           |
| Anidamiento de loops                     | 16                                       | `MAX_LOOP_DEPTH`                 |
| Anclas por programa                      | 64                                       | `MAX_ANCHORS`                    |
| Canales de audio                         | 16                                       | `MAX_CHANNELS`                   |
| Slots de samples                         | 16                                       | `MAX_SMP_SLOTS`                  |
| Strings en strtab                        | 256 entradas × 255 chars                 | `MAX_STRINGS` / `MAX_STRING_LEN` |
| Largo de token                           | 31 chars                                 | `MAX_TOKEN_LEN`                  |
| Buffer de delay                          | 65536 muestras/canal                     | `DELAY_BUF_SIZE`                 |
| Ring del scope                           | 65536 muestras/canal                     | `SCOPE_RING_SIZE`                |
| FFT máxima                               | 2048                                     | `SPEC_MAX_FFT`                   |
| Diagnósticos por carga                   | 12                                       | `MAX_DIAG_MSGS`                  |
| Instrucciones por ejecución de bloque    | 100k (`{s}`) / 500k (`{b}`) / 5M (`{a}`) | watchdog de `run_program`        |
| Captura de `smplive`                     | 65536 muestras (clamp)                   | `SCOPE_RING_SIZE`                |
| Profundidad de llamadas (`,_`)           | 16 niveles                               | `MAX_CALL_DEPTH`                 |
