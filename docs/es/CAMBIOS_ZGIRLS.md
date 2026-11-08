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
- **Sin hacer**: nodos de arco `CCRA` genéricos (hoy el anillo del super está a mano en el HUD), máscaras (`KSMG`), el resto de vistas.
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

