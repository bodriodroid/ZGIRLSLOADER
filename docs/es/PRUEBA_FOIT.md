# Prueba del parser .foit + .art + .art-pt (ZDroid-Engine)

Esto es una **prueba**: lee tus archivos de personaje (los que ya extrajiste de tu copia del juego) y comprueba que el
motor entiende el `.foit` completo (frames, pivotes, hit/hurt/push boxes, estados, transiciones, proyectiles) y que
todas las imágenes de `.art` / `.art-pt` se decodifican. No incluye datos del juego y no modifica tus archivos.

## Cómo correrla (macOS / Linux, sin SDL)

```
tools/run_foit_test.sh /ruta/a/tu/carpeta/con/foit_art_artpt  WalkFwd  ./foit_out
```
La carpeta puede tener varios personajes (`annie.foit`, `annie.art`, `annie.art-pt`, `annie_1p.dds`, `painwheel.foit`, ...).
Si falta el `.art-pt` o la paleta `_1p.dds` el programa lo dice y sigue (sin paleta usa colores de relleno).

Salida esperada por personaje, por ejemplo Annie:
```
annie          OK  'Annie' | 1549 frames (art 851, art-pt 688, none 10) | 1539 images | 582 states | 37 children ... consumed exactly
               files: .art yes | .art-pt yes | palette yes
               images decodable: .art 850 ok / 0 bad | .art-pt 689 ok / 0 bad | file missing for 0
```
Si dice `PARSE FAILED` o `bad > 0`, mándame `foit_out/report.txt`.

## Otras opciones de la herramienta (`build_foit/zdroid_foit`)

```
zdroid_foit annie.foit --states                       # lista de estados: id, nombre, nodos, ticks, siguiente, transiciones
zdroid_foit annie.foit --frames 1,137 --out out/      # PNG de un frame con pivote (cruz magenta) y cajas (rojo hit, verde hurt, azul push)
zdroid_foit annie.foit --state s.WP --chains --out out/   # tira de PNG con los frames que reproduce un estado + su script
```

## Pruebas unitarias (opcional, necesita SDL2 para compilar todo)

```
cmake -S . -B build -G Ninja -DZDROID_SDL2_SOURCE_DIR=../SDL -DZDROID_BUILD_TESTS=ON
ZDROID_TEST_CHAR=/ruta/con/annie ZDROID_TEST_FOITS=/ruta/con/todos/los/foit build/zdroid_tests
```
Cada `.foit` debe consumirse hasta el último byte; 20 de 20 lo hacen con los archivos de la versión PC.
