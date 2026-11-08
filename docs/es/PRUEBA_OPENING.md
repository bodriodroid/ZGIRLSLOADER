# Prueba: Loading, Opening (AttractMode) y Selector de personajes

Necesita tus propios archivos del juego montados (nada del juego va en este zip):

- `ui/Win/*.gbs`, `ui/Win/Textures/**` (incluye `Textures/AttractMode`, `Textures/charselect`), `ui/Win/Fonts/**`, `ui/Win/UISound/**`
- `loading_<idioma>.dds` + `loading_<idioma>.spr.msb` (pantalla de carga)
- Música (de `data01/music-win/music`, convertida a .wav, en la carpeta `music`): `menu_intro/menu_loop`, `select_intro/select_loop`
  y, para el Opening, `sg_attract_c.wav` + `sg_attract_c_loop.wav` (sin ellos el Opening va en silencio)
- Arte de Historia (`data01/story_art/story_art`: `Filia/...`, `backgrounds/...`) para las escenas del modo Historia (en preparación)

Orden al abrir: aviso legal -> **Loading** (arte de líneas arriba, "LOADING..." y la bomba abajo a la derecha) -> **Opening** (17 tomas
de `AttractMode.gbs`, cualquier toque/tecla o Atrás lo salta) -> título -> menú.

Single Player -> **Story Mode** (y Arcade / Versus / Training) abren el **Selector de personajes**. En Historia no se elige escenario ni
segundo personaje; Double y Valentine salen bloqueados ("LOCKED!") hasta que la historia los desbloquee.

Capturas sin pantalla (desktop): `SDL_VIDEODRIVER=dummy zdroid_viewer <carpetas…> --smoke out.png --lang es --dt 17 --smoke-frames 4`
con `--screen splash` (Loading), `--screen attract --attract-shot 3 --attract-tick 100` (Opening) o `--screen charselect`.

Hay que reconstruir el APK para que lleve estos cambios.
