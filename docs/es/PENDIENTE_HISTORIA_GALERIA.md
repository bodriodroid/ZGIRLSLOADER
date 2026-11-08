# Modo Historia + Galería de Extras: estado y lo que falta

**Estado (actualizado):** Modo Historia (Visual Novel) **ya está cableado y compila**: Menú -> Story -> selector original
(sin stage ni 2º personaje) -> línea de tiempo -> cutscenes (plano + cuadro de diálogo real + nombre + texto + SKIP) ->
combate -> siguiente nodo. El progreso se guarda en `zgirls.savini`. Probado con los datos de Filia (Filia_Legacy_Intro y
FiliaPremiddle). La **Galería** también está cableada: `gallery.cpp` está en el `CMakeLists.txt` y Extras -> Art Gallery (`ACT_EXTRAS_GALLERY`) la abre desde `menu.cpp`, con selector de música (jukebox) y bloqueos según la Historia.
Voces: `story_vo/lang_<en|jp>/<clave>_<EN|JP>.wav` (ver `storyVoiceFiles` en `src/core/story.h`); si no hay archivo, la línea
queda muda y el log dice qué ruta buscó. Falta: música (`Msg: MUSIC_xx`, hay claves con letras y nombres que no están en
music.ini), retratos THs (no vienen en los datos de prueba), `AnimSpeed`/`ResetAnim`/`P_END`, y peleas de equipo.
Los pasos de abajo ya están todos hechos; quedan como referencia de cómo se cableó.

## Lo que ya está escrito

| Archivo | Contenido | ¿En el build? |
|---|---|---|
| `src/app/zg_progress.h/.cpp` | Progreso de Historia y desbloqueos, guardados en el `.savini` (`src/core/savini.h`). También tiene `zgOption()`, que lee `zgirls_flags.txt` (`story_all`, `gallery_all`). | **Sí** |
| `src/app/story_mode.h/.cpp` | `story::StoryScreen`: lista de personajes, línea de tiempo del `.stm`, reproductor de cinemáticas (GameUi + GbsAnimPlayer, como `attract.cpp`), créditos y avisos de desbloqueo. Pide las peleas con `Out::Kind::Fight`. | **Sí** |
| `src/app/gallery.h/.cpp` | `gallery::GalleryScreen`: cuadrícula de miniaturas, pestañas, visor a pantalla completa (flechas, deslizar, Back), bloqueos según la Historia, fondo `ArtGallery.gbs / Posterpile_BG` y jukebox de música. | **Sí** |
| `src/app/play.h/.cpp` | API de Historia: `enterStory()`, `inStory()`, `storyOutcome()`, `leaveStory()`, `findFighter()`. En Historia, el final del combate muestra CONTINUE / RETRY / GIVE UP, y Back abandona la pelea. | **Sí** (son añadidos y no cambian los modos que ya existían) |

## Cómo se conectó (histórico: los 4 pasos ya están hechos; se conserva como referencia)

### 1. `CMakeLists.txt` (raíz)
Añadir a `ZDROID_VIEWER_SOURCES`:
```
src/app/zg_progress.cpp src/app/story_mode.cpp src/app/gallery.cpp
```
(El APK usa la misma lista. Revisar también `android/app/CMakeLists.txt` por si tiene una lista propia).

### 2. `src/app/menu.h` y `menu.cpp`
- En `Result`: `enum class Kind { None, Play, Home, Quit, Story, Gallery }` y añadir `std::string storyChar;`.
- En los miembros privados: `bool csStory_ = false;` y `std::string prefsDir_;`. En `setPrefsDir()` poner `prefsDir_ = dir;`.
- En `showCharSelect(bool story, ...)`: poner `csStory_ = story;` y recalcular los bloqueos, quitando los que ya se desbloquearon:
  ```cpp
  std::string prog; std::set<std::string> locked;
  if (gd_ && gd_->vfs().readText("ProgressionInit.txt", prog)) locked = CharSelect::parseLocked(prog);
  ZgProgress zp; zp.load(prefsDir_);
  for (auto it = locked.begin(); it != locked.end();) it = zp.storyUnlocked(*it) ? locked.erase(it) : std::next(it);
  charSel_.setLocked(locked);
  ```
  (con `#include "zg_progress.h"`).
- En `actionImplemented()`: `if (k == "ACT_STORY" || k == "ACT_EXTRAS_GALLERY") return true;`
- En `runAction()`:
  ```cpp
  if (k == "ACT_STORY") { if (!showCharSelect(true)) res.kind = Result::Kind::Story; }   // sin Character Select: la lista propia
  else if (k == "ACT_EXTRAS_GALLERY") res.kind = Result::Kind::Gallery;
  ```
- En `frame()`, estado CharSelect, caso `Out::Kind::Confirm`: si `csStory_` es true, hacer
  `state_ = State::Menu; res.kind = Result::Kind::Story; res.storyChar = o.name;` en lugar del aviso `csconfirm`.

### 3. `src/app/zdroid_viewer.cpp`
- `#include "story_mode.h"` y `#include "gallery.h"`.
- `enum class Screen { Home, Browse, Steam, Play, Menu, Story, Gallery };`. Tienen que ir **después** de Menu, porque la barra superior convierte los índices 0..4.
- En `struct App`: `story::StoryScreen story; gallery::GalleryScreen gallery;`.
- En la configuración, junto a `app.menu.setPrefsDir(...)`: `app.story.setPrefsDir(app.prefsDir); app.gallery.setPrefsDir(app.prefsDir); app.story.setAudio(&app.audio);`.
- Después de cada reescaneo, junto a `menu.setGameData(...)`: `story.setGameData(&gd); gallery.setGameData(&gd);`.
- En el bloque Back:
  ```cpp
  else if (app.screen == Screen::Story && app.story.onBack()) {}
  else if (app.screen == Screen::Gallery && app.gallery.onBack()) {}
  ```
- En el `switch (app.screen)`:
  ```cpp
  case Screen::Play:
      app.play.update(ui, dtMs); app.play.render(ui, ren);
      if (app.play.inStory() && app.play.storyOutcome() != play::PlayScreen::StoryOutcome::None) {
          const auto o = app.play.storyOutcome();
          app.play.leaveStory();
          app.story.fightResult(o == play::PlayScreen::StoryOutcome::Won ? 1 : o == play::PlayScreen::StoryOutcome::Quit ? -1 : 0);
          app.screen = Screen::Story;
      }
      break;
  case Screen::Story: {
      const story::Out so = app.story.frame(ui, ren, dtMs);
      if (so.kind == story::Out::Kind::Fight) {
          app.play.enterStory(ren, so.fight.p1, so.fight.p2, so.fight.level, so.fight.rounds, app.menu.rules().difficulty);
          app.screen = Screen::Play;
      } else if (so.kind == story::Out::Kind::Back) app.screen = Screen::Menu;
      break;
  }
  case Screen::Gallery:
      if (!app.gallery.frame(ui, ren, dtMs)) app.screen = Screen::Menu;
      break;
  ```
- En el caso Menu:
  `Kind::Story` → `app.story.begin(mr.storyChar, app.menu.language()); app.screen = Screen::Story;`
  `Kind::Gallery` → `app.gallery.begin(app.menu.language()); app.screen = Screen::Gallery;`
- Barra superior: no dibujarla en Story ni en Gallery (están a pantalla completa). Añadir
  `&& app.screen != Screen::Story && app.screen != Screen::Gallery` a la condición de `drawTopBar`.

### 4. Compilar y corregir
Cosas que conviene revisar en la primera compilación: que se resuelvan los includes de `src/core` (`story.h`,
`text_formats.h`, `vfs.h`, `dds.h`, `menu_data.h`) y que `SDL_PIXELFORMAT_RGBA32` exista (SDL ≥ 2.0.5).

## Datos que tiene que haber en el teléfono (carpeta de datos del usuario)
- `core/arcade/story/*.stm` y `*.ini`, `stages.ini`, `ProgressionInit.txt` y `localization/*_loc.json`: sin esto no hay Historia.
- `Win/CutsceneCommon.gbs` y las texturas `Textures/CutsceneCommon/`, `Textures/Cutscenes2013/` y las fuentes: para el cuadro de diálogo real.
  Si faltan, se dibuja un cuadro simple con la fuente propia.
- `story_art/Cinematics/<dir>/*.dds` (planos, `ShotTex`) y `story_art/THs/<dir>/*.dds` (retratos, `Portrait`).
- Galería: `ui_artgallery/gallery.ini` y sus imágenes (`ui_artgallery/Assets/`). Si no están, se usa cualquier `.dds` de
  `ui_artgallery/`, y si tampoco hay, las imágenes de `story_art/Cinematics/<Personaje>/`. `Win/ArtGallery.gbs` +
  `Textures/ArtGallery/` es opcional (el fondo).
- Los personajes y escenarios de las peleas (`.foit/.art/.lvl`), igual que en el modo Versus.

## Reglas y decisiones de diseño
- Comandos de cinemática:
  - `ShotTex <nodo> <dir> <nombre>` aplica la textura `<dir>/<nombre>` al sprite.
  - `Portrait <lado> <dir> <nombre>` usa `THs/<dir>/<nombre>`; `_EMPTY` lo quita. Se ignora en las escenas antiguas `<Char>ArcadeMode`, que no vienen en los datos.
  - `Text` muestra la línea localizada y espera un toque. Las líneas `Header` y `Portrait` que siguen se aplican a ese texto.
  - `Anim` simple espera a que termine (como máximo 4 s). `_PERSIST` no espera. `_LEGACY` se para en cada marca MSGA hasta `Msg: DIALOGUE_LEGACY_PLAY`.
  - `DIALOGUE_ON/OFF`, `CINE_ON/OFF`, `FADE_ALL` y `CLEAR` controlan el cuadro de diálogo.
  - `Choice`/`Decision` muestran botones.
  - Todavía no se hace nada con `MUSIC_*`, `P_END`, `Voice`, `AnimSpeed` ni `ResetAnim`.
- Peleas: Historia lee `Fighter` (solo el primero, porque el motor es 1 contra 1), `World` (que pasa por `stages.ini` al nivel), `NumRounds` y `UseAI`.
- Desbloqueos (suposición propia, porque no se conoce la regla original): al terminar cualquier historia se desbloquean las de Double y Valentine, y se activa `Gallery<Char>_Story`.
  `UnlockCriteria` de la galería:
  - `OneStory` / `OneArcade`: cualquier historia terminada.
  - `Gallery<Char>_Story` / `_Arcade`: la historia de ese personaje.
  - Un criterio desconocido deja la imagen visible.
- El formato exacto de `gallery.ini` **no se conoce**. Solo se sabe qué claves busca el exe (`Name:`, `Description:`, `ArtistName:`,
  `Tags:`, `UnlockCriteria:`, `Path:`, `CSPath:`). El analizador es tolerante: cada `Name:` abre una entrada nueva.
- Para pruebas: escribir `story_all` o `gallery_all` en `zgirls_flags.txt`.

## Pendiente después de esto
Música y voces de las cinemáticas, peleas por equipos (`VsSubBossDouble`), reglas de jefe (`LuaFile`), Arcade y la
mejora del sistema de combate.
