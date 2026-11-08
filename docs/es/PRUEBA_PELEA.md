# Prueba de pelea (ZDroid-Engine, modo pelea ya activado)

Esto es una **prueba**: la pelea ahora se juega con los datos de **tu** `annie.foit` (animaciones, hit/hurt/push boxes,
daño, hitstop, hitstun, velocidad de caminar/dash/salto). No incluye ningún dato del juego: todo se lee de tus archivos.

Desde la build Nov-06 la pantalla de pelea corre el runtime de los `.foit` completo (`foit_duel`, flujo de rondas dentro del mundo: entrada, TAKE N, ACTION!, K.O., siguiente ronda); si algo se ve mal, `ZDROID_PLAY_OLDDUEL=1` vuelve al duelo antiguo. El texto de abajo describe el duelo antiguo (`zdroid_duel`).

Lo que hay hecho: 60 ticks por segundo fijos, animaciones del `.foit` a su duración real, caja de golpe contra caja de
vulnerabilidad, bloqueo (agachado/de pie, alto/bajo), lanzamientos al aire, caída, levantarse, KO y pose de victoria,
combos con daño escalado, medidor, dash (doble toque), salto neutro/adelante/atrás, 6 normales de pie / agachado / aire.

Lo que **todavía no** está (sale aproximado o no sale): especiales y supers (faltan las tablas de comandos), agarres,
proyectiles, poses de reacción exactas, air dash / doble salto. Los demás personajes necesitan su `.art-pt` y `_1p.dds`.

## 1. Compilar (macOS M3 / Linux)

```
git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git ../SDL
cmake -S . -B build -G Ninja -DZDROID_SDL2_SOURCE_DIR=../SDL -DCMAKE_BUILD_TYPE=Release
cmake --build build
```
(`brew install cmake ninja` si no los tienes.) El modo pelea ya viene activado; `-DZDROID_FIGHT_MODE=OFF` lo esconde.

## 2. Pelear

Monta TODAS las carpetas que tengas de tu copia (el motor busca cada archivo por su nombre dentro de todas):
datos/core, el UI pack, la carpeta con `annie.foit` + `annie.art` + `annie.art-pt` + `annie_1p.dds`, los `.lvl`
(stages) y las texturas (`.dds`):

```
./build/zdroid_viewer /ruta/datos /ruta/core /ruta/ui /ruta/annie /ruta/stages /ruta/textures
```
En el menú: Training Room (o Local Versus / CPU Quick Match) -> elige personaje -> FIGHT!
Con solo Annie montada los dos jugadores son Annie.

| tecla | acción |
|-------|--------|
| W A S D | saltar / izquierda / agacharse / derecha (doble toque A o D = dash) |
| J K L | puñetazo ligero / medio / fuerte |
| U I O | patada ligera / media / fuerte |
| flechas | también mueven a P1 mientras P2 sea CPU |
| Enter / Espacio / R | reiniciar round |
| **F1** | ver cajas: rojo = golpe, verde = vulnerable, azul = empuje |

Sin teclado (Android): botones táctiles en pantalla.

Demo sin tocar nada (los dos CPU) y cajas puestas: `ZDROID_PLAY_DEMO=1 ZDROID_PLAY_BOXES=1 ./build/zdroid_viewer ...`

## 3. Pelea sin ventana (comprobar números)

```
./build/zdroid_duel /ruta/annie/annie.foit --p1 "10:,1:LP" --gap 180 --trace
```
Botones: `U D L R LP MP HP LK MK HK` unidos con `+`, formato `TICKS:BOTONES,...`. Imprime estado, frame, posición y vida
por tick, y comprueba que guardar/cargar el estado (rollback) nunca diverge.

## 4. Pruebas automáticas

```
cmake -S . -B build -G Ninja -DZDROID_SDL2_SOURCE_DIR=../SDL -DZDROID_BUILD_TESTS=ON
ZDROID_TEST_CHAR=/ruta/annie ZDROID_TEST_FOITS=/ruta/con/todos/los/foit build/zdroid_tests
```
Esperado: `0 failed`. Si algo falla o la pelea se ve rara, mándame la captura y la salida de la terminal.
