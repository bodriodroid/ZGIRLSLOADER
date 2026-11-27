# ZGirls Loader / ZGirlsMobile - cambios sobre ZDroid-Engine fase 1

Nada de esto se ha compilado (a pedido). Solo se hicieron revisiones estáticas: nombres, XML, rutas del paquete Java y referencias.

## Nombre
| dónde | antes | ahora |
|---|---|---|
| Proyecto CMake (escritorio) | `ZDroidEngine` | `ZGirlsLoader` (motor sigue siendo ZDroid-Engine) |
| Título de ventana / barra superior (escritorio) | SkullGirls Loader | **ZGirls Loader** |
| Título de ventana / barra superior (Android) | SkullGirls Loader | **ZGirlsMobile** |
| Nombre de la app en Android (`strings.xml`) | SkullGirls Loader | **ZGirlsMobile** |
| Paquete Android (`applicationId` / `namespace`) | `org.bodriodroid.sgloader` | `org.bodriodroid.zgirlsmobile` |
| Activity | `SGLActivity` | `ZGirlsActivity` (`java/org/bodriodroid/zgirlsmobile/`) |
| Proyecto Gradle | (sin nombre) | `ZGirlsMobile` |
| Artefacto de GitHub Actions | `sgloader-debug-apk` | `zgirlsmobile-debug-apk` |
| Aviso legal (`dis.2` en `menu.cpp` y `docs/DISCLAIMER.md`) | "Skullgirls Loader es un cargador..." | "ZGirls Loader es un cargador..." |
| versionName | 0.3 | 0.4-zgirls |

Al cambiar el paquete, Android la instala como **otra app** (no actualiza la vieja SkullGirls Loader). La carpeta por
defecto de datos pasa a ser `/sdcard/Android/data/org.bodriodroid.zgirlsmobile/files`; si ya usabas una ruta propia,
escríbela otra vez en Home.

La carpeta interna de ajustes ahora es `SDL_GetPrefPath("bodriodroid", "zgirls")` (ver `src/app/app_paths.cpp`). Al abrir por
primera vez copia lo que hubiera en la carpeta vieja (`larpbase/sg_loader`: `dataroot.txt`, `touch_layout.txt`,
`menu_settings.txt`, `zgirls_flags.txt`, `zgirls_progress.txt`), así que no se pierde nada.

## Parche "solo Visual Novel" (esta entrega)
- Sprites: arreglo de las barras negras (bit 6 del tag = filas impares de tono interpolado), en `art.cpp` y `foit.cpp`.
- Quitado el botón PLAY / la pestaña Play de la barra superior.
- Selección de personaje: los 6 botones (LP/MP/HP/LK/MK/HK) y sus círculos quedan ocultos; solo las flechas de nivel.
- Marca: Larpbase -> bodriodroid (paquete Android, carpeta de ajustes, README).
- **`.savini`**: un solo archivo `zgirls.savini` en la carpeta de ajustes, con secciones `[options]` (Help & Options),
  `[progression]` (mismas claves y letras I/C/L/U/N que ProgressionInit.txt), `[earned]` (contadores y cadenas de dígitos,
  p. ej. `FiliaWins`) y `[story]` (`pos.filia`, `done.filia`). Guardado atómico. Si existe ProgressionInit.txt en tus datos
  se usa para rellenar las claves que falten (no se incluye en el zip). Importa una vez los archivos viejos
  `menu_settings.txt` y `zgirls_progress.txt`. Código: `src/core/savini.*`, `src/app/zg_progress.*`, ajustes en `menu.cpp`.
- **Pantalla vieja "PLAY - CHOOSE YOUR FIGHTERS" eliminada.** Arcade / Quick Match / Local / Practice usan el selector
  original del juego (Charselect.gbs, con stage, colores y mods/pal) y al confirmar el combate arranca directo; al salir del
  combate se vuelve al menú.
- **Modo Historia (Visual Novel) cableado:** Story -> selector original (un personaje, sin stage) -> línea de tiempo ->
  cutscenes -> combate -> siguiente nodo, con progreso en el `.savini` y desbloqueo de Double/Valentine.
- **Voces del Story:** `story_vo/lang_<en|jp>/<clave>_<EN|JP>.wav` (reglas del exe en `docs/RE_NOTES.md`). Sin archivos, la línea
  queda muda.
- **Pendiente:** música de las cutscenes (`Msg: MUSIC_xx`), retratos THs, Galería, mods (Ahead of the Curve, después de la VN).
  Los mods (Ahead of the Curve) van después de la Visual Novel.

## Logo
`android/branding/logo_full.png` es el logo nuevo tal cual. A partir de él se regeneraron:
`android/branding/logo_1024.png`, `res/drawable-nodpi/ic_launcher_foreground.png` (icono adaptable, dentro de la zona
segura) y todos los `mipmap-*/ic_launcher.png` + `ic_launcher_round.png` (mdpi a xxxhdpi). Fondo del icono: blanco.

## Lo que no estaba activado
- **Modo pelea**: en el APK queda siempre activo (`zd_features.h` lo fuerza con `__ANDROID__` y
  `android/app/CMakeLists.txt` fuerza `ZDROID_FIGHT_MODE=ON`). En escritorio sigue activo por defecto.
- **Opciones de depuración que en Android no se podían usar** (en PC eran variables de entorno o F1):
  crea `zgirls_flags.txt` en `/sdcard/Android/data/org.bodriodroid.zgirlsmobile/files/` con una palabra por línea:
  - `boxes` - ver cajas de golpe (rojo), vulnerables (verde) y empuje (azul) desde el inicio del combate
  - `demo`  - CPU contra CPU
  - `pad`   - mostrar siempre el pad táctil
  - `huddemo` - demo del HUD
  Se leen al empezar cada combate. Las variables `ZDROID_PLAY_*` siguen funcionando en escritorio.

## Lo que sigue sin activar (y por qué)
- Download Content, Achievements, Leaderboards y juego en línea: dependen de los servicios en línea del juego original; siguen atenuados.
- Galería de Extras, modo Historia completo, especiales/supers, agarres, proyectiles: aún no están programados en el motor
  (no es un interruptor apagado, falta el código). Ver `docs/RE_NOTES.md`.
- DepotDownloader para Android: receta sin compilar (`tools/build_depotdownloader_android.sh`).

## Datos del juego
El APK y este zip **no** llevan archivos del juego (ni `SkullGirls.exe`, ni `core/`, ni `ui/Win`). Se leen en tiempo de
ejecución desde la carpeta que elijas en el teléfono, igual que antes.

## Cómo sacar la APK cuando quieras compilar
Igual que en `android/README.md`: GitHub Actions (*android-apk -> Run workflow*, artefacto `zgirlsmobile-debug-apk`) o
`cd android && ./gradlew -PsdlDir=/ruta/a/SDL assembleDebug`.

## Ronda 19-oct (selector, historia, mods)
- Selector original: se puede elegir el 2.º personaje (pasada de JUGADOR 2); tamaño de equipo fijado en 1 (desarrollo).
- Historia: la cinemática (_LEGACY) se reproduce completa hasta su primer corte antes de que entren texto y voces.
- Historia: la caja de diálogo estaba "hundida" (texto en y=650..755 de 720). Subida 56 px (`kDialogueLift`) y letra auto-ajustada si el texto es largo.
- `mods/pal/<Personaje>/`: carpetas por personaje, los .pal sueltos se ordenan solos (`organize_pal = 0` en mods.ini lo apaga).
- `kUnlockAllForTesting = true` (zd_features.h): todo desbloqueado para probar. Ponerlo en false para el juego final.
- Browse -> botón **Mods** (abajo a la derecha): pestañas Packs / Mods / Stages / Palettes (por personaje) / Info, interruptor ON/OFF por carpeta (escribe `mods/mods.ini` `disabled =` y recarga). Código: `modsdir::Report::items`, `modsdir::setItemEnabled()`, `drawMods()` en zdroid_viewer.cpp. Smoke: `--mods-view`.
- Verificado en pantalla: caja de diálogo completa (marco entero, 3 líneas) y la cinemática sale antes de voz + subtítulo.
- Voces: además de la ruta exacta (`story_vo/lang_xx/<clave>_EN|JP.wav`) se busca por nombre "flexible" (solo letras y dígitos, sin importar mayúsculas, carpeta, guiones bajos ni sufijo). Las claves con doble espacio (`Beowulf  Post-Intro 400`) se normalizan a un espacio.
- Umbrella: los `{hn01}`..`{hn13}` (ruidos del sombrero, no están en ningún loc: los expande el .exe) muestran un ruido de relleno en vez del token; la voz se busca por el mismo token (`hn05`).
- Auditoría: botón **CHECK** en la pantalla de Story (o `--story-audit` en el smoke) recorre los 352 nodos de las 18 historias sin jugarlas: scripts, escenas, vistas, animaciones, imágenes, retratos, claves de texto, voces y escenarios de las peleas. Escribe `story_audit.txt` en la carpeta de datos de la app (problemas + líneas sin archivo de voz). Con los datos de prueba (solo arte de Filia, sin THs) no hay ningún problema de código; solo faltan imágenes que el usuario sí tiene.

## Ronda 20-oct (pelea: sombrilla, carga, menús del juego)
- **Umbrella sin sombrilla (arreglado)**: el cuerpo de Umbrella no lleva el paraguas cerrado; es un objeto hijo, `UMBRELLA_Hungern` (46 estados: idle / agachada / caminar / dash / salto x 4 niveles de hambre). Antes los hijos no se instanciaban.
  `MoveSet` ahora arma sus líneas de tiempo (`hasCompanion`, `companionFor(estadoCuerpo, "Satiated")`, `companionFrameAt`) y `play.cpp` lo dibuja detrás del cuerpo con el mismo pivote (reloj propio por jugador, se reinicia al cambiar de estado). Ataques y golpes ya traen la criatura en los frames del cuerpo. Nivel de hambre fijo en Satiated (el gestor `UMBRELLA_Manager` no está decodificado).
- **Escenario**: la imagen del escenario se coloca con su línea de suelo (~90 % de alto, medido) sobre el suelo de los luchadores y es un poco más ancha que la pantalla (parallax sin costuras). Antes se repetía en mosaico con costuras y una franja plana negra tapaba el suelo. Las sombras son 3 discos suaves que se desvanecen con la altura.
  Honesto: sin los modelos 3D ni `stages/textures/2D/stage_*.dds` (no están en el volcado) se usa la imagen del selector de personajes. Si montas `stages/textures/2D/`, se usa sola (ya buscaba ese nombre primero).
- **Pantalla de carga real** (`Phase::Loading`): al empezar un combate se parsean los dos `.foit`, las paletas y el fondo en hilos aparte (`std::async`); mientras tanto se dibuja `LoadingScreen.gbs` ("Loading Transition": negro + el corazón con calavera, animación "Loading Bug") con barra y los nombres. Después, las texturas de las poses de reposo / caminar / agacharse y de la criatura se crean a 8 ms por cuadro. Mínimo 0,9 s en pantalla. Revancha / Retry con los mismos personajes no recarga. Atrás durante la carga la cancela. `ZDROID_PLAY_SYNCLOAD=1` (o `syncload` en `zgirls_flags.txt`) carga bloqueando, para capturas.
- **Menús de la partida, dibujados con las escenas del juego** (`play_menu.cpp`):
  - Pausa (Start o Atrás): `InGame.gbs` -> `PauseOptions` (marco, placa "PLAYER 1", título con alas) + filas Resume Game / Help & Options... / Command Lists... / Leaderboards / Achievements... / Exit Game con los textos del loc (`ResumeGame`, `Options`, `Exit`...). El combate se congela. Command Lists, Leaderboards y Achievements salen atenuados (sin función todavía).
  - Fin de combate: banner `PrePostHud.gbs` -> `Round Win` ("ENCORE PERFORMANCE?" con la fuente "Big Options w Wings") + `EndMatchOptions`: Play Again / Change Characters / Change Stage (siguiente escenario) / Return to Main Menu. En Historia: Continue, o Retry / Give Up.
  - Help: `Help.gbs` -> `Help` con los textos del loc (Move, Jump, Crouch, Punches, Kicks, Tag 1/2, Ensemble 1/2, Block, Throw).
  - Give Up: tarjeta `GameOver.gbs` ("GAME OVER", animación del juego) antes de volver a la historia.
  - Entrada: teclado (arriba/abajo/Enter/Atrás) y toque (tocar una fila). Captura sin jugar: `ZDROID_PLAY_OVERLAY=pause|help|end|gameover`.
- **Escenas .gbs ya conectadas**: Main, Charselect, AttractMode, ArtGallery, InGame (HUD, Pausa, Fin), PrePostHud (banner), Help, GameOver, LoadingScreen. Siguen sin usar: Options (las opciones en partida), ThanksForPlaying, Credits/FrontCredits, FlyingText, Store, Multiplayer, Cutscene* , GenericLoader, OtterIsStupid.
- **HUD**: está dirigido por `InGame.gbs` -> `HUD`, pero las texturas `Textures/HUD/*` (marcos de barra, monedas, placas de nombre, retratos) **no están en el volcado `ui-win`** que tengo; sin ellas se ve con colores planos. Si están en otra carpeta de `sg_dump_output/unpacked`, móntala junto a `ui/Win` y se usan solas.
- Pruebas: 1585 + 488 comprobaciones, 0 fallos.

## Ronda 22-oct (galería, opciones, créditos, banner)
- **Art Gallery** (`gallery.cpp`): dibujada con `ArtGallery.gbs` (`Posterpile_BG`, rejilla 5x3 con marcos, resaltado que se desliza, visor `ArtGallery_Viewer`, cinta inferior con el nombre). Sin `ui_artgallery/` usa las imágenes de historia (`story_art/<Pers>/*.dds`), una pestaña por personaje. `ZDROID_GALLERY_VIEW=n` abre el visor en la imagen n (captura).
- **Options dentro de la partida**: Pausa -> *Help & Options...* abre `MediumOptions1P` (volúmenes general / música / efectos, rondas, tiempo, idioma, tamaño y opacidad de los botones táctiles). Se guardan en `[options]` de `SavIni` (`ZD_TOUCH_SCALE`, `ZD_TOUCH_OPACITY` son nuevas) y se aplican al instante (`Audio::setVolumes`, `TouchPad::setLayout`). Atrás: Help -> Options -> Pausa. Captura: `ZDROID_PLAY_OVERLAY=options`.
- **Créditos** (`credits.cpp`, nuevo): Extras -> Credits / Contributors (y el nodo de créditos de Story). Texto de `ui/config/Credits/credits_2021.ini` (o `credits.ini`, `credits_contrib_2013.ini`); plantillas de `Credits.gbs` (fuentes, alas de la cabecera, rejilla de 40 px, velocidad -719 px / 594 ticks). Una página con más filas que huecos se continúa (imagen lateral: mismo diseño; el resto: texto plano). Toque = pausa, dedo mantenido = avance rápido, X / Atrás = salir. Al final de una historia sigue la tarjeta `ThanksForPlaying.gbs`. Capturas: `--credits`, `--contributors`, `ZDROID_CREDITS_PAGE=n`, `ZDROID_CREDITS_THANKS=1`, `ZDROID_CREDITS_DEBUG=1`.
- **Mensaje final de los créditos**: después de la última página (65 en los datos de prueba) hay una página con el mensaje del autor del loader ("El dinero corrompe. ...  Gracias por comprar el juego."), en **Oswald** (SIL OFL 1.1). El proyecto no lee .ttf: `tools/gen_credits_font.py` rasteriza `tools/fonts/Oswald-VariableFont_wght.ttf` (peso 500, 48 px) a `src/app/credits_font.h` (198 glifos, latín + Latín-1 + comillas / guiones, codificado por rachas de ceros) y `credits.cpp` crea una textura por glifo. El rodillo se detiene 10 s en esa página (`kNoteHoldTicks`). Para cambiar el texto: constantes `kNote*` en `credits.cpp`; para otro peso / tamaño: `python3 tools/gen_credits_font.py <peso 200..700> <px>`.
- **Banner de avisos** (`play_menu.cpp`, `drawToast`): `FlyingText.gbs` -> `Banner1` ("NOT AVAILABLE IN THIS VERSION" en Command Lists / Leaderboards / Achievements). Las posiciones de las claves animadas de estas barras son la del *pivote* (esquina = posición - pivote): `NodeState::pivotAnchored` lo aplica solo a esos nodos para que la barra crezca desde el centro (en el resto de escenas no cambia nada).
- **Espacios en fuentes del juego**: no se dibuja glifo para el espacio (la fuente "Credits Names" tiene una caja de relleno); avance máximo 0,28 x alto de línea, también en `textWidth`.
- `zdroid_probe --keys` imprime ahora también `pivot(x,y)` de cada clave.
- Pruebas: 1585 + 488 comprobaciones, 0 fallos. `kUnlockAllForTesting` sigue en `true` (zd_features.h); ponerlo en `false` para el juego final.
- Sigue sin conectar: Store, Multiplayer, Cutscene*, GenericLoader, Match Intro / Showtime, HUD_Combo / TrainingMode / Movelist (Command Lists), FlyingText Notice1 / 2P Interrupt. HUD y Escenario reales necesitan `Textures/HUD/*` y `stages/textures/2D/stage_*.dds` del volcado.

## Ronda 22-oct (2): intro de ronda y K.O.
- **Intro de ronda** con `PrePostHud.gbs` (`drawRoundIntro`, `play_menu.cpp`): al empezar cada ronda, "1v1 Match Intro" (moneda "TAKE N" que gira con `coin_number_0N`, tiras de película arriba y abajo, 85 ticks) y a continuación "Action Animation" ("ACTION!", destello de damero, 89 ticks) con la pelea ya en marcha. En la ronda decisiva (los dos a una victoria, con más de 1 ronda) se usa "Showtime Animation" - **suposición mía**, el volcado trae la vista pero no cuándo se usa; si en el PC no es así, se cambia en una línea (`decider`).
  - Durante el "Take" nadie actúa (entradas a 0, los luchadores esperan 85 ticks en su intro: `duelStartRound(..., introTicks)`) y el reloj de ronda no corre (`introTimer_`). Pausa = congela también el intro. El entrenamiento no lo muestra. Sin `PrePostHud.gbs` simplemente no hay intro.
  - Las claves de estas vistas colocan los sprites por pivote (esquina = posición - pivote): todos los nodos van con `NodeState::pivotAnchored`.
  - Capturas: `/tmp/ft.sh out.png 45 --fight annie:umbrella:0` (Take), 105-130 fotogramas (Action); `ZDROID_PLAY_NOINTRO=1` (o `nointro` en zgirls_flags.txt) lo salta.
- **K.O. / TIME UP** entre rondas (`drawRoundEnd`): fuente "Big Options w Wings" con entrada escalada y "ROUND N" debajo, en lugar del texto de 8x8. Captura: `ZDROID_PLAY_OVERLAY=ko|timeup`.
- Contador de combos (`HUD_Combo`) ya estaba conectado en `play_hud.cpp`; sigue necesitando `Textures/HUD/*` (dígitos `combo_number_0N_color/black`).
- Pruebas: 1585 + 488 comprobaciones, 0 fallos.

## Ronda 23-oct: música en todas las pantallas
Antes solo sonaban el tema del menú, el del selector y el de attract; pelea, historia, créditos, final y galería iban en silencio.
- **Catálogo** (`src/core/music_lib.*`, `MusicLibrary`, con pruebas): lee `core/music.ini` (`Track / Stage / Files: <intro>, <loop>`) y busca los `.wav` por nombre en las carpetas montadas (`skull02_1.wav`, `sg_news_loop.wav`...). Además añade cualquier otro `.wav` largo (>= 200 KB) que no sea de `UISound/`, voces (`story_vo`, `lang_xx`, `*_EN/_JP`) ni efectos: se agrupa en intro + loop por su sufijo (`_intro/_loop`, `_1/_2`, o `<nombre>.wav` + `<nombre>_loop.wav`). Al cargar los datos imprime `[music] N themes known, M with files on disk (missing: ...)`; si las pistas son `.ogg/.mp3` lo dice (el mezclador solo lee `.wav`).
- **Reproductor** (`music_player.h`): un `MusicPlayer` para toda la app; `play(idx)` carga la pista (una vez) y la pone con fundido, `[music] now playing: <título> (<archivo>)` en el log al cambiar. `Audio` ahora guarda para siempre el menú y el selector y cachea solo las últimas 3 de las demás (`Audio::kMaxCached`, `findTrack`, `releaseTrack`), así toda la banda sonora no llena la RAM.
- **Pelea**: la pista de su escenario según `music.ini` (`forLevel`: `new_meridian_3d` -> Moonlit Melee, `rooftops_day_3d` -> Clear Breeze, `river_king_casino` -> Where Money Flows..., `atrium` -> Royal Canopy Waltz, etc.). Los escenarios que `music.ini` no lista (boardwalk, speakeasy, wasteland...) reciben siempre la misma pista de relleno entre las que haya; sin ninguna pista de escenario en disco suena la del selector. Se decodifica en un hilo junto a los luchadores (pantalla de carga) y arranca con el combate; Jugar otra vez no la reinicia. Los volúmenes de Options (general / música) ya la afectan.
- **Historia** (`story_mode.cpp`): los `Msg: MUSIC_xx` de los guiones. `MUSIC_STOP/OFF` calla; `MUSIC_NEWS`, `MUSIC_BOSS`, `MUSIC_ARRHYTHMIA`, `MUSIC_ATTRACT`, `MUSIC_CAT/CAT2/BLOODBATH/ANNIE` por palabra en el nombre; los números (`02`, `03`, `06`, `21`, `24`, `31`) y las letras (`K`, `R`, `W`, `U`, `O`, `Q`, `Y`, `S`, `F`) se buscan como `sg<código>_*`, `sg_<código>*`, `skull<código>_*` (de ahí que `MUSIC_02` -> `sg02_dnb_loop`, la versión de cinemática de Moonlit Melee, y `MUSIC_31` -> `sg31_*`). Si un código no tiene archivo con ese nombre, el log dice cuál y qué temas hay en disco, y sigue la música actual (`[music] story cue MUSIC_xx: no file named for it ...`). Mapa y selector de historia = tema del menú; una cinemática empieza en silencio hasta su primer cue. Las peleas de historia usan la pista del escenario.
- **Créditos y final**: el rodillo toca un tema con "credit" en el nombre (si no, "ending/final/outro", si no, el del menú); la tarjeta de Thanks for Playing del final de una historia toca "ending/final/outro/thanks/victory" (si no, el de los créditos).
- **Galería**: toca un tema "gallery" (si no, el del menú) y tiene botón **MUSIC**: un selector (jukebox) con todas las pistas del catálogo (las que tienen archivo primero; las demás salen atenuadas con "(no file)"). Arriba/Abajo o toque eligen, Enter o toque tocan, Izquierda/Derecha o PREV/NEXT cambian, STOP/PLAY, barra de progreso y barritas animadas en la pista que suena; Atrás vuelve a las imágenes y al tema de la galería. `ArtGallery.gbs` no trae reproductor propio: está dibujado con su fondo (`Posterpile_BG`) y fuentes. Captura: `ZDROID_GALLERY_JUKE=<fila>`.
- Menú: si no tiene tema propio en los datos ya no sigue sonando la última pista de una pelea.
- Ganchos de humo: `ZDROID_STORY_AUTOPLAY=1` (la historia abre el siguiente nodo sin tocar; sirve para ver los cues en el log).
- Pruebas: `zdroid_tests` 1627 (42 nuevas del catálogo), `zdroid_duel_tests` 488, `zdroid_audio_tests` 68 (caché de pistas).

### Nombres de archivo que busca (según music.ini; `.wav`, intro primero)
skull02_1/2 (New Meridian) · skull03_1/2 (Medici) · skull_maple_1/2 · skull01_1/2 (Class Notes) · skull04_1/2 (ASG) · skull06_1/2 (Innsmouth) · skull_casino_1/2 · skull05_1/2 (Catedral) · skull_boss_1/2 · skull_rooftops_1/2 · skull_nighttime_1/2 · skull_underthebridge_1/2 · skull_glasswaltz_1/2 · skull_nmc_1/2 · skull_eliza_1_long + skull_eliza_2 · sg_gehenna_1/2 · sg_arena_intro/loop · sg_emptyarena_intro/loop · sg02_dnb_loop · sg03_dnb_loop · sg06_dnb_loop · sg_news_intro/loop · sg_k1_intro/loop · sg31_intro/loop. Los temas de créditos / final / galería y los de los cues 21, 24, R, W... no están en `music.ini`: se descubren por el nombre del archivo (ver arriba).

## Ronda 24-oct: HUD y escenarios como en PC (arte del propio juego, nada dibujado a mano)
- **El HUD estaba en los atlas**: las filas de la tabla de texturas de los `.gbs` (284 bytes) terminan con `[atlas id][u0][v0][u1][v1]`. `InGame.gbs` (244 de sus 246 filas: HUD, banners de Posthud, marcos de diálogo, flechas, selectores de Options, piezas del selector) apunta a `Textures/TA_1001_1.dds` / `TA_1001_2.dds` (2048x2048, DXT5) con el rectángulo de cada pieza. `GbsTexture` guarda esos campos (`inAtlas()`, `GbsScene::textureByName/ById`) y `GameUi::texture()` recorta la región del atlas cuando no hay un `.dds` suelto con ese nombre (`loadAtlasRegion`, atlas cacheado; `hasTexture` también cuenta las piezas de atlas). Se borró el pintor procedural: todo sale de tu `ui/Win/Textures`.
- **Barras metálicas** (`P1/P2_One_Fighter`): la posición y el tamaño salen de las claves de `P1_OneFighterAnim` / `P2_OneFighterAnim` del propio `.gbs` (167,54 / 368x29 y 27,54), con `NodeState::forceBox`. Ya no tapan la moneda del retrato.
- **Retratos grandes reales**: `character_<n>_colors|lines|shade.pcx` (404x576, 8 bits RLE; `hud_art.h` / `hud_portrait.cpp`) se componen con la paleta del color elegido (`Skin::palette()`): banda = índice/5+1, tono = sombra, la capa de líneas oscurece, fondo transparente. P2 sale espejado porque el nodo trae `flipX` (ahora también en texturas "raw"). Montar `hud_new/palettized_hud_art`. BossMarie usa sus propios `name_bossmarie`, `character_symbol_bossmarie01` y `character_bossmarie_*.pcx` ("BLOODY MARIE"); se quitó el atajo bossmarie -> marie.
- **Fin de combate**: ahora es la vista "Continue Service" de `InGame.gbs` (fondo oscuro, damero, alas, glifos, tiras de película) con su animación OnActivate y la cabecera real: `posthud_cservheader_small01b` (**ENCORE PERFORMANCE?**, versus), `_small03` (**THE STORY CONTINUES**, victoria en historia/arcade) y `_small02` (**WHAT NOW, BIG SHOT?**, derrota). La cabecera va por pivote (`pivotAnchored`).
- **Escenarios** (`drawWorld`): el fondo es la imagen de todo el escenario, así que se ancla al escenario (ancho = 2 * límite * escala de cámara): crece con el zoom y se desliza con la cámara; escritorio y pizarrón de Class Notes salen grandes y el piso de papel cuadriculado llena la parte baja. La línea de suelo (~90 %) queda en el piso de los luchadores, el cuadro sigue por debajo (sin banda negra ni degradado) y se filtra en lineal. La línea roja central de la imagen (si la hay: columna roja delgada en las últimas filas) se alinea con el centro del escenario (`findCenterLine`).
- Pruebas: 1751 + 507 + 68 comprobaciones (nuevas: filas de atlas, PCX RLE, composición del retrato), 0 fallos; con tus datos reales 59213, 0 fallos. `kUnlockAllForTesting` sigue en `true`.
- Pendiente (pedido tuyo, después): música y voces del Visual Novel.

## Build 0.6 Dev_Oct-26 - Ronda 25/26-oct: resolución real, Opciones de Desarrollador, sombras, carga por etapas y fondos 2D reales
- **Resolución**: todo se dibuja con coordenadas decimales (`SDL_RenderCopyExF`), filtro lineal o pixelado por textura y sprites guardados en niveles de potencia de 2 (`spriteLevelFor`, `src/core/imgscale.*`) según el tamaño real en pantalla; los bordes transparentes se rellenan (`bleedTransparent`) para que el filtro no deje halos oscuros. Con "Resolución de render" < 100 % la partida se dibuja en una textura menor y se estira.
- **Opciones de Desarrollador** (`src/core/dev_options.*`; menú principal y pausa > Ayuda y Opciones): resolución de render (Baja/Media/Alta/Nativa), calidad de sprites, suavizado, sombras, fondo del escenario (Auto/Portada/Generado), escalado del fondo, precarga de cuadros, FPS, cajas de golpe/daño, intro de ronda, entradas e info del escenario. Se guardan en `[options]` como `ZD_DEV_*`; para pruebas, `ZDROID_DEV_<CLAVE>=<texto>`.
- **Sombras**: Apagadas / Bajas / Medias / Altas (silueta aplastada del luchador) / Juego (la misma, que se desvanece con la altura como el shader `FloorShadow` de PC: alfa 0,75 en el suelo, 0 en `ShadowDist`).
- **Carga por etapas**: primero el escenario (ya visible, con su nombre), luego los datos de los luchadores y por último los sprites, decodificados en hilos y subidos a la GPU con presupuesto de 8 ms por cuadro; la caché de texturas tiene tope en bytes y expulsa los cuadros más viejos. Las portadas del selector ya no se usan nunca como fondo (salvo la opción "Portada").
- **Fondos 2D reales** (26-oct, con tus `stages/textures/2D/*.dds`): miden 3750x2000, o sea el `StageSize` de los `.lvl`: son la vista frontal completa del escenario, 1 píxel = 1 unidad. Vienen con los **canales rotados** (el shader `LevelFlatBG` lee `.argb`: R está en alfa, G en rojo, B en verde y el azul guardado vale 255), así que sin corregirlo salen azules y medio transparentes; `unrotateArgb()` lo detecta y lo restaura (solo en carpetas `2D/`). El dibujo los coloca a escala 1:1 con el escenario, el suelo en la fila `1 - BottomClearance/alto` (0,968) y, si el panorama termina antes del borde de la pantalla, estira su última franja. El menú de reserva (sin paquete de UI) usa la misma corrección. Los NPC de estos fondos están pintados dentro de la imagen (sin animación).
- **Herramientas**: `zdroid_probe --missing` lista, por cada `.gbs`, qué texturas, sonidos y fuentes no están en disco.
- **Revisión externa**: arreglo de `level.cpp` con prueba, `<algorithm>`, textos legales, LICENSE y avisos de terceros (OFL de las fuentes), ajustes de Android (versionCode, abiFilters, firma), CI con sanitizers y fuzzing. Sin hacer: partir los archivos grandes, generar `credits_font.h` en el build, centralizar las variables de entorno.
- **Datos**: nada del juego va dentro del repo ni de los zips. Para estos fondos basta la carpeta `stages/textures/2D` (46 archivos, unos 330 MB); el resto de `stages/textures` (unos 650 MB: NPC, suelos, públicos de la arena) es para el futuro camino 3D, que además necesita las mallas `.sgm/.sga/.sgs.msb` de cada escenario.
- Pruebas: 59690 comprobaciones con tus datos reales, 0 fallos (incluye la rotación de canales); `kUnlockAllForTesting` sigue en `true`.
- Pendiente (pedido tuyo, después): música y voces del Visual Novel.
- **Nombre de las builds**: `0.6 Dev_Oct-26` = versión 0.6, con Opciones de Desarrollador, del 26 de octubre. Una build sin "Dev" sería una sin esas opciones; las siguientes se llamarán por su mes y día (`Oct-..`, `Nov-..`). Android: `versionCode 3`, `versionName 0.6-zgirls-Dev_Oct-26` (instala encima de la anterior).

## Build 0.6 Dev_Oct-27 - la update que repara varias cosas
- **Escenario equivocado / "placeholder"** (causa real): el selector de escenarios manda el id de la miniatura (`rooftops_day`, `casino`, `training_stage`, `nightmare_crest`...) pero los `.lvl` se llaman `rooftops_day_3d`, `river_king_casino`, `training_3d`, `maple_crest_mare_3d`... y la búsqueda era por nombre exacto: en unos 20 de los 31 escenarios no coincidía nada y se quedaba con el escenario 0 (o el de la pelea anterior). Ahora `resolveLevel()` (`src/core/level.*`) usa la tabla del propio juego (`stages.ini`: id de localización, archivo `.lvl` y miniatura) y, si hace falta, el nombre sin `_3d` o un fragmento que sea único. Se usa en Versus, Historia (`World ...`) y Arcade. Si un escenario no se encuentra, el log lo dice (`[play] stage '...' matches none of the N .lvl files`). Con tus datos, las 32 filas de `stages.ini` se resuelven desde sus 4 nombres.
- **Mods de escenarios**: si un mod trae un `.lvl` con el mismo nombre que uno del juego (p. ej. `arena.lvl`), solo cuenta el que gana en el Vfs (antes quedaban los dos y podía elegirse el original).
- **Random**: ya no sale `test1`, `test2` ni los escenarios marcados `ExcludeFromRandom`/`BetaOnly` en `stages.ini` (arena Evo, Gehenna, SGCS...); la escalera de Arcade tampoco. Los escenarios de mods sí pueden salir. En Historia, `World Random` elige uno al azar en vez de repetir el de la pelea anterior.
- **Filia (no atacaba, animación rota)**: sus movimientos se llaman distinto (`Jab/Strong/Fierce/Short/Fwd/RH` en vez de `WP/MP/HP/WK/MK/HK`, un solo `Jump_Start`/`JumpLand`). `moveset.cpp` acepta los dos juegos de nombres y usa ese salto/aterrizaje para las tres direcciones. Los 18 normales y el salto salen de sus datos reales (probado con una pelea Filia vs Annie con IA: ataca y conecta).
- **Boss Marie**: es un rig de arcade (sin datos de golpe, sin agacharse ni saltar); las pruebas con datos reales ya no la tratan como peleador.
- **Historia, otros personajes**: comparados con la lista de 20 personajes del juego, todos los nombres de `Fighter` de las 18 historias se resuelven a un peleador, y los 30 `World` están en `stages.ini` y ahora llegan a su `.lvl`. Lo que sigue faltando en CHECK son imágenes (`THs/...` y `story_art` de los demás personajes: 2500 retratos y 1342 imágenes que no están en los datos que me pasaste, solo las de Filia); no es de código. Si en tu cel alguna historia se ve mal, pulsa CHECK en Story y mándame `story_audit.txt`.
- **Música del Nivel de Prueba**: el Training Stage (`training_3d`) y `test1`/`test2` tocan "Hymn For The Weekend (Final Remake)" cuando en tus datos están `Hymn For The Weekend Final Remake Intro .wav` y `... Loop.wav` (intro una vez, luego el loop; cualquier carpeta montada, p. ej. `music/`; los archivos NO van en el repo ni en los zips). Los nombres se buscan sin importar mayúsculas, espacios ni signos. Además, ahora se leen **todos** los `music.ini` que haya (no solo el que gana en el Vfs): uno de un pack o mod puede dar música a cualquier escenario (`Track:` / `Stage:` / `Files:`) y gana sobre el del juego y sobre este tema.
- **WAV de 32 bits float** (lo que exporta un DAW; el loop pesa 52 MB): decodificador propio, 75 MB de RAM y 0,2 s en vez de 225 MB y 0,5 s con la conversión de SDL; remuestreo cúbico de 48 kHz a 44,1 kHz.
- **Herramientas**: `zdroid_moves <char.foit>... [--states]` muestra qué estados clave y qué normales no resuelve un personaje, y los timelines que no terminan.
- Pruebas: 59927 comprobaciones con tus datos reales + 44180 de audio, 0 fallos (también con ASan/UBSan) (nuevas: nombres clásicos de Filia, `foit` reales, id de escenario -> `.lvl`, escenarios de mods, música del nivel de prueba, WAV float). `kUnlockAllForTesting` sigue en `true`.
- **Sin hacer** (decidido por ti): leer los `.gfs` directamente y mapear la carpeta `Skullgirls/` (`data01/*.gfs`, `Salmon/`) - para las de Nov. También pendientes: música/voces del Visual Novel, NPC animados (necesitan los `.msb` 3D), líneas `Replace` de los `.lvl`.
- Android: `versionCode 4`, `versionName 0.6-zgirls-Dev_Oct-27` (instala encima de la anterior).

## Build 0.6 Dev_Oct-28 - lógica de los .foit, y por qué un escenario cae en el nivel de prueba
- **Normales leídos de la tabla del propio personaje**: cada `.foit` tiene un estado `Normals` cuya tabla de transiciones decide qué ataque sale (el orden es la prioridad). `hubNormals()` la lee (botón = el único `0` de `t[0..5]`, dirección = `b0`, postura = prefijo `s.`/`c.`/`j.`) y `MoveSet` la usa para lo que los nombres no dan. Resultado: **Black Dahlia y Marie ya tienen sus 18 normales** (usan `LP/LK`, un tercer juego de nombres) y **Peacock su MK aéreo** (`j.MK_N/F/B`). Los 18 peleadores salen 18/18 y la tabla y los nombres coinciden en todos.
- **Normales con dirección** (los del propio juego, no inventados): adelante (`s.F+MP`/`s.F+HP` de Annie, `s.F+HP` de Cerebella, Squigly, Painwheel `s.F+HK`, Parasoul, Umbrella), atrás (`s.B+HK`, `j.B+WK` de Parasoul, `j.MK_B` de Peacock) y abajo en el aire (`j.D+HK` Beowulf, `j.D+MP` Cerebella, `j.D+MK` Parasoul). En el duelo salen al mantener esa dirección con el botón; el resto de personajes no cambia.
- **Estados "router"** (un tick que salta al golpe de verdad: `s.HP` -> `s.HP_NoChair` de Beowulf, `s.MP`/`s.HK` de Ms. Fortune, `s.HP` de Painwheel): el golpe que sí se mueve recibe los mismos datos de golpe provisionales que los demás.
- **Escenario equivocado / "Test Level"**: con tus datos completos los 31 escenarios del selector resuelven bien (nivel y fondo; herramienta nueva `zdroid_stages <carpetas>`), así que en el cel pasa por otra causa: no se ve ningún `.lvl`, el id no coincide con ninguno, o el `.lvl` sí está pero falta su imagen de fondo (`stages/textures/2D/*.dds`). Ahora el juego **lo dice**: en la pantalla de carga y los primeros 9 s de la pelea aparece una línea roja con el motivo (`no .lvl in the mounted data`, `stage 'x' matches no .lvl (N)`, `no backdrop picture for <nivel> (wants <archivo>)`) y queda en el log (`[play] stage problem: ...`). Y si el id no resuelve ya no se queda en el primer nivel de la lista (que podía ser `test1`): sale un escenario real al azar.
- **Herramientas**: `zdroid_stages <carpetas de datos>` (31 ids del selector -> `.lvl` -> imagen de fondo, marca `NO LEVEL` / `NO BACKDROP`), `zdroid_cmds <char.foit> <estado | --hubs | --to estado>` (transiciones y pasos de comando en crudo), `zdroid_moves` ahora lista las variantes de dirección.
- Pruebas: 70711 comprobaciones con tus datos reales y los 20 `.foit`, 0 fallos (también con ASan/UBSan) (nuevas: tabla `Normals` sintética y real).
- Android: `versionCode 5`, `versionName 0.6-zgirls-Dev_Oct-28` (instala encima de la anterior).

## Build 0.6 Dev_Nov-06 - la pelea corre con los datos del propio .foit (runtime conectado al GGPO y al render)
- **Qué cambió**: hasta ahora el runtime de los `.foit` (`foit_rt`) solo corría en el simulador. Ahora la pantalla de pelea (`PlayScreen`) lo usa de verdad: `FoitDuel` (`src/core/foit_duel.*`) mete el mundo del runtime dentro de las mismas sesiones tipo GGPO de siempre (`LocalSession`, `SyncTestSession`, un futuro backend de red). Guardar/cargar = el mundo completo, así que un rollback restaura la pelea real y no una copia; el checksum cubre el mundo entero.
- **Flujo de ronda dentro del mundo**: entrada de los luchadores -> tarjeta "TAKE N" -> "ACTION!" -> pelea -> K.O. / tiempo -> poses -> siguiente ronda -> fin del match. La pantalla solo lee la fase y elige la tarjeta (PrePostHud). Nadie puede actuar fuera de la pelea; un botón de ataque *pulsado* durante la entrada la corta. Entrenamiento: sin reloj y nadie muere. Regla supuesta (no confirmada): si una ronda decisiva termina en empate se juega una más (solo si el match lo pide).
- **Render**: cada objeto del mundo (los dos cuerpos y lo que generan: proyectiles, efectos, ayudantes) se dibuja con el offset de su frame y sus slots de escala/alfa; un luchador invisible (entrada) ya no deja su sombra. F1 muestra las cajas reales (empuje, vulnerable, golpe).
- **Cámara**: cuando alguien sale volando, el piso baja un poco y, si sigue subiendo, la cámara se aleja en vez de sacar los pies del otro luchador de la pantalla (provisional, hasta leer la cámara del juego).
- **Si algo se ve mal**: `ZDROID_PLAY_OLDDUEL=1` (o `oldduel` en el archivo de flags) vuelve al duelo antiguo por tablas de movimientos. El log dice cuál corre: `[play] fight engine: the .foit data itself (foit_duel)`.
- **Herramienta**: `zdroid_foitduel A.foit [B.foit] --intro --ready 85 --hold 110 --synctest --rollback 700,200` corre un match entero por `SyncTestSession` y un rollback profundo (los checksums se repiten).
- Pruebas: `testFoitDuelReal` (con `ZDROID_TEST_FOIT=<carpeta con annie.foit>`): conversión de input, synctest, rollback profundo, match completo en orden, entrenamiento, empate y skip de la entrada. 2604 comprobaciones, 0 fallos, también con ASan. Sigue rojo `zdroid_overlay_tests` (DSEG, en pausa).
- **Sin hacer**: equipos / tag / asistencias (el código está pero apagado y sin probar), cámara y config de escenario del juego, movimiento raíz, op 117 y tabla de ids de personaje.
- Android: `versionCode 6`, `versionName 0.6-zgirls-Dev_Nov-06` (instala encima de la anterior).

## Build 0.7 Dev_Nov-09 - PAFX (Paradox Archive FX) y los efectos de Core reconstruidos para Android
- **CI de Android**: el paso "Setup Android SDK" fallaba porque `android-actions/setup-android@v3` instala por defecto el paquete `tools`
  (ya no existe) y `sdkmanager` salía con error justo después de aceptar las licencias. Ahora solo pide `platform-tools`. (Tu edición ya
  apuntaba a eso pero tenía las claves mal indentadas, lo que invalidaba todo el YAML; quedó con tu idea y el YAML válido.)
- **Core**: el zip nuevo es idéntico al Core que ya estaba montado (882 archivos, 0 diferencias). Trae los shaders: `.fx` (HLSL), `glsl12`
  (GLSL de escritorio) y `pssl` (PS4, binarios). Los `.fx` sirven de referencia de comportamiento (notas en `docs/RE_NOTES.md`).
- **PAFX** = *Paradox Archive FX*: el paquete de efectos de render de Android; no es un `.gfs` ni lleva arte ni audio. Lo arma cada jugador
  **en su dispositivo, desde su propio Core**, y lo sella con una clave por instalación que se genera allí y no sale de allí. Ni el repo,
  ni los zips públicos, ni la APK llevan un `.pafx`. Formato en `docs/PAFX.md` (cabecera de 64 bytes + tabla y entradas selladas con
  ChaCha20-Poly1305, implementado sin librerías externas y comprobado contra los vectores del RFC 8439 y contra otra implementación).
- **El código es nuestro, los números son del jugador**: los 6 programas GLSL ES (`src/fx/fx_glsl.cpp`) están escritos desde cero y no
  llevan ningún valor del juego; los valores (colores de los flashes, contraste/brillo/saturación, sombra del super, colores de la muerte de
  Jojos) los lee `zdroid_pafx pack` de tu `core/shaders/*.fx` y viajan dentro del pack sellado (`fx/tuning.ini`). Sin pack el motor usa una
  apariencia neutra propia.
- **Efectos (v1)**: `sprite_palette` (sprites desde los planos de índice + paleta, igual que las tablas de CPU a +-1/255, con rotación de
  tono, alfa, flash y sombra de super), `super_outline`, `floor_shadow`, `flat_tint` (escenarios / tarjetas de UI), `death_portrait` y
  `death_background`. Matemática de referencia en C++ (`src/fx/fx_math.*`, también la ruta de CPU).
- **Verificación**: `tools/fx_gles_check.py` dibuja cada programa con un GLES real (Mesa llvmpipe, sin GPU) y lo compara píxel a píxel con la
  referencia: diferencia máxima **1/255** en los 11 casos, con números neutros y con los leídos de tu Core. Nuevo job de CI `fx-gles`.
  `tests/test_fx.cpp` (341 comprobaciones: cifrado, contenedor con clave equivocada / daño / truncado / entradas cambiadas, extracción de
  números, maths) limpio con ASan/UBSan; el fuzz ahora también alimenta el lector de PAFX.
- **Herramienta**: `zdroid_pafx pack | info | dump | gen-check`. Con tu Core: 4 grupos de números leídos, 97 archivos de shader con huella.
- **Sin hacer (y dicho claro)**: el juego en vivo todavía dibuja con `SDL_Renderer`, que no admite programas propios; falta el pase GLES
  pequeño (vaciar el lote de SDL, dibujar los sprites con `sprite_palette`, restaurar el estado), cargar el pack desde la carpeta privada
  de la app y un botón "crear pack de efectos" junto al selector de datos. Tampoco están aún la iluminación con normales (el decodificador de
  `.art` no expone el plano de normales), el campo "chowder", las máscaras de tono por fila de paleta ni los retratos palettizados de Otter.
- Android: `versionCode 11`, `versionName 0.7-zgirls-Dev_Nov-09`.

## Build 0.6 Dev_Nov-07d - CCRA: los anillos del super salen del propio .gbs
- **Nodos de arco (`CCRA`)** decodificados: segmentos, radio de la línea central, grosor, color, barrido, ángulo inicial y sentido; y sus
  claves de animación (`TLRA`: radio, grosor, color, barrido, inicio). Antes la palabra de color de esas claves se leía como radio.
  Módulo nuevo `src/core/gbs_arc.*` (geometría sin SDL, con pruebas) y `GameUi` los dibuja como cualquier otro nodo.
- **HUD**: el anillo del super ya no se dibuja a mano (155° fijos, colores inventados): se muestran el anillo del stock que tienes y el que se
  está llenando, y el llenado lo hace la animación `P?_SuperBar<N>_Anim` puesta en el tick que corresponde al valor del medidor (como hace
  el juego). Resultado: colores, grosor y sentido reales; el anillo en llenado crece desde el extremo donde empieza, no desde la insignia.
  El destello `SuperBarFlash` (alfa 0 en reposo) vuelve a existir.
- `GbsAnimPlayer::seek(nombre, tick)`: pone una animación en un tick y la mantiene (una copia ya existente conserva su orden de arranque).
- Pruebas: `testGbsArc` (geometría, direcciones, `seek`) y `testGbsArcReal` (28 nodos de `InGame.gbs`, valores de P1/P2, la animación de un anillo).
  Todo en verde salvo `zdroid_overlay_tests` (DSEG, en pausa).
- Android: `versionCode 10`, `versionName 0.6-zgirls-Dev_Nov-07d`.

## Build 0.6 Dev_Nov-07c - las escenas .gbs se leen como las lee OtterUI, y la sombrilla vuelve a su sitio
- **Sombrilla (y demás hijos) separados de las cajas**: los objetos "atados a otro" (`TETHEREDTO`) ahora se mueven cada tick con la
  regla del motor: el modo 1 (el más común: 59 ayudantes nacen así) acompaña al padre por lo que este se movió desde el tick
  anterior, y los modos 4/5/6 (destellos que saltan a una celda del cuerpo) y 7/8/11 (quedarse en un punto con nombre del frame)
  ya estaban; faltan los modos 2/3/9/10 (necesitan la cámara del juego). El guardado del runtime pasa a la versión 4 (guarda dónde
  estaba el objetivo la última vez, así un rollback no rompe la atadura). Prueba: `testFoitTether`.
- **OtterUI como referencia, no como código**: el formato `.gbs` es el de la librería OtterUI (MIT) sobre la que corre el juego. Se
  leyó su fuente solo para **comprobar las reglas**; el código de este repo es una reconstrucción propia (no se copió ni pegó nada y
  la herramienta con la que se compara contra el runtime real vive fuera del repo). `THIRD_PARTY_NOTICES.md` lo dice.
- **Animación**: la suavización de un tramo sale de la clave donde **empieza** el tramo (tipos 0 ninguna / 1 entrada / 2 salida /
  3 entrada-salida; potencia `1 + 4*cantidad/100`; el factor se cuantiza a 1/1000), las claves en el tick >= largo de la animación
  se ignoran y un canal que empieza fuera de la animación no se evalúa. Antes se usaba la clave final y solo salida. Comprobado
  frame a frame contra el runtime real de OtterUI leyendo tu `Main.gbs` (posición, tamaño, giro, color de los 158 nodos en los
  ticks 1-400: idénticos salvo +-1/255 de alfa en un nodo).
- **Pivote como Otter**: la posición de un control es **donde está su pivote**; la esquina superior izquierda es `posición - pivote`
  y el giro es alrededor de la posición. Ya aplicado al título, al menú raíz y a las páginas del menú principal (la diferencia con el
  runtime real bajó de 29% a 0.55% de los píxeles).
- **Texto como Otter** (`src/core/gbs_text.*`, sin SDL, con pruebas): espacio = tamaño/3 + tracking, tabulador, avance de línea
  `tamaño + (int)(tamaño*interlineado)`, modos de ajuste (0 corta por palabras, 1 sin corte, 2 escala al ancho, 3 escala solo si no
  cabe, 4 trunca con "..."), el texto que empezaría bajo la caja se descarta, sombra (`dropShadow`, +d,+d) e inclinación. Salida
  idéntica a Otter con un texto de prueba (lorem ipsum) en la misma caja.
- **Menú principal**: la noticia sigue en 2 líneas dentro de su panel (como tu captura de Android) y la descripción se corta como
  en Otter. Las cajas de toque de la lista se corrigen aparte (`kItemHitShiftX`) para no mover el dibujo.
- Otras pantallas (selector, Play, diálogos) siguen con su lectura anterior del pivote; se pasarán una a una comparando con Otter.
- **Sin hacer**: nodos de arco `CCRA` genéricos (hecho en la Nov-07d), máscaras (`KSMG`), el resto de vistas.
- Pruebas: todo en verde salvo `zdroid_overlay_tests` (DSEG, en pausa).
- Android: `versionCode 9`, `versionName 0.6-zgirls-Dev_Nov-07c` (instala encima de la anterior).

## Build 0.6 Dev_Nov-07b - menú principal con las animaciones del propio juego (Main.gbs)
- **Antes** el menú se movía "a ojo": película que corre sin parar, brillo que pulsa, desplazamientos de la lista y de la banda
  inventados. **Ahora** todo sale de las 118 animaciones de la vista `MainMenu_New` (LAGG) a 60 Hz; el código solo elige cuál suena:
  - `MenuIntro` (70 ticks) al entrar al menú: la película entra deslizando y las etiquetas aparecen al final; la noticia sube con `News_Frame_Anim_On`.
  - Cambiar de opción reproduce un paquete a la vez: `MenuFilmUp/Down` (grupo y película avanzan **un paso**, ya no hay scroll continuo), `Menu0Up*`/`MenuDown0*`, `Menu4Up*`/`Menu4D*`, `MenuUp8*`/`Menu8D*` (las etiquetas que salen y entran) y `MenuCenterSwap` (la cabecera nueva "salta" al sitio). Al terminar se re-enlazan las etiquetas y `MenuReset2` devuelve todo al reposo (se ve igual porque la película se repite cada paso).
  - **Opciones bloqueadas**: su color oscuro (`0x404040`) viene de las propias animaciones (`Menu<N>Locked`, y las letras U/L de los nombres), ya no es un gris elegido por nosotros.
  - Abrir/cerrar un submenú: `SubMenuIntro` / `SubMenuOutro` (la banda sube de y=255 a 88 y vuelve; antes era una curva exponencial) y la fila resaltada de un submenú se desliza 40 px en 5 ticks (`SubAdvSelect`).
  - Al volver de otra pantalla (selector) el menú rehace su intro; si había un submenú abierto se coloca directo (`SubMenuReset`).
- Nodo nuevo en el núcleo: `src/core/menu_list_anim.*` (`MenuListAnim`, sin SDL, con tests; con datos reales comprueba las 64 combinaciones de bloqueado/libre y que todas las animaciones elegidas existen).
- `zdroid_gbs --keys <vista> <anim> rgb` muestra también los tintes de las claves.
- Capturas de humo del menú: `ZDROID_MENU_SCRIPT="120:down,200:enter,260:back"` pulsa teclas en esos fotogramas (`down up left right enter back`).
- Android: `versionCode 8`, `versionName 0.6-zgirls-Dev_Nov-07b`.

## Build 0.6 Dev_Nov-07 - fin de ronda y HUD con las animaciones del propio juego (InGame.gbs / PrePostHud.gbs)
- **Banner "Round Win"** (`PrePostHud.gbs`, vista "Round Win", 208 ticks): al terminar la ronda sale primero K.O. / TIME UP y luego la placa con el titular ("THAT'S A WRAP!" o la versión Perfect) y dos líneas de texto que salen de tu `<idioma>_loc.json`. El resultado se decide como en el juego: K.O. de P1 / P2, decisión por tiempo de P1 / P2, empate o doble K.O.; la línea de abajo califica cómo ganó según la vida que le queda al ganador (>=75, 50, 35, 25, 10, menos) y "Perfect" si no perdió nada. La ronda dura lo que dura el banner (`endHold` 300).
- **HUD dirigido por sus animaciones** (`GbsAnimPlayer` sobre la vista "HUD"): `OnActivate` coloca cada nodo; el panel de combo entra con `P?_Combo_ShowAnim` (5 ticks), aguanta el último conteo un segundo tras acabar el combo y sale con `P?_Combo_HideAnim`; al completarse una barra de super se reproduce `P?_SuperBar_Complete` y con el medidor lleno `P?_SuperMeter_FullGlow` (se apaga con `_Off`). El nombre del combo (POETIC, CUTE, ... del `Combo_<golpes>` del loc) aparece bajo el conteo.
- **Super flash**: mientras el runtime marca SUPERFLASH de un luchador, entra su filmstrip (`P?_Super_Anim_In`: cara en gris de `super_<personaje>01`, viñeta oscura) y sale con `P?_Super_Anim_Out` al acabar.
- **Herramienta**: `zdroid_gbs <archivo.gbs> --keys <vista> <animación>` lista los canales y claves de una animación (nodo, tick, posición, tamaño, alfa).
- Humo para capturas: `ZDROID_PLAY_RWFORCE=codigo:vida%:perfect` fuerza el resultado del banner (1/2 tiempo P1/P2, 3 empate, 4/5 K.O. P1/P2, 6 doble K.O.); `ZDROID_PLAY_HUDDEMO=1` ahora también simula un super flash de P1.
- **Sin hacer / dudas**: barra de stun (hace falta el umbral de mareo), filmstrip de más de un luchador (equipos), animación de pausa "Player N Pause", `Movelist`, panel de `TrainingMode` y el resto de vistas de Main / Options / Help / Store. Un KO con agarre de Filia deja un frame con el muñeco de agarre dibujado además de la rival real (pendiente con el trabajo de agarres).
- Android: `versionCode 7`, `versionName 0.6-zgirls-Dev_Nov-07` (instala encima de la anterior).

## Build 0.8 Dev_Nov-11 - efectos PAFX en el juego en vivo (GLES, opcional) y paletas Claude
- **Efectos por GPU en la partida** (opción de desarrollador "Efectos GPU", **desactivada por defecto**): `FxRenderer` (`src/app/fx_gl.*`) dibuja
  los programas PAFX dentro del renderer GLES2 de SDL por "pases": SDL termina su lote, el pase lee viewport / recorte / destino que SDL ya puso,
  dibuja con sus programas y devuelve **todo** el estado GL (programa, buffer, texturas, mezcla, atributos) antes de que SDL siga. Solo con el
  renderer `opengles2`; en cualquier otro caso (o si algo falla en la partida) se queda la ruta de CPU de siempre.
- **Skin**: sprites desde los planos de índice (textura RGBA8: G = tono, B = banda) + textura de paleta; `sprite_palette` para Pixelated y el
  programa nuevo `sprite_palette_smooth` (bilineal sobre los colores ya pintados) para Smooth; sombras de suelo con `floor_shadow`. Dibujo en dos
  fases (primero decodificar / subir, luego un solo pase). El pack viejo sin el programa nuevo se reconstruye solo ("hecho antes de esta versión").
- **El pack en el dispositivo** (`src/fx/fx_store.*`): `fx/fx.pafx` + `fx/pafx.key` en la carpeta privada de la app; se reutiliza si abre con
  su clave, tiene todos los programas y la huella coincide con tu Core; se reconstruye (escritura atómica) si falta, cambió el Core, es de una
  versión anterior o está dañado. Si falta el Core nunca se reemplaza un pack hecho con uno.
- **Comprobado sin teléfono**: `tests/test_fx_gl.cpp` (58 comprobaciones; job de CI `gl-renderer`) compara sprites, sprites suaves, sombras y contorno
  con la matemática de C++ en distintas escalas, viewports, recortes, tamaño lógico y render a textura, y que lo que dibuja SDL antes y después
  del pase y todo el estado GL quedan intactos. Con arte real (`ZDROID_SMOKE_GL=1` en el visor) la ruta de GPU difiere de la de CPU como máximo
  16/255 en unos pocos píxeles. **Falta la primera prueba en un teléfono real**, por eso la opción viene apagada; el estado se ve en el overlay de la etapa.
- **Sin hacer**: (actualizado en la 0.9: el super ya se reproduce, ver abajo) los tintes de `_palette_effects.ini` y las paletas animadas todavía no se reproducen; normales, "chowder", máscaras de tono, retratos palettizados
  de Otter y desenfoque de movimiento tampoco.
- **Paletas Claude** (`zdroid_palgen`, `src/core/palgen.*`): genera `.pal` a partir de las texturas de paleta de TU juego. Cuerpo y ropa en gris normal
  (la piel un poco más clara), lo que ya era negro sigue negro (las corbatas de Filia, Big Band y Peacock, negras), lo casi blanco, blanco, y los ojos
  en el naranja Claude `#D97757` (tabla de regiones en `src/core/pal_theme_hints.inc`); se conserva el sombreado. Variantes `Claude` y `ClaudeDark`.
  Squigly y Marie se saltan (su textura tiene una región más / menos que la tabla). Faltan los ojos de Beowulf, Big Band, Painwheel, Peacock y Umbrella.
  Los `.pal` generados salen de datos del juego: **no van en el repositorio ni en zips públicos**, solo el generador. Documentado en `docs/MODS.md`.
- Android: `versionCode 12`, `versionName 0.8-zgirls-Dev_Nov-11`.

## Build 0.0.0.1-Dev - resource packs que se mezclan, AOTC 0.18.0 y conversión de personajes MUGEN

- **Resource packs mezclables** (`src/core/pack_merge.*`, `pack.ini`): `*_loc.json` se mezcla clave por clave por defecto (un pack que traduce tres líneas ya no borra el resto del idioma),
  y la sección `[merge]` elige por tipo de archivo `replace | keys | records <Clave> | append`; `requires` / `conflicts` entre packs. El texto mezclado se escribe en `mods/.cache`, nunca en la carpeta del juego.
- **Evitar mezclas por nombre:** `match = path` hace que un archivo solo reemplace el archivo del juego cuya ruta refleja su carpeta (dos `dx1.dds` de escenas distintas ya no se confunden); `ignore = ...` deja fuera archivos del pack.
- **Ahead of the Curve 0.18.0** (restauración con `.foit`, `.art`, `.art-pt`): su `AOTC.lua` corre sin tocarlo (171 reglas, 15 archivos de personaje); `file:lines()` añadido al sandbox de `changes.lua`.
- **Carpeta `examples/`** con un pack de ejemplo, uno de restauración y un mod de personaje (recolores / personajes), probados por `zdroid_pack_tests`.
- **Conversión de personajes MUGEN** (`zdroid_mugen`, docs en `docs/MODS.md`): sprites SFF v1 + paletas `.act` a `.art` + texturas de paleta, sin pérdida (0 píxeles distintos al volver a decodificar con el motor), y un `.foit` con los frames, cajas y una animación por acción
  del `.air`. `foit::write` es la inversa exacta del lector: los 20 `.foit` del juego se reescriben idénticos byte por byte. Sin más datos, el personaje convertido solo se anima.
- **Personaje convertido que juega (plantilla `--moves`)** (`src/core/mugen_moves.*`, `examples/mugen/moves.example.ini`): el CNS / CMD de MUGEN es otro lenguaje y **no se traduce**; en su lugar las animaciones del propio personaje se colocan en estados fijos del motor (`Stand`, andar, agacharse, saltar con aterrizaje, golpes de pie / agachado / aéreos, recibir golpe de pie / agachado / en el aire) con un archivo de texto corto que dice qué acción va dónde y con qué números. Probado con `zdroid_sim` contra un personaje del juego: camina, se agacha, salta, ataca (daño, hitstop, retroceso) y recibe golpes (baja la vida y vuelve a `Stand`). **Todavía no tiene** bloqueo, derribos, especiales, supers ni KO.
- **Extras → Combate libre** (`src/app/fighter_pick.*`): la selección de personaje del juego es una rejilla fija de sus 18 luchadores, así que un personaje que solo existe en un mod no tiene casilla. Esta pantalla lista todos los personajes de los datos montados (los del juego y los de los mods), con vista previa de su pose de pie, sus colores (`<nombre>_<N>p.dds` y `mods/pal`), modo (entrenamiento / contra CPU / 2 jugadores), escenario y el botón A PELEAR; los botones para moverlo son el pad táctil de siempre. Los personajes sin `.art-pt` (todos sus fotogramas están en el `.art`) ya no reciben el aviso de «falta .art-pt».
- **Personajes de mods en la selección de personaje real** (`src/app/custom_cards.*`, `charselect.*`): la rejilla del juego deja 5 casillas reservadas vacías (las dos esquinas de arriba y la fila de abajo); los personajes de un mod las ocupan en orden, en todos los modos **menos Historia**. Como el mod no trae icono, retrato ni placas, se dibujan al abrir la pantalla a partir de la pose "Stand" del propio personaje (icono 01/02/03, retrato grande, placa del nombre y de la peana con la fuente del juego, moneda de la barra de datos) y se registran en la caché de texturas con los mismos nombres que usa la escena (`GameUi::putTexture`). La pose se queda de pie sobre la peana, Tab / toque cambia su color y mantener pulsado abre el editor de color, igual que con un personaje del juego. Hace falta al menos `<nombre>_1p.dds`; caben 5 (los demás siguen en Extras → Combate libre). Humo nuevo: `--cs-pick <nombre>`.
- **Sigue pendiente (dicho claro):** combate sin errores, 3D con fallos, voces, rediseño del menú de mods / packs (#33), y que los personajes convertidos tengan retrato en el HUD de la pelea, bloqueo, derribos y especiales.
- Android: `versionCode 23`, `versionName 0.0.0.1-Dev`.

## Build 0.0.0.0-Dev - retratos del HUD con sus colores (y la numeración vuelve a empezar)

- **Numeración nueva:** la **1.0.0.0 será el lanzamiento principal**; hasta entonces las builds son `0.0.0.0-Dev` (sin fecha en el nombre). Las secciones de abajo (0.9.x) son la numeración anterior y se dejan como historia.
  `versionName` = `0.0.0.0-Dev`; `versionCode` sigue subiendo (22) para que Android la instale encima de la anterior.
- **Retratos grandes (`character_<nombre>_colors|lines|shade.pcx`)**: arreglado el color (ojos rojos, piel rara, sombrero magenta). La capa `colors` **no** guarda un número de fila: guarda la posición de la
  **primera clave de color de la región** dentro de la lista plana de claves del personaje, así que cada valor pertenece a la última fila que empieza en ese valor o antes. Con 5 claves por región coincidía con el viejo `valor / 5 + 1`
  (por eso Filia ya salía bien y Cerebella, Dahlia o Marie no). `portraitRows` (`src/core/hud_portrait.cpp`) calcula la fila de cada valor con una búsqueda: usa la tabla de claves de `palmod` cuando la propia paleta la confirma
  (`coledit::keyCountCandidates`), y donde la tabla es de otra build o no cabe en 8 bits elige las cuentas que hacen que los valores usados caigan sobre inicios de fila. Si ni con las cuentas mínimas caben 255 posiciones
  (Black Dahlia), los valores son números de región.
- **Tono y contorno como el shader del juego** (`Otter_fp-Palettized`): el brillo 100..255 de `shade` va de 0,5 a 14,5 entre los 16 tonos (el más claro nunca se alcanza) con mezcla lineal; el contorno (`lines`) mezcla el color de contorno
  por dieciseisavos, y el valor 0 (capa de contorno) se dibuja con ese color y alfa = fuerza de la línea, no con el color de la región 1 como antes.
- **Comprobado** con tus 19 `.pcx` y las paletas `_1p.dds`: los 19 retratos salen coherentes (Annie, Beowulf, Big Band, Cerebella, Double, Eliza, Filia, Fukua, Marie, Ms. Fortune, Painwheel, Parasoul, Peacock, Robo-Fortune, Squigly, Umbrella, Valentine y Bloody Marie).
  Black Dahlia sale sin el magenta, pero su cara queda oscura (no sé todavía si el juego la pinta así). `zdroid_hudart <salida> --rows <personaje> <carpeta pcx> <paleta.dds> [--fits]` imprime qué fila recibió cada valor.
- **Pruebas:** `tests/test_hud_portrait.cpp` (`zdroid_hudportrait_tests`, 86 comprobaciones: 5 claves por región, regiones de distinto tamaño, tabla de otra build, regiones planas, capa que no cabe en 8 bits, tono y contorno) y el test de retratos de
  `tests/test_main.cpp` puesto al día.
- **Sigue pendiente (dicho claro):** combate sin errores, sprites de efectos de la carpeta `sprites` en combate, el 3D con fallos, voces, y los `.gbs` en el resto del menú (inicio, Galería de Arte, Opciones / pausa / controles) como la edición de PC;
  y el rediseño de mods / resource packs (#33).
- Android: `versionCode 22`, `versionName 0.0.0.0-Dev`.

## Build 0.9.8 Dev_Nov-17 - editor de color: tu propio color para cada personaje

- **Cómo se abre:** en la selección de personaje, **mantén pulsado el personaje** de pie (o la tecla `C` en escritorio; un toque corto sigue pasando al siguiente color). Se abre el editor de color,
  que **no es la capa de mods**: no toca `mods/`, el cargador de mods ni los archivos del juego.
- **Qué puedes hacer** (`src/app/colour_editor.*`): tocar una parte del personaje (o recorrerlas con `<<  <  >  >>`), mover **matiz / saturación / brillo** de esa parte con el sombreado intacto, o elegir **un solo tono**
  de su rampa; para todo el personaje: matiz global, **Pastel / Vivo / Oscuro / Claro / Al azar**; **Parte orig. / Todo orig.** devuelven lo de antes. La vista previa es el personaje de pie, recoloreado al instante (la parte activa pulsa).
  Inspirado en la paleta de sgmpalette de krazete, pero hecho con los datos reales de TU paleta.
- **Cómo funciona** (`src/core/colour_edit.*`): cada fila de una paleta (`<nombre>_Np.dds` = bandas x 16 tonos) es la interpolación lineal de unos pocos **colores clave** (la misma cuenta que `palmod::bake`);
  se recuperan por mínimos cuadrados (exactos a <= 1 nivel en las paletas reales) y se editan. Las tablas de regiones no coinciden en Marie, Squigly, Big Band ni Eliza, así que cada fila usa la cuenta de la tabla solo si ajusta
  (error <= 3) y si no busca de 1 a 16 claves. El editor pinta el sprite de pie con una consulta por texel (`CharSpriteEdit`), sin decodificar nada de nuevo.
- **Favoritos** (`src/core/fav_colours.*`): "GUARDAR" crea `<carpeta de ajustes>/favcolours/<personaje>/<nombre>.zgcol` (texto `ZGCOL 1`) + su `.dds` horneado, y aparece al final de la lista de colores del personaje
  (`fav:<nombre>`, se ve como estrella + nombre). Al abrir el editor sobre un favorito, GUARDAR lo sobreescribe y aparece **BORRAR**.
- **Pruebas:** `tests/test_colour_edit.cpp` (`zdroid_colour_tests`, 124 comprobaciones; 194 con `ZDROID_TEST_PALS` apuntando a paletas reales: ajuste exacto, ida y vuelta, HSV, favoritos en una carpeta temporal). ASan/UBSan limpios.
  Capturas headless del editor en Filia, Marie, Squigly, Big Band, Eliza y Black Dahlia; el visor tiene `--cs-edit`, `--cs-edit-char <nombre>` y `--hold x,y` (pulsación larga) para esto.
- **Sigue sin hacer (dicho claro):** no lo he probado en un teléfono (el gesto de mantener pulsado y el tamaño de los controles táctiles los tienes que confirmar tú); el editor solo trabaja con el sprite de pie del selector, y el color guardado
  se aplica a los **luchadores** por su paleta (igual que las paletas de mods). Los retratos grandes (`character_<nombre>_colors|lines|shade.pcx`) siguen con su propio problema de color (ojos rojos / piel rara): falta ver tus `.pcx`.
  Las filas de Marie y Squigly del `.pal` de las paletas Claude siguen desplazadas (necesito un `.pal` de ejemplo).
- Android: `versionCode 21`, `versionName 0.9.8-zgirls-Dev_Nov-17`.

## Build 0.9.7 Dev_Nov-16 - las luces del escenario sobre los personajes

- **Iluminación de personajes** (`src/core/fighter_light.*`, `art.*`, `foit.*`, `src/app/play.*`): el `.lvl` de cada escenario trae luces (`Light: Amb / Dir / Pt`) que **no tocan el escenario** sino a los luchadores.
  Los sprites ya traen lo necesario: un plano de **normales** (la primera capa RLE de cada imagen; se guarda como diferencias de bytes por columna) y, en la paleta (`<nombre>_1p.dds`), una marca de **"recibe luz"** por color (alfa >= 128).
  Un píxel con marca queda como `color * (ambiente + difusa) + luz de borde`, que es la fórmula del shader del propio juego (`TiledSprite_fp-PaletteGradient`). La luz de borde es la naranja de ASG Labs sobre el lado izquierdo de los cuerpos.
  Se hornea en el sprite al decodificarlo (hilos de trabajo, ruta CPU); un sprite volteado tiene su propia copia si la luz tiene borde; las siluetas (sombra, super) nunca se iluminan.
- **Medido contra una captura del original (asg_labs):** el ambiente que encaja es `Amb` (el de PC), no `VitaAmb` (el de la portátil, demasiado claro); la luz de borde cae a la izquierda en los dos luchadores.
- **Paletas hechas por herramientas** (los `.pal` de `mods/pal`, incluidas las "Claude"): no dicen qué colores reciben luz (todo sale opaco), así que ahora toman esa marca de la paleta propia del personaje (`inheritLitMask`), si tiene el mismo tamaño.
- **Opción nueva** en Developer Options: **Iluminación de personajes** (activada por defecto); apagada, se dibuja como antes.
- **Pruebas**: `tests/test_fighter_light.cpp` (`zdroid_light_tests`, 68 comprobaciones; 152 con `ZDROID_TEST_CHARS` apuntando a los datos reales: todo píxel visible tiene normal y los bordes apuntan hacia fuera). ASan/UBSan limpios. Capturas de una pelea real en ASG Labs con las luces encendidas y apagadas.
- **Sigue sin hacer (dicho claro):** los colores salen **algo más naranjas que el original** (al azul le falta ~0,2 en el multiplicador; no son las luces puntuales: en ASG Labs están a y = 1500 con radio <= 1050, fuera del alcance de un luchador en el suelo; puede haber post-proceso en la captura);
  las luces puntuales (`Pt`) se leen y hay función probada (`pointDiffuse`) pero el juego aún no las aplica; la ruta GPU (PAFX, apagada por defecto) no ilumina; la línea `3D` (inclinación de cámara) del `.lvl` no se lee.
  **No he podido verlo en un teléfono real**: falta que lo pruebes (ASG Labs es el mejor escenario para notarlo).
- Android: `versionCode 20`, `versionName 0.9.7-zgirls-Dev_Nov-16`.

## Build 0.9.6 Dev_Nov-15 - la música la decide el .lvl, sombras de una sola capa, animaciones de textura en el 3D

- **Música del escenario = la que dice su `.lvl`** (`src/core/level.*`, `src/core/music_lib.*`): cada `.lvl` trae `Music_Intro` y `Music_Loop` (p. ej. ASG Labs: `skull04_1` + `skull04_2`).
  Antes se ignoraban: se elegía por `music.ini` y, si el nombre del escenario no coincidía, sonaba una pista "de relleno" de otro nivel (la mezcla de músicas que veías).
  Ahora manda el `.lvl`; `music.ini` solo rellena si el `.lvl` no nombra música o sus archivos no están. También se leen `Music_InterruptIntro` (el loop empieza en "FIGHT!" aunque el intro no haya acabado),
  `Music_Outro` y `Music_Alt_*` (leídos, aún sin usar). Si el `.lvl` nombra una música que no está en tus datos, el registro lo dice: `[music] <nivel>: its .lvl names ...`.
  La caché de pistas ahora distingue dos temas con el mismo loop y distinto intro (`Audio::trackKey`).
- **Sombras sin manchas** (`src/app/shadow_layer.*`, `Skin::drawShadow`): cada imagen del personaje (cuerpo, pelo, arma...) se dibujaba como su propia silueta translúcida y donde se solapaban salían parches más oscuros.
  Ahora todas las siluetas van opacas a una capa fuera de pantalla (a mitad de tamaño, con filtro lineal: borde suave) y la capa se pone una sola vez sobre el suelo con la opacidad y el desvanecimiento por altura de siempre: un único tono, y el suelo se ve a través, como en el original.
  Si el dispositivo no admite render targets, se dibuja como antes. Comprobado con el renderizador por software (`zdroid_shadow_tests`); **las siluetas reales no se han podido ver aquí (no hay sprites en los datos de prueba): falta mirarlo en el móvil**.
- **Escenarios 3D con animación de textura** (`src/core/sganim.*`, `stage3d.*`): los `.sga.msb` de tipo "uv_track" (577 de 1460) mueven las coordenadas de textura: NPC del fondo a cuadros (flipbook, 279 archivos),
  cascadas, humo, pantallas. Se distingue el flipbook (cuadros que se mantienen) del scroll (velocidad constante que suma texturas enteras). Las 883 animaciones de esqueleto siguen sin leerse.
  El reloj de la escena cuenta ticks de combate (no se guarda en las partidas).
- Confirmado en los shaders del propio juego (sin implementar todavía): las luces del `.lvl` (`Light: Amb/VitaAmb/Dir/Pt`) **no iluminan el escenario 3D** (los shaders de malla dan textura x color de vértice y nada más)
  sino a **los personajes**: ambiente + difusa + luz de borde con la normal del sprite, solo en los colores marcados "con luz". Nuestra ruta de CPU dibuja a los luchadores sin esa iluminación, que es la diferencia más visible
  con el original en la pelea (borde naranja de ASG Labs, etc.) y va a ser lo siguiente. Detalle en `docs/RE_NOTES.md`.
- Android: `versionCode 19`, `versionName 0.9.6-zgirls-Dev_Nov-15`.

## Build 0.9.5 Dev_Nov-14 - escenarios 3D: el fondo ya es la escena 3D del juego

- **El escenario se dibuja como escena 3D** (`src/core/sgmesh.*`, `sgscene.*`, `stage3d.*`, `src/app/stage3d_gfx.*`, `docs/STAGES_3D.md`): cada carpeta de escenario trae `background.sgi.msb`
  (qué mallas y dónde) y una `.sgm.msb` por malla. Los dos formatos están descifrados y comprobados con los 29 escenarios y las 1793 mallas de los datos de prueba, sin excepciones.
  Las mallas se dibujan con sus texturas (`<material>.dds`) detrás de los luchadores, con perspectiva real; en pantalla ancha ("No Limits Screen") se ve más escenario a los lados porque la geometría sigue.
- **Cámara enganchada a la 2D**: todas las escenas llevan un rectángulo llamado `FightPlane`, que es la vista frontal completa del escenario (3750x2000 unidades de escenario).
  La cámara 3D se coloca para que ese rectángulo caiga exactamente donde la cámara 2D (zoom, desplazamiento, línea del suelo) pone el escenario, así los luchadores siguen pisando donde siempre.
- **Si faltan datos, sigue el fondo 2D**: sin la escena, o con menos del 60% de los triángulos con textura, se queda la imagen plana de antes (se nota en el registro: `[play] stage 3D unavailable ...`).
  Las texturas se limitan a 2048 y a 160 MB en total (se reducen a la mitad las más grandes hasta que cabe).
- **Opción nueva "Escenarios 3D"** (Opciones de Desarrollador): No / **Auto** (por defecto: 3D, y si el dispositivo tarda más de ~9 ms por cuadro durante 2,5 s seguidos vuelve a la imagen en esa pelea) / Sí (nunca vuelve por velocidad).
  El recuadro de "Info del escenario" muestra mallas, triángulos y milisegundos.
- Lo que **todavía no** hace: animaciones (`.sga`/`.sgs`: NPC, humo, pantallas, desplazamientos de UV), iluminación del `.lvl` (`Light:`), inclinación de cámara (`3D`) ni color de vértice (los datos lo traen casi todo en blanco). Las mallas con esqueleto salen en pose de reposo.
  Solo se verificó con los datos de ASG Labs (los otros 28 escenarios no tienen sus texturas en los datos de prueba, así que caen al fondo 2D). **No probado todavía en un teléfono**.
- Android: `versionCode 18`, `versionName 0.9.5-zgirls-Dev_Nov-14`.

## Build 0.9.4 Dev_Nov-13 - la carga es la del juego, y las imágenes volteadas de los .gbs

- **Una sola pantalla de carga, la del juego**: la carga de la pelea usaba una versión mía (pantalla negra con el corazón de `LoadingScreen.gbs`, el texto "Preparando sprites..." y una barra).
  Ahora es la misma que al arrancar: "LOADING..." con la bombita "8" sobre la presentación lenta de dibujos de línea (`loading_<idioma>`). No se dibuja nada encima.
  Lo único que puede aparecer, pequeño y abajo, es el aviso de por qué no se usó el escenario elegido (por ejemplo "NO .lvl in the mounted data"). Si faltan los
  `loading_<idioma>` en tus datos, cae a `LoadingScreen.gbs` y, si tampoco está, a un texto simple.
- **.gbs: tamaños negativos**: una clave de animación puede dar un ancho o alto negativo (el corazón de `LoadingScreen.gbs` se voltea así: `139,31 -95x98`). SDL ignora un rectángulo
  con tamaño negativo, así que esa imagen desaparecía a mitad de la animación. Ahora se dibuja desde la esquina contraria con el volteo correspondiente (`GameUi`, sprites normales).
- Android: `versionCode 17`, `versionName 0.9.4-zgirls-Dev_Nov-13`.

## Build 0.9.3 Dev_Nov-13 - efectos de paleta (PALFX) y "No Limits Screen"

- **PALFX conectado** (`src/core/palfx.*`, `docs/PALFX.md`): los destellos y tintes de color de los luchadores (golpe, estado, super...) ahora se leen de la tabla de efectos del propio
  juego (`characters/_palette_effects.ini`) y se reproducen un fotograma por tick. Los scripts no nombran el efecto: guardan su hash de 32 bits en los slots `PALFX*`, y el
  cargador busca el efecto por ese hash. Hoy se pinta como un lavado de color sobre todo el sprite (ruta CPU); la ruta GPU (tinte por píxel) sigue pendiente.
  Dudas abiertas, dichas claras: `HiMult` / `LoMult` se interpretan como la fuerza en cada extremo (documentado, sin confirmar) y 2 hashes que aparecen en los scripts no coinciden con ningún efecto de la tabla.
- **Nueva opción de desarrollador: "No Limits Screen"** (Ajustes → Desarrollador, apagada por defecto). Rompe el 16:9 **solo dentro de la pelea**: usa la forma de la pantalla del
  teléfono sin girarla ni estirar nada.
  - La altura lógica se queda en 360 y el ancho crece con la pantalla (un móvil 20:9 pasa de 640 a 800; tope 960). El escenario, el fondo, las sombras y los luchadores ven más a cada
    lado (la cámara no deja que se vea fuera del escenario: si el escenario es más estrecho que la pantalla, se acerca lo justo).
  - **El HUD, los carteles de ronda (K.O., etc.) y los menús de pausa/opciones siguen siendo 16:9 y se quedan en el centro** ("zona segura"). Las barras y el oscurecimiento del
    fondo continúan hasta los bordes. Los botones táctiles de la derecha se mueven al borde real; el stick y lo de la izquierda no.
  - Los menús, la carga y el resto de pantallas siguen en 16:9. Una pantalla que no es más ancha que 16:9 queda igual que antes.
  - Los toques se convierten con el ancho lógico actual, así que el stick y los botones responden donde se dibujan.
  - Probado con capturas a 640 y 800 de ancho (cámara, fondo, HUD, pausa, botones) y 168 comprobaciones de la fórmula de tamaño (`zdroid_layout_tests`).
    **No probado en un teléfono real.**
  - Para capturas de escritorio: `ZDROID_DEV_NOLIMITS=On` y `--smoke-width 800`.
- Android: `versionCode 16`, `versionName 0.9.3-zgirls-Dev_Nov-13`.

## Build 0.9.2 Dev_Nov-13 - acceso a todos los archivos (opcional), como pediste

- **Nuevo botón en la pantalla de arranque: "Usar mi carpeta sin copiar (acceso a todos los archivos)"**. Android pide el permiso (Android 11+: la pantalla de
  "Acceso a todos los archivos" de esta app; Android 7-10: el permiso de almacenamiento), eliges la carpeta del juego con el selector del sistema y **el juego la lee donde está**:
  no duplica espacio, y `mods.ini` / la carpeta de descargas de Steam se escriben junto a ella. Solo se guarda la ruta (`files/dataroot.txt`).
  - La ruta se saca de la carpeta elegida (almacenamiento interno, tarjeta SD, ids `raw:`); si viene de otra app de archivos y no se puede saber la ruta, lo dice y ofrece la copia privada.
    Busca el juego hasta dos niveles hacia dentro, igual que la copia.
  - **La copia privada (Core cifrado en la bóveda) sigue siendo la opción de siempre**; el permiso no se pide hasta pulsar el botón nuevo.
  - Si quitas el permiso en los ajustes de Android, el cargador lo nota (prueba si puede listar la carpeta, no lo adivina) y vuelve a la carpeta propia de la app.
- Manifiesto: `MANAGE_EXTERNAL_STORAGE` (+ lectura/escritura solo para Android <= 12 / <= 10) y `requestLegacyExternalStorage` para Android 10.
- Nativo: antes se ignoraba cualquier carpeta fuera de la app; ahora se usa si se puede listar.
- Android: `versionCode 15`, `versionName 0.9.2-zgirls-Dev_Nov-13`. **La parte Java (permiso, selector, conversión de ruta) solo la valida el CI; no hay Android SDK aquí.**

## Build 0.9.1 Dev_Nov-12 - arreglo: el menú no cargaba (la app ahora copia TODA la carpeta del juego)

- **Qué pasaba**: al quitar el permiso de "todos los archivos" (0.9), la app ya no podía leer la carpeta de datos donde estaban `ui/Win/Main.gbs`, las fuentes, los
  escenarios, etc. (la bóveda solo guardaba el Core), y una ruta guardada fuera de la carpeta de la app seguía apuntando ahí. Resultado: "no carga los archivos del menú".
- **Arreglo**: la pantalla de arranque ahora pide la carpeta del JUEGO (la que contiene `core`, `ui`, `stages`, `characters`... o directamente el Core; busca hasta dos niveles
  hacia dentro). El Core va a la bóveda cifrada y **todo lo demás se copia a la carpeta privada de la app** (`files/data`), que el cargador monta junto a la bóveda
  (el Core es la capa de abajo; la carpeta de la app y los mods la sobrescriben). Sigue sin pedir ningún permiso de almacenamiento.
  - Muestra el progreso ("Copiando 123 / 4500 (350 / 900 MB)"), se puede cancelar, comprueba que haya espacio antes de empezar, y lo que ya está copiado (mismo nombre y
    tamaño) no se repite, así que volver a elegir la carpeta tras una actualización o una copia cortada es rápido. Se omiten `.exe`/`.dll`/instaladores/archivos comprimidos.
    Límites: 8 GB y 200 000 archivos de datos (para no copiar el disco entero por error).
  - "Borrar lo copiado" quita la bóveda y los datos copiados. Mantén pulsado el icono de la app -> "Cambiar carpeta del juego" para repetirlo.
  - Si ya pones los datos a mano en `Android/data/<paquete>/files`, la pantalla ni aparece.
  - Una ruta guardada fuera de las carpetas de la app (p. ej. `/sdcard/sg_dump`, de versiones anteriores) se ignora en Android en vez de dejar los datos vacíos.
- Probado con el visor: bóveda (Core) + carpeta de datos copiada, sin ninguna carpeta Core suelta, carga la escena del menú (32 fuentes, 213 texturas, 30 nodos) y la selección de personaje.
  **La parte Java (selector, copia, progreso) solo la valida el CI; no hay Android SDK en este entorno.**
- Android: `versionCode 14`, `versionName 0.9.1-zgirls-Dev_Nov-12`.

## Build 0.9 Dev_Nov-12 - efectos de super conectados, Core cifrado dentro de la app y desarrollador "Destroyer And Nitro"
- **Qué hace el simulador** (medido con tus datos, no copiado de ningún lado): el luchador que lanza un super levanta dos interruptores. `SUPERSHADOWS`
  queda en 1 durante **todo** el movimiento y `SUPERFLASH` durante su primer tramo (decenas de ticks; el reloj de la ronda se detiene). El valor de
  `SUPERFLASH` es el look del flash (1 super, 2 / 3 combo hiper, 4 contra). `FoitDuel::superState()` los lee y dice de quién es el super;
  cada `FoitView` marca si ese objeto es el que lo levantó (`superLit`).
- **Línea de tiempo de presentación** (`src/fx/super_fx.*`, solo imagen: no se guarda, el simulador no la lee, no afecta al rollback): la oscuridad del
  escenario sube en 8 ticks y baja en 14; el flash es un pulso (sube en 3 ticks, baja en 16, se reinicia si cambia el look). Esos tiempos son números
  neutros propios; los colores y mezclas salen del pack PAFX con los números de TU Core.
- **Ruta GPU** (opción "Efectos GPU"): cada objeto lleva `ScreenFx::make(...)`; el bando del super queda iluminado y todo lo demás oscuro; durante
  el pulso el cuerpo del dueño además dibuja la **silueta de super** en dos colores (los `shadow1` / `shadow2` del `.pal` de su paleta; sin ellos,
  los colores del propio look). El escenario va por SDL: se oscurece multiplicando (`SDL_BLENDMODE_MOD`) y recibe un velo durante el pulso.
- **Ruta CPU** (la de siempre): aproximación - multiplicación de color por sprite y escenario, y el flash como un velo sobre la imagen terminada;
  no hay silueta. Mismos tiempos.
- Captura de ayuda: `ZDROID_PLAY_SUPERDEMO=1` (o la palabra `superdemo` en `zgirls_flags.txt`): el jugador 1 lanza un super medio segundo después de
  empezar la pelea, con barra llena, mientras el 2 se queda quieto. El estado de la opción GPU ahora dice si existe el programa de silueta.
- **Pruebas**: `tests/test_fx.cpp` (rampas, pulso, looks, colores de silueta) y `tests/test_main.cpp` (un super real levanta los interruptores solo en su
  dueño, las vistas lo marcan, y se apagan al acabar); capturas headless de una pelea en las dos rutas. ASan/UBSan limpios.
- **Sigue sin hacer (dicho claro)**: los tintes de `_palette_effects.ini` (el formato se entiende, pero los slots `PALFX*` de los objetos guardan
  patrones de 32 bits que no son números de efecto pequeños ni coinciden con los hashes de nombre habituales, así que no se sabe qué efecto lleva
  cada objeto); las paletas animadas (bytes de tono del `.pal`); las banderas por objeto `IGNOREALLSUPERFLASHES` / `IGNOREPARENTSUPERFLASH` (hoy todo lo del
  bando del dueño cuenta como iluminado); barra de stun; modos 2/3/9/10 de las ataduras (necesitan la cámara); y lo de arriba (vistas de Main / Options,
  cutscenes, Squigly / Marie y los ojos de 5 personajes en las paletas Claude). **La ruta GPU sigue sin probarse en un teléfono real**, por eso la opción sigue apagada.
- **Core cifrado dentro de la app (sin permiso de "todos los archivos")**: la app ya no pide `MANAGE_EXTERNAL_STORAGE` ni los permisos de almacenamiento
  (solo `INTERNET`, para Steam). Pantalla nueva de arranque (`CoreSetupActivity`): eliges UNA vez la carpeta `core` con el selector de carpetas de
  Android; la app la **copia a su almacenamiento privado** (`files/core_import`), el cargador la **sella en un solo archivo cifrado**
  (`files/fx/core.vault`, ChaCha20-Poly1305 con la clave `pafx.key` de esa instalación) y **borra la copia suelta**. Tus archivos originales no se tocan.
  Una bóveda copiada a otro teléfono no abre, y un byte cambiado se rechaza. El Core se lee desde la bóveda como si fuera una carpeta
  (`Vfs::mountVault`, rutas virtuales `vault:<id>/<nombre>`; cualquier carpeta o mod montado después la sobrescribe) y el pack de efectos toma sus shaders de ahí.
  Mantén pulsado el icono de la app -> "Cambiar carpeta Core" para repetirlo; "Continuar sin Core" lo salta. Detalles y límites: `docs/CORE_VAULT.md`.
  Cambia una cosa: una carpeta fuera de la de la app (p. ej. `/sdcard/sg_dump`) ya no se puede leer; el resto de datos va en `Android/data/<paquete>/files`.
- **Desarrollador "Destroyer And Nitro"**: es el nombre del certificado con el que se firma el APK de release (`CN=Destroyer And Nitro, O=Destroyer And Nitro`),
  sale en la pantalla de aviso y en la de Core. El flujo de GitHub firma con tu clave estable si defines los secretos `ZG_KEYSTORE_B64` +
  `ZG_KEYSTORE_PASSWORD` (`tools/make_release_keystore.sh` la crea); sin ellos firma con una clave propia de esa build (mismo nombre, pero hay que desinstalar
  antes de instalar la siguiente) y comprueba con `apksigner` que el firmante es ese. **Ojo, dicho claro:** un nombre en el certificado no quita por sí solo el
  aviso de "desarrollador desconocido" de Android / Play Protect; eso pide registrar el paquete + la clave en la Android Developer Console de Google (hay cuenta
  gratuita de "distribución limitada" para aficionados). Pasos en `android/README.md`.
- **Pruebas nuevas**: `tests/test_vault.cpp` (`zdroid_vault_tests`, 93 comprobaciones): ida y vuelta, nada en claro dentro del archivo, clave equivocada / sin clave /
  tabla dañada / truncada / una entrada alterada, el VFS y su orden de capas, shaders y pack de efectos desde la bóveda, e importación desde la carpeta de paso
  (buena, carpeta equivocada, reemplazo). Con un Core real: 882 archivos (10,5 MB) sellados y leídos idénticos; el visor arrancando solo con la bóveda da la misma imagen,
  byte a byte, que con la carpeta Core. **La parte Java (selector, copia) solo la valida el CI: en este entorno no hay Android SDK.**
- Android: `versionCode 13`, `versionName 0.9-zgirls-Dev_Nov-12`.
