# Reverse-engineering notes (what is known, how it was established)

Everything here was read from the user's own files (annie.foit/.art/.art-pt/_1p.dds, core/Win/ui extractions,
their copy of SkullGirls.exe read **statically** - nothing was run, unpacked or modified). Addresses are VAs of the
PE32 image (base 0x400000). `TODO.c` (the user's radare2 batch, 6789 function stubs with names/sizes, no decompiled
bodies because r2ghidra was not installed) was used as the function table.

## Verified on real data (tests/real_diff.py, tests/test_main.cpp "real character files")

* `.foit` image records, `.art`, `.art-pt`, RLE, 16x16 block placement, `.dds` palettes: 1,207 records of annie decode
  **identically** (every field + CRC of every assembled frame) to `tools/reference/sg_extract.py`.
* Tag byte: `0x3A`, `0xBA` **and `0x7A`, `0xFA`** (175 records in annie that the original script skips; all validate).
  bit 7 = silhouette layer, bit 6 = **tone rows interpolated**. The grad plane of a flagged image stores the tone bits
  (4..0) of the even rows only; rows 1,3,..,13 of every 16-row tile keep just the line level (bits 7..5) and the game
  rebuilds their tone as `(tone[y-1] + tone[y+1]) / 2` (row 15 is stored).  Drawn as stored they show up as black
  horizontal bars (the 2-row alternation measured in the grad plane).  Found by reading the Sprite Workshop decoder
  (`decode_image`, `flags & 64` on channel 1); implemented in `interpolateOddToneRows` (art.cpp), applied by `ArtFile::decode`
  and by the real frame path `foit::decodeImage`.  The oracle CRC of flagged frames therefore differs from sg_extract.py on
  purpose (test anchor 407).  Channel 0 (the normals, which we do not use for colour) is delta coded in the workshop decoder.
* Coverage of the quick scanner (`ArtFile`, viewer/probe only): .art 85.6 %, .art-pt 65.4 % of the bytes.  The rest is
  explained by the structured frame table: a frame is `origin i32 x2, art bank u8 (5 .art / 4 .art-pt), n markers u8 +
  12 B each, image-slot flags u8 (0x41 = slots 0 and 6)`, and the scanner only recognises frames with 0 markers.  The
  structured parser (`foit.cpp`, what the game uses) reads all 1,549 frames / 1,539 images of annie.
* Palette `.dds`: 16 tones x 52 bands for annie (`annie_1p.dds`, 3456 bytes = 128 header + 52*16*4).

## The .foit character body (decoded; parser `src/core/foit.cpp`, tool `zdroid_foit`)

Read statically from `SkullGirls.exe`: `0x493150` (character body, "Error reading sprite. Expected 0, found %u"), `0x44c2f0`
(frame), `0x463ca0` (image), `0x44ce60` (state header), `0x44d1b0` (node properties), `0x46db40` / `0x46c580` (command header /
step), `0x48c5a0` (script chain), `0x4849b0` (hit data), `0x48c3f0` (sub list). Method: Ghidra's native decompiler
(`decomp_opt` + `sleigh_opt`, built from source) run on an XML image of the exe - static analysis only. **Verified: all 20
`.foit` files supplied (18 fighters, Boss Marie and the hitspark effect; run them with `ZDROID_TEST_FOITS`) are consumed
to the last byte, and every one of Annie's 1,539 frame images decodes.**

```
file    u32 off | 'VRSN' | u32 9 | u8 flags     (flags bit 3: an animation-summary block follows; skipped, state names are used instead)
at off  u8 len + name | u32 v374 | u8 hasLayer
        hasLayer = 1: a layer/palette object (0x485a50: ~300 named layers/effects, colours) - NOT decoded, skipped by locating
                      the frame list (a candidate is accepted only if the rest of the file parses to its last byte)
        u8 c + c*(u32,u8)            list8
        177 * u32                    slot defaults (float bits) - the default value of the object's 177 float "slots"
        u8 chainFlags, one script chain per bit 0..2
        u8 1 (this object owns the frame list) | u32 N | N frames
        states, refs, default push box, u32 nchild | nchild children | nstates * (u8 len + name)
frame   i32 pivotX (A), i32 pivotY (B), u8 src (5 = .art, 4 = .art-pt, 0 = none), u8 n + n*(u32,u32,u32) entries (unknown),
        u8 slotMask (bit i = image slot i present), images, 10 * i32 xy, 3 box lists
        slot 0 = the sprite, slot 1.. = extra layers (Cerebella, Marie, Ms.Fortune use them), slot 6 = always a placeholder
        image slot -> xy pair: 0,1 -> xy[0..1] | 2,3 -> xy[2..3] | 4,5 -> xy[4..5] | 6 -> xy[6..7] | 7 -> xy[8..9]
        the image is drawn with its top-left at (xy - pivot): the pivot is the point between the feet, y grows downwards
        box lists: u16 count + count * (top, bottom, left, right) relative to the pivot: 0 = hit, 1 = hurt, 2 = push
image   u16 cells, w, h, n | u8 flags (low 6 bits 0x3A; 0x80 silhouette, 0x40 odd tone rows interpolated, 0x10 data present, 0x20 external)
        0x04: 2 extra u32 | 0x10: mask (cells+7)/8 bytes, then external: u32 off + 3*u32 sizes (normal/grad/band RLE planes)
state   u8 f (4/8: u32 v18 | 2: u8 c + c*u32 name hashes) | u8 b14 | u16 nTransitions | u16 next state (0-based)
        header: u16 nodes, each node = 3*u16 frame refs (1-based, 0 = none; refs[1] is the body) + node properties
                u8 flag -> command + node properties | u8 flag -> script chain (initial slot values, e.g. walk speed)
        then nTransitions * (u16 target state + command)         e.g. Stand has 144: the cancel / input table
node properties (linked list)   u8 b1,b2,b3 | b3&0x80 chain | u16 duration if b1&1 else 1 (ticks at 60 Hz) | b3&0x40 u8 |
        u8 f2 (1 hit data | 2 sub list | 4 u8 c + c*u32) | u8 f3 (!=0: a command follows and the list continues)
        a node without frame refs is a script node (init / loop label / slot reset)
command u16 a, u16 b, u16 nSteps, u8 flags (2: default step) | steps:  u8 b0,b1,b2 | u16 w | u8 flags (2: chain) | 15 * u8 t
        looks like the motion-input matcher (direction ids + 15 per-button requirements, 10 = don't care)
script chain (nodes)  u8 A,B,C | u8 flags (bit0 boolean node, bit1 more follow) | u8 op | operands a,b,c (1 byte if desc&1 else 4)
child   u32 off | 'VRSN' | u32 9 | u8 0 | body at off with the same grammar, no layer object and no frames of its own
```
A walk through Annie: `Stand` (id 21) loops on itself with 9-tick frames, `WalkFwd` (27) plays frames 44..46 for 3 ticks
each then `WalkFwd_Loop` (28), `s.WP` (118) is the standing light punch whose active frame 137 has a hitbox
(-175,-130,15,202); every attack ends in `Stand`.

### The script VM ("slot ops"), from `0x48cb80` (arithmetic) / `0x48d7e0` (boolean) / `0x48c850` (read) / `0x48ca70` (write)
Each object has 177 float slots at `obj + id*4` (defaults = the table above), a 96-bit flag word array at `+0x2c4`, and
global slots at `0xc51100`. A node is `C := op(A, B)`: 1 set, 2 add, 3 sub, 4 mul, 5 div, 6 abs, 7 trunc, 8 "default": `C := default value of the slot operand A names` (`0x48c9f0` reads the defaults through A, C is only the target: `ANIMHINT = default(WEIGHT)` puts the body's WEIGHT in ANIMHINT, `XVEL = default(A.LIFEBAR)` is the attacker's full life),
9 sin, 10 cos, 0xB max, 0xC min, 0xD atan2, 0xE/0xF conditional set, 0x1C or, 0x1D and, 0x1E mod, 0x19/0x1A/0x1B random...
Boolean nodes compare A and B: 0x10 `<`, 0x11 `>`, 0x12 / 0x16 `==`, 0x13 `>=`, 0x14 `<=`, 0x15 / 0x17 `!=`, 0x18 true.
Operand descriptor byte: bit0 slot id (else float literal) | bit1 global slot | bit2 random(range) | bit5 flag bit |
0x08 `L.` (leader) | 0x10 `E.` (enemy) | 0x40 `T.` | 0x80 `A.` (attacker) | 0x18 `PT.` | 0x90 `PRNT.` (parent) - these prefixes
and the constants `PHYSICSAIR/GROUND/BOTH/LOCK/NONE`, `MIDHITANIM`... come from the script text parser at `0x48dc00`.
Slot ids that are now identified (read from Annie's literal chains, see "Combat numbers" below): `[158]` velocity X,
`[159]` velocity Y, `[54]` gravity (default 1.5), `[59]` state type (2 = airborne), `[107..109]` spawn child (id, offset),
`[170]` hit counter. Slots `[53] [93] [95] [96] [97]` appear in the hit chains and are not understood yet.

### Combat numbers (read by `src/core/moveset.cpp`, checked by `testMoveSetAndDuel` on Annie)
Hit data record (node property `f2 & 1`): `fc` damage in raw game units (s.WP = 300, shown as 30 of 1000 health),
`f10` hitstop ticks (both fighters freeze), `f8` hitstun ticks, `f13` level (1 high / air, 2 low, 3 mid), `f18` strength 1-3.
It carries two script chains (CORRECTED 2-Nov, checked against the shipped `f_49f170` and a ground throw): `chains[0]` = what a defender that is
**not standing on the floor** gets (SPRITESTATE != 2, off the ground, or rising), `chains[1]` = what a **grounded** defender gets
(`[158]` negative = away from the attacker, `[159]` vertical speed, `[59] == 2` = launch). The result codes follow the same split: `f14` / `f15`
(hit / block) for the not-grounded defender, `f16` / `f17` for the grounded one. Codes: 0 nothing, 1 hit, 2 block, 3 throw, 4 touch only
(a ground throw such as Filia's s.Throw_Fwd is `(0,0 | 3,3)`: it only grabs a grounded defender).
Walk 5.5 forward / 4 back, dash 14 / 11, jump vy 32, jump vx 7, gravity 1.5 - all literals in the state header / node
scripts (px per tick at 60 Hz; stage units are sprite pixels: the stage is 3750 wide, the spawns are +-200 from the centre).
Normals are named `s.` / `c.` / `j.` + `WP MP HP WK MK HK` (WP = light punch, WK = light kick). Some normals (Annie's
s.WK) have boxes but no hit data in their own body - the hit data sits in a spawned child object - and get a synthetic
record by strength. Still approximated: which reaction state a hit leads to (picked by level and posture), pushback decay
(x0.88 per tick), knock-down timing, block stun (= hitstun), no chip damage; throws, specials and supers need the command
tables (the `command` grammar above) decoded; children / projectiles are not spawned.

## What this means for the port

* The 144 Lua files in `core/` are **arcade AI and training/tutorial scripts**, not character logic.
* Character behaviour is data inside `.foit` (named states with tick timings and frame lists) driven by an engine whose
  variable/flag vocabulary is visible in the exe strings: `STATENUM PREVSTATENUM SPRITESTATE PHYSICSTYPE HITTYPE
  HITSTUN HITPAUSE BLOCKTYPE NUMHITS NUMBLOCKS DAMAGE DAMAGESCALE CHIPDAMAGE STUNDAMAGE UNDIZZYFALLOFFTIMER
  ARMORCANCELTIME ASSISTOK WEIGHT FRICTION FACINGDIR CAMSHAKE* PALFX* SUPERFLASH ...` and animation classes
  `MIDHITANIM HIGHHITANIM LOWHITANIM KNOCKDOWNANIM WALLSPLATANIM FLOORBOUNCEANIM ...`.
  The `.spt` sources named in `compile.ini` (`ArtAssets/<char>/<char>.spt` -> `export/temp/<char>.foit`) are compiled into
  the .foit, so the .foit very probably carries compiled state scripts. Decoding that bytecode + re-implementing the
  state/hit/physics rules is the core of a real port.
* Still unknown: `.gfs` container layout (`re::gfs::parse_package_header`), `.sb` (39 files), `.fx` (10), `.seq`
  (64 small files, look like input-sequence tables), audio, the `LAGG` animation chunks of `.gbs` scenes (see below).


## `.gbs` UI scenes (decoded for `Main.gbs`; parser: `src/core/gbs_scene.cpp`, checked by `testGbsSceneReal`)

Header and view table: see `gbs.h`. Sections: font chunks (`TNFG`), a texture table (284-byte rows: id, name, ...),
then views (`WVGG`). A view = 140-byte header (canvas size floats at +92/+96; texture, animation and node counts at
+112/+116/+120), the texture and animation id lists, the node tree, and a trailing `LAGG` chunk.

* Node chunk: tag, size, id, name[64], then floats x, y, pivotX, pivotY, w, h, rotation (from +76). `RPSG` sprite
  (texture id +112, mirror flags +124), `LBLG` label (font id +112, scale +120/+124, h-align +136 0 left/1 centre/2 right,
  vertical-centre +140), `PRGG` group (child count +112, children inline from +116; sizes must tile exactly).
  Positions are relative to the parent group. A texture id of `0xe801ffff` means "flat colour / no texture".
* Fonts: metrics are in a 512-wide space while the atlas page on disk is 768 px: scale source rects by
  pageWidth / atlasW. Glyph top = lineTop + font.baseline * scale - bearingY * scale; line advance = font.lineHeight.
  A glyph row is `{codepoint, 0, x, y, w, h, bearingY, advance, bearingX, page}`: the atlas page is the LAST word (word 1 is
  always 0).  `AttractText` has 5 pages, the JP fonts up to 16; reading the page from word 1 draws every glyph from page 0.
  Japanese uses the `<font>_JP` twin fonts.
* Texture names end in `.png` in the scene but the files on disk are `.dds`.
* The rest pose in the file is not the final pose: e.g. in `Splash_2ND` the PressButton, CopyrightNotice and VersionNumber
  labels overlap, and the plain `Skullgirls Text` logo sits under the full `logo_2ec` one; the original fixes this with
  its animations / code. ZDroid hides or offsets those nodes by name (`menu_scene.cpp`).
* `LAGG` (decoded; parser `gbs_scene.cpp`, player `gbs_anim.cpp`, checked by `testGbsAnimSynthetic` / `testGbsAnimReal`):
  `LAGG [size][count]` -> `NAGG` animations `[size][name char[64]][length ticks][loopStart][loopEnd, 0xffffffff = none]
  [channelCount][eventCount][channelBytes]` -> `CAGG` channels `[size][nodeId][keyCount]` -> `MRFK` keys
  `[size][tick][modeA][modeB][8 layout anchors][tag TLPG|TLPS|TLBL|TLRA][blockSize][payload words]`.  Payload words: pivotX, pivotY,
  x, y, w, h, rotation (+ ARGB colour for sprite / label / ring, + scaleX / scaleY for labels; TLRA keeps its raw arc words).
  Events `RFCM [size][name char[64]][tick][count]` hold sub-blocks `ADNS` = sound `[16][sound id][volume 0..100]` (row of the
  scene's sound table, 264-byte rows `[id][name]` between the texture table and the views) and `AGSM` = message `[12][id]` for the
  game code.  Ticks run at 60 Hz; a key is the node's COMPLETE pose (x, y relative to the parent like the resting x, y);
  interpolation is linear (colour per ARGB channel), a key flagged (modeA >= 1, modeB > 0) eases out with power 1 + modeB / 2.
  A sprite without texture and with a pose colour is a flat coloured quad (fades, flashes, "black_screen").  Several animations
  play at once, the one started last wins per node, finished ones hold their last pose, loops use [loopStart, min(loopEnd, length-1)].
* `AttractMode.gbs` (the opening): one view per shot (`shot1` ... `shot17`; `shot18` / `black` are one-tick helpers) with an
  animation of the same name that runs the whole shot (22 s for shot1, 3 s for the fighter posters, 11 s for the final logo);
  `OnActivate` is the resting pose.  The labels are filled by code from loc keys that are the English sentences
  ("Every seven years ...", "However, if the woman is impure of heart,", "THE SKULL HEART" ...).  Music `sg_attract_c` / `sg_attract_c_loop`
  (not in the data folders given so far: the montage is silent until they are mounted).
* Story mode data (`core/arcade/story/*.stm`, `*.ini`): `.stm` = `CharName`, `ArcadeNode` blocks (`NodeType NewScene|Fight`, `FileName`,
  `TimelinePos`, `UnlockCondition <Char> <pos>`), `ProgessionUnlock Story<Char>` at the end.  Cutscene scripts: `Scene:` / `View:` /
  `Anim:` / `ResetAnim:` / `ShotTex: <node> <dir> <name>` (texture `story_art/<dir>/<name>.dds` on the named sprite) / `Text: <loc key>` /
  `Header: Left|Right <name>` / `Portrait: <side> <char> <in> <out>` / `Msg: DIALOGUE_*` / `Choice:` `Decision:`; the "legacy" cutscenes
  are the `*_Legacy_Intro|Ending` views of `CutsceneCommon.gbs` (13 `Shot_NN` sprites + one long animation with message cues).  Fight scripts
  are `Player` / `Fighter` / `Color` / `Assist` / `World` / `UseAI` / `LuaFile` / `NumRounds` / `StartingHealthByPercent` ... lines.
  `ProgressionInit.txt` flags `StoryDouble` and `StoryValentine` as `L` (locked at start): the game unlocks them by playing the story.
  Not decoded: the numeric ids of the `Msg:` names (the cues in the animations only carry `0xeb03....` ids).

## Main menu and Character Select as the PC edition has them (read from SkullGirls.exe + checked on screenshots)

* Root list (list builder at `0x64a780`): Download Content, Achievements, Leaderboards, Extras, Single Player, Versus,
  Training, Help & Options, Exit Game; the cursor starts on Single Player (index 4).  Loc keys: `Download/DownloadDesc`,
  `Achievements/AchievementsDesc`, `Leaderboards/LeaderboardsDesc`, `Extras/ExtrasDesc`, `Single Player/SPDesc`,
  `Versus/VersusDesc`, `Training/TrainingDesc`, `Options/OptionsDesc`, `Exit/ExitDesc`; headers `Textures/MainMenu/header_<x>01.png`.
* Layout: the active item has no text (slot `MenuItem4` is the band); items above use `MenuItem0..3`, items below use
  `MenuItem5..10`; the labels end about 130 canvas px left of the scene's rest pose (right edge ~1010, left of the vertical strip).
  `Main Menu Icon` (`icon01`) is the Versus emblem; other entries show only the lit cell inside the `overglow01` ring.
* Bottom row: loc `Label Text` ("Choose a Menu Option") on the left, `Button Text MainMenu` on the right; its `{gl}{gr}{gu}{gd}`
  tokens are arrows, `{glk}{glp}` = A/X, `{gmk}{gmp}` = B/Y, `{srtg}` = start (`Textures/Buttons/*.dds`).
* News box (`NewsFeed_Main_GRP`): rests at y=912, slides to y~466.  The content is fetched from a server; offline the game
  shows `NewsMessageDefault`.  ZDroid draws the frame + that text only.
* Character Select (`Charselect.gbs`, constructor `0x5e6dce`): 8x3 icon grid of roster indices, -1 = Random; see `charselect.h`.
  Music: `music/select_intro` once, then `music/select_loop`.  Opening: `AttractMode.gbs` (see above), played by `src/app/attract.cpp`.
* Still NOT decoded: the names of the 177 script slots and the meaning of the hit-data fields
  (fights use the real frames, boxes and state graph from the .foit but approximated physics until those are mapped).

## Character Select - how the PC edition builds it (matched against screenshots)

* Three steps, three views of `Charselect.gbs` driven by their own LAGG animations: `CharSelectTeamSizeP1/P2` (`Intro`, `Outro`, `TeamSize<N>Select/Deselect`),
  `CharSelectFighters` + `CharSelectPickFighterP1` (`Intro`, `PortraitIntro`, `P1_F1_NameIntro`, `Outro`, `CharSelectSnd_<name>_1|2`) and `CharSelectStage`
  (`OnActivate`, `StageSelectOutro`, `Left/RightPressed`); `CharSelectBackground_NEW` loops `Lights` / `BG`.
* Some group positions are parked off-screen in the file and put in place by code: the size panel's `Color Select` group (y -436 -> 0) and the stage view's `Character Select`
  group (name bar + arrows; the code puts it at (0, 400), the animation then slides it up from below).  Label boxes sit to the right of what they belong to: the original centres
  `Player Name` on `Header`, the size text / exit text on `Frame`, `StageNameLabel` on `Stage Name Frame`.
* The stage picture is NOT in `CharSelectStage` (`LevelImage` is unused): it is `Stage_01` / `Stage_02` of the background view (783x440 at (392,140), under the art deco `Window`),
  cross-faded with `Stage_Fade_Up`, `Stage_Switch_01` (02 fades in over 01), `Stage_Switch_02` (02 fades out), `Stage_Off`.  `stage_random.dds` is a 12x12 placeholder (nothing is drawn).
* Standing characters: `character_select_<name>.spr.msb` + `.dds` = the tile sprite container of `spr_tiles.h` (anim `idle`, 9..20 frames, frame offset = hot spot = feet), drawn at half size.
  The atlas is NOT a picture: every 16-bit texel is R5 = tone*2 (grad bits 4..0), G6 = palette row, B5 = line alpha; look the colour up in the character's own `<name>_1p.dds`
  (`keyRgb(row, tone)`, same palette as the fight sprites).  Row 0 = black outline with alpha from B5; row 0 with R5 = 31 (0xF800 / 0xF81F) = background key.
  The character stands on `P1_L1_PillarGrp` (follow its pose) at about (pillar.x + 58, pillar.y + 23).
* Voices: the scene's `CharSelectSnd_<name>_1|2` animations cue `UISound/selectconfirm.p1|p2.3.wav` and an event `UISound/charconfirm.a|b.<name>[.playable|.boss].txt`
  (`Event: AnnouncerCharSelect`, `SoundSlot "Announcer<Name>_P1|P2"`); the recording is `UISound/charconfirm.<name>.p1|p2.wav` (`UISound/JP_chars/` for Japanese).


## Custom palettes (.pal) and the palette texture

`<name>_1p.dds` is 16 tones x (1 + regions) ARGB; row 0 = outline layer (all zero in the stock files = black), then one row per
region, tone 0 darkest ... tone 15 lightest.  A community `.pal` stores n key colours per region (light -> dark, n from a per-build
table, 1..8) and the game stretches them: key i of n sits at tone 15*(n-1-i)/(n-1), linear in each channel (alpha included),
truncated.  Verified against Filia (5 keys x 24 regions): 98.7 % identical channels, rest +-1.  Regions fitted the same way across the
whole roster: 893 of 914 region rows are an exact n-key piecewise-linear ramp with the 3.7.14 table (the misfits are Squigly, Marie,
one BigBand and one Eliza row: those regions differ in the user's build).  See docs/MODS.md and src/core/palmod.h.

## Story Mode: voces y guardado (sacado de SkullGirls.exe con Ghidra/capstone)

**Voces del Story.** Cada `Text: <clave>` de un script de cutscene busca su voz en:
`story_vo/lang_<en|jp>/<tokens de la clave pegados>_<EN|JP>.wav`
- Si una línea necesita varios archivos (Cine), el `_` final se cambia por `_1_`, `_2_`, `_3_` (máximo 3).
- Prefijo `_RAW_` en la clave = esa línea no tiene voz.
- `Voice: <nombre>` fuerza un archivo concreto.
- El japonés sale del paquete `story_vo_jp` (en el loader: `mods/sdvoice_japanese`).

**ProgressionInit.txt.** Líneas `clave = valor`, leídas con `"%255s = %255s"`. Valores: `I` incompleto, `C` completo,
`L` bloqueado, `U` desbloqueado, `N` no, cadenas de dígitos (vectores de bits) y contadores. Claves de Story, Arcade,
Gallery*, paletas, escenarios y trials. El juego real guarda esto cifrado en SAVEDATA.BIN (no se replica); el loader usará
un `.savini` propio con secciones `[options]`, `[progression]`, `[earned]` y `[story]` (ver `src/core/savini.h`).

## Objetos hijo "compañeros" (Umbrella / Hungern)
`Object::children` comparte la lista de frames del personaje. `UMBRELLA_Hungern` (hijo 1 de Umbrella) tiene los estados de locomoción
(`Hungern_Stand_Idle_*`, `Crouch_Idle`, `Walk_Forward/Backward`, `Dash_Forward/Backward`, `Jump_Up/Forward/Backward_And_Prejump`) en cuatro niveles
(`Satiated`, `Starving`, `Ravenous`, `Overstuffed`); cada nodo usa `refs[1]` y el pivote del frame es el mismo que el del cuerpo, así que se dibuja en el
mismo punto de los pies. Los estados del cuerpo de ataque/golpe ya incluyen la criatura en sus frames; `Hungern_Wait_Invisible` es lo que el hijo hace
mientras tanto. El nivel lo decide `UMBRELLA_Manager` / `Hunger_Manager_Loop` (no decodificado todavía; se fija Satiated).

## Escenas .gbs: posición de las claves animadas (22-oct)
En `FlyingText.gbs` -> `Banner1`, la barra `MessageBG` va de tamaño 1 a 1294 con posición X constante (711) y el pivote creciendo (0 -> 646 = ancho/2): la posición de la clave es la del pivote, la esquina superior izquierda es `posición - pivote`. Las etiquetas (`MessageTXT`: pos 71, ancho 1176, pivote -56) siguen la misma regla. El resto de escenas verificadas usan esquina = posición (pivote solo como centro de giro), así que la regla se activa por nodo (`NodeState::pivotAnchored`).
Créditos: `Credits.gbs` -> `Credits` (plantillas `HeaderGRP`, `CreditGRP`), `CreditsScroll` = -719 px en 594 ticks, páginas apiladas cada 720 px; la fuente "Credits Names" dibuja una caja en el espacio (no se dibuja).

## `.gbs` texture rows: atlas regions (24-oct)
Each row of the texture table is 284 bytes: `[u32 id][char name[260]][u32 atlasId][f32 u0][f32 v0][f32 u1][f32 v1]` (UVs are fractions of the atlas, top-down). A plain row carries `[0][0][0][1.0][1.0]` or its own id with the full rectangle; a row that names another row's id is a region of that atlas (`Textures/TA_1001_1.png` -> the real file `Textures/TA_1001_1.dds`). `InGame.gbs` has 244 of 246 rows in `TA_1001_1/2` (2048x2048 DXT5): the whole HUD, the Posthud banners (`posthud_cservheader_small01b|02|03` = ENCORE PERFORMANCE? / WHAT NOW, BIG SHOT? / THE STORY CONTINUES), dialog frames, arrows and the Options selectors; `Options.gbs` has one row in `TA_1007_1`. Atlas regions are 1.5x the node size on the 1280x720 canvas for most HUD frames. Game code positions some nodes through named animations (e.g. `P1_OneFighterAnim` t=1: pos 167,54 size 368x29 for the cover plate over the two empty sub-bar slots): read the last key of the animation instead of the node's resting pose.
The big HUD portraits are not in the atlas: `character_<name>_colors|lines|shade.pcx` (404x576, PCX 8-bit RLE, indices only) + the fighter palette: band = colors/5 + 1, tone = shade/255 * (tones-1), multiplied by lines/255; idx 0 with shade 255 and lines 255 is the transparent background. The P2 portrait node has the `flipX` mirror flag.


## Floor shadow (PC engine shader, read from `core/shaders/glsl12/TiledSprite_{vp,fp}-FloorShadow*.glsl`, 25-oct)
The fighters' shadow is the sprite itself drawn a second time through a `FloorShadow` variant of the TiledSprite program: the silhouette is flattened onto the floor plane (every vertex is moved towards the camera by its height above the floor, `gFloorShadowYPos` = 64 is the floor line), coloured black, and its alpha is `0.75 * (1 - |z| / FADE_DIST)` where z is the height above the floor and `gFloorShadowFadeDist` defaults to -400 (the level's `ShadowDist`, so a jumping fighter's shadow weakens and slides towards the camera). `Skin::drawShadow` (play.cpp) is the port's version: one textured quad per image slot, mirrored vertically and squashed by 0.15, with per-vertex alpha for the linear fade; Dev Options > Shadows picks Off / Low / Medium / High (constant alpha) / Game (this).
Note for SDL's software renderer: four equal vertex colours are applied as a plain texture modulation that stays set, so the quad keeps its two vertex alphas one step apart.
Stage sources: the `.lvl` "2D" line names `stages/textures/2D/stage_<name>.dds` - the same file name as the Character Select picture `Textures/charselect/stages/stage_<name>.dds` (960x540, a thumbnail), so the two are told apart by folder. The PC stage itself is a 3D scene (`background.sgi.msb` = instance list, `*.sgm.msb` = meshes, `*.sga.msb` / `*.sgs.msb` = animations / skeletons; skydome, ground, billboard NPC sheets such as `asg_labs_fg_npc_anim_*`).  The file layouts of the scene and of the meshes are written down in `src/core/sgscene.h` and `src/core/sgmesh.h` (readers only; `zdroid_sgscene_tests` / `zdroid_sgmesh_tests` walk a real stage folder when `-DZDROID_TEST_STAGES=<dir>` is given).

### 2D stage backdrops (`stages/textures/2D/*.dds`, 26-oct, from the user's extracted data)
* 46 files, DXT5 3750x2000, no mip chain: exactly the `.lvl` `StageSize`, so one pixel is one stage unit and the picture is the whole stage seen from the front (the NPCs are painted in; the casino / atrium / Under the Bridge ship 3-6 NPC variants, `*_NPC-1..6`).
* **Channel order**: `TiledSprite_fp-LevelFlatBG.glsl` does `texture2D(gDiffuseTexture, uv).argb`, so the file holds R in alpha, G in red, B in green and 255 in blue (the shader's alpha).  Decoded as plain RGBA the picture is blue and ~25-60 % opaque.  Only the `2D/` folder is stored this way; other stage textures (arena crowds, ...) decode normally, and 37 root files also have an all-255 blue channel for reasons that were not checked, so the rotation is applied by folder (and only when blue is >= 99.9 % 255): `unrotateArgb()` in `core/imgscale.*`.
* Floor line: stage y = `BottomClearance` (64) above the bottom edge, i.e. image row `1 - 64/2000 = 0.968`; the PC camera tilts (`3D <rate> <h1> <h2>`) and may show below it, so the picture's last rows are stretched when it ends above the screen's bottom edge.
* `stages/textures/` (not `2D/`) holds the 3D scene's own textures (grounds, NPC sheets, `Arena{Evo,SGCS,SGCS2024}` crowds per fighter, skydome, `uv_*`, `AnimShadow_Atlas`).  The meshes that place them are in the `stages/<name>/` folders (a texture is named by the mesh's `material` string; the V coordinate is stored flipped for the DDS files).  Skeletons and animations (`*.sgs/.sga.msb`) are not read yet.


## The `Normals` hub: which attack a button starts (28-oct, read from the 20 shipped `.foit`, behaviour only)
Every fighter has a state called `Normals` (0 ticks, never shown): its transition table is what the engine walks, top to bottom, when an attack button is pressed on the ground or in the air. First match wins, so the order of the table is the priority order.
* **One transition = target state + one input step.** For a plain attack the step has `t[0..5]` = LP MP HP LK MK HK with `0` = "pressed" and `10` = "do not care" (other values are used by hold / release / charge commands in the special-move hubs, not here). Exactly one of the six is `0`; a throw is the same table with two buttons `0` (`s.Throw_Fwd`, `s.Throw_Back`, `j.Throw`), which is how a throw beats a normal listed after it.
* **Posture comes from the target's name prefix** (`s.` stand, `c.` crouch, `j.` air); the step's first direction byte `b0` is the condition. Direction ids go round the compass: `0` forward, `1` down-forward, `2` down, `3` down-back, `4` back, `5` up-back, `6` up, `7` up-forward, `9` neutral, `10` do not care. Crouching attacks are `b0 = 2` (with `1` and `3` in the other two bytes), plain stand / air attacks `9`.
* **Command normals live in the same table, listed before the plain one**: Annie `s.F+MP`, `s.F+HP` (`b0 = 0`), Parasoul `s.B+HK`, `j.B+WK`, Cerebella / Squigly `s.F+HP`, Peacock `j.MK_F` (`0/7/1`) / `j.MK_B` (`4/3/5`) next to `j.MK_N` (`9`), and down-in-the-air attacks (`j.D+HK` Beowulf, `j.D+MP` Cerebella, `j.D+MK` Parasoul: `b0 = 2` on a `j.` target). BossMarie's rig has `s.U+*` (up + button), which no fighter has.
* **Names are not enough**: the three generations of characters name the six buttons `WP/MP/HP/WK/MK/HK`, `Jab/Strong/Fierce/Short/Fwd/RH` and `LP/MP/HP/LK/MK/HK` (Black Dahlia, Marie), and Peacock's air MK exists only as `j.MK_B/N/F`. The loader now resolves names first and fills what is missing from this table (`MoveSet::Ids::normal`, `normalF`, `normalB`, `normalD`; `hubNormals()`); a test over all 20 files checks that table and names agree for the 18 fighters.
* **Router states**: several heavy normals are a 1-tick state whose `next` is the real move (`s.HP` -> `s.HP_NoChair` / `s.HP_Chair` for Beowulf, Ms. Fortune's `s.MP` / `s.HK` -> `_WithHead` / `_Headless` / `_Headed`, Painwheel's `s.HP` -> `s.HP_Normal` / `s.HP_Beam`, Peacock's `j.MK_*` -> `j.MK_ForReal`). The script picks the branch (head on or off, chair or no chair); the loader follows `next` and hands the router's button to the state it lands in, so the stand-in hit data applies to the move that really swings. Beam / projectile branches have no hit boxes in the body frames (their hit data lives in a spawned object): nothing to arm.


## Data driven fight runtime (`src/core/foit_rt.*`, `src/tools/zdroid_sim`; 1-Nov, behaviour read from the 20 shipped `.foit` files)
The goal is that the fight is run by the `.foit` data itself (same files and format as the PC game), not by hand-made move tables. `zdroid_sim` runs it headless: `zdroid_sim <p1.foit> [--p2 f] --input "n*30,d*3,df*3,f+0*2" [--p2-input ...] [--p1x N] [--p2x N] [--verbose]`; `--dump-child N` / `--dump-frame N` list the states of a child body / the boxes of a frame; `ZSIM_CHILDREN=1`, `ZRT_TRACE=1`, `ZRT_TRACE_SPAWN=1`, `ZRT_TRACE_REC=all` trace what happens.
* **Order inside one tick of an object**: push the input sample, evaluate every transition, pick the first one whose condition holds (or advance the node timer), execute the node (its property records, gated by their commands), then physics. A script that sets `SKIPPED` makes the object look at its transitions again and step once more, up to 500 times, until a state commits.
* **SPRITESTATE** (slot 62) is set by the engine when no script chose it that tick: 1 in the air (or rising), 2 on the ground. Ground and air normals, and the ground / air choice of a hit, read it.
* **Hit resolution**: a hit data is *armed* by a node property record, touches when one hit box overlaps one hurt box of the enemy, and then does nothing more until it is armed again (one touch per arming). **The armed hit survives a state change** (a thrown bomb keeps the hit it was thrown with while it lands and walks) and ends only by a connect or a "clear hit" record. Level mask `f13` against the defender's INVINCIBLE / BLOCKTYPE decides touch / block; the defender gets the attacker's chain as a pending reaction and its own hit-router states run it.
* **Children**: a node property record may carry a sub list; each item = child index in the owner's child list (0xFFFF = only a script slot), forward offset, height offset, and a script run on the new child. The child starts in the state named by the child body header and **runs its first node in the tick it is born** (earlier it skipped it). A helper ends with the "commit" record bit. Bodies have SOLIDFLOOR = SOLIDWALLS = 1, children default to 0. The pool is ~1 KB per object in the shipped engine.
* **Engine ops** (the opcodes above the plain arithmetic ones): 31 = square root, 34 = power, 38/39 = state tag test / not, 42/43 = reaction pending test / consume, 44/45 = x / y of the point whose name hash is B in the current frame's point table (`Frame::entries`, triples hash, x, y), 47 = any character of the team is in a state tagged B, 50 = leader is roster character B-1, 53 = the player has a single character standing, 54 = palette index, 55..64 = decimal digit k of A, bool 65..114 = digit k of A compared with C (== < > <= >= in blocks of ten), 118/119 = give / take a tag. Answered with a neutral 0 until the data exists in the port: 25/26/40/41 (named points of the sprite `.art-pt` files), 27 (palette colour channels), 48/49/52 (match globals).
* **Throws (2-Nov)**: result code 3 links thrower and defender (`attacker.target = defender`, `defender.attacker = attacker`, THROWN = 1, NUMHITS + 1), puts the defender in the character's **`refBf` state (`Throw_Stun`)** and re-evaluates / steps the thrower at once (that is how `Throw_Fwd -> ThrowSuccess_Fwd` fires). While linked: the defender shows **node `THROW - 1`** of its Throw_Stun (the thrower's scripts pick the pose; the node timer does not advance by itself), is placed at `thrower.x + facing * THROWX`, `thrower.y + THROWY`, and fighters are not pushed apart. `THROWADDHITS` / `THROWADDDMG` (cleared every node execution, applied on every pass, also script-only ones with SKIPPED) add hits and damage scaled by `max(0.2, defender DAMAGESCALE)`; **a throw never kills: it leaves 1 HP and sets THROWFATAL on the defender, `THROWFINALIZE` turns that into a KO**. `THROW < 0` = one last placement with `|THROW|`, then the defender is let go (THROWN = 0 -> its `THROWN == 0` transition leaves Throw_Stun); `THROW == 0` at the end of a tick lets go at once; a state change zeroes THROW. Approximation: the original also adds a frame-image rectangle term to the placement (not in the port's frame data); THROWX / THROWY alone already land on the right spot for the grabs checked (Filia 242 forward, release 140 behind and 75 up).
* **Damage scale / combo (2-Nov)**: damage of a hit = `floor(fc * max(floorScale, defender.DAMAGESCALE))` with floorScale 0.2 (0.275 when fc >= 1000). From the 3rd hit of a combo on, every hit multiplies the defender's DAMAGESCALE by 0.875 (never below 0.2). Any node record carrying bit `b3 & 0x20` (every node of the free / actionable states: Stand, Walk, Dash, Jump, Wakeup ...) resets the combo of that object: hit counter 0, THEIRCOMBOCOUNT 0, DAMAGESCALE 1. Throws prorate with their own scripts (`T.DAMAGESCALE := T.DAMAGESCALE * 0.5` at their end).
* **What a connecting hit writes into the defender (3-Nov, hit-apply routine `0x4a05a0..0x4a0c40`, inside `0x49f170`)**: `scale` = the attacking character's DAMAGEMULT (68), times the defender's ASSISTDMGMOD (69) while the defender's ISPOINT (27) is 0. Then DAMAGE (84) = whole(fc x combo proration) x scale, slot 85 = fc as stored, **CHIPDAMAGE (86) = fe x scale** (u16 at hit+0xe, not prorated), NOCOUNTERS (90) = hit flag bit 3, LastHitCannotKill (21) = hit flag bit 0 and defender is its own leader and ISPOINT != 0, HITSTUN (53) = f8, METERGAIN (89) and BASEMETERGAIN (92) = hit+0x2c read as a float (0 for a defender whose life is already gone), BASESTUN (88) = the stun table entry of the hit strength f18 (0..7), STUNDAMAGE (87) = BASESTUN while the defender is the point character. Everything else (life, REDLIFE, TOTALSTUN, super bars, chip on a block) is done by the defender's own hit / block-stun state scripts (`LIFEBAR = sub(LIFEBAR, DAMAGE)`, `TOTALSTUN = add(TOTALSTUN, STUNDAMAGE)`, `... mul(35) add(CHIPDAMAGE)` ...) which also read the clamps (`LastHitCannotKill`) - nothing in C++ touches the life of a normal hit. The hit's flag byte (+0x19) is built by the loader (`0x4849b0`) from the two flag bytes of the record: `fa >> 5` -> bits 0..2 (cannot kill / invert the hit condition / extra re-evaluation) and `fb & 0x1f` -> bits 3..7 (`HitData::flags19()`). Throws take medium stun when their strength entry is 0. **The stun table** is the game's own `characters/_dizzy_values.ini` (loaded by `0x5a5360` through `0x4847e0`: tokens are read in pairs, the first letter L / M / H picks entry 1 / 2 / 3, the number is the stun; `parseStrengthTable`, `World::strengthTable`, `zdroid_sim --dizzy FILE` or the file next to p1.foit); the file is the player's data and is not shipped. Not decoded yet: the assist / hit-owner gates around STUNDAMAGE (attacker fields +0x2ec, +0x3e0 / +0x3e8, +0x367 bit 2: stun of assist hits and the once-per-attack gating) - all of them are inert while no assist exists.
* **Screen edge flag and corner pushback (4-Nov, end of the physics step `0x49b500`)**: TOUCHSCREENLR (13) is **relative to FACINGDIR**: 0 = clear of both screen edges, +-1 = the push box is within 2 px of an edge, +-2 = entirely beyond it, +0.5 more for a point character when the edge is the end of the stage; the value is negated when the object faces left, so positive = the edge in front, **negative = the edge behind** (918 transitions of the shipped data test `TOUCHSCREENLR < 0`). TOUCHSCREENUD (14) is the same idea for top / bottom (+-1, 2, 0) and no shipped script reads it. PUSHBACK (95 time, 96 speed, 97 change per tick, 98 always) is consumed here: while PUSHBACKTIME > 0 and HITPAUSE = 0 the **attacking character** (`+0x37c`, set by the hit routine to the attacker's leader) is moved by `int(PUSHBACK)` px along the defender's facing (`0x49da30` adds to the accumulators `+0x328/+0x32c`, i.e. `accX/accY`), when PUSHBACKALWAYS != 0 or (SOLIDWALLS != 0 and TOUCHSCREENLR < 0, the defender is cornered); then PUSHBACK += PUSHBACKDELTA and PUSHBACKTIME -= 1 whether or not anyone was moved. The defender's own slide is XVEL set by the hit chains. The hit routine **clears** AIRHINT (91), ANIMHINT (93) and PUSHBACK* (95..98) on the defender before its chain runs (the chain refills them) and writes WASHITBYASSIST (94) = 1 when the attacker's character is not the point character.
* **Node event list**: the u32 list of a node record (sounds / effects by name hash) is queued in `World::events` (tick, object id, hash, position) for the host to drain every tick; `zdroid_sim` counts them.
* **Helper idle heuristic**: a helper whose transition table also lists its own posture (Umbrella's Hungern sprite goes Idle -> Branch -> Idle every tick) keeps playing its animation: if a tick ends in the state it started in after a round trip it continues from the node it was on. The shipped rule is unknown; this is cosmetic.
* **Counter hit and wall bounce are data (4-Nov)**: the hit routine writes WASCH (70) = the defender's COUNTERHIT (132) before the chain runs; the x1.5 counter-hit damage, the extra stun and the launch are all in the defender's hit scripts (an idle opponent hit by a light punch loses 300, a counter hit 450). Wall bounce / splat / knock-back are states with `SOLIDWALLS == 0; TOUCHSCREENLR < 0` in their transitions, fed by the facing-relative TOUCHSCREENLR above: nothing extra is needed in C++. Op 8 ("default") takes the default of the slot named by operand **A** (it was reading C; that made `XVEL = default(A.LIFEBAR)` return 0 and collapsed life in some characters).
* **Round flow (4-Nov, `rt::Match`, `startMatch`, `zdroid_sim --match SECS[,ROUNDS]`, tests `testFoitMatchReal`)**: the shipped scripts see the match layer through two slots of every character only. ROUNDOVER (30): `< 0` intro, `0` fight (every cancel needs `ROUNDOVER == 0`), `> 0` round over (1 = KO is tested once by a cinematic; 2 = time over is assumed; `-2` is also tested by ~10 Stand transitions and is not modelled). DOPOSE (135) is a three-step handshake: the layer writes **1** (pose wanted: Stand goes to `IntroPose` when ROUNDOVER < 0 and DOPOSE > 0, or to `WinPose` when ROUNDOVER > 0 and DOPOSE != 0), the pose acknowledges with **2** (`WinPose` entry, `Snapback_StaySeated`), and writes **0** when it is done (`IntroPose_End_TellGame`, `*_SignalDone`). The intro has a second stage: the chosen intro pose (`Intro_Kirby`, `Intro_Rise`, `Intro_WaitForSword`, `Intro_Twins` ...) holds its first frame until DOPOSE **> 1**, so the layer raises 1 -> 2 after `Match::introGoTicks` (90; the real delay is the announcer banner and is not read from the exe). For the end of a round the layer writes 1 to the winner and -1 to a living loser (WinPose copies `max(DOPOSE, 0)` into `[169]` and `WinOrLosePose_Choice` takes `Pick_LosePose` when it is 0); a fighter with no life left is not asked for a pose. **`Match` (saved and hashed, save format v2)**: Intro -> Fight (99 s clock, stopped during a super flash) -> on a KO `Settle` (ROUNDOVER stays 0 so the killing blow can still wall-bounce, the fallen fighter must lie still on the ground for 20 ticks) -> Ending (ROUNDOVER 1 / 2, poses) -> next round (fighters re-created: same side, colour and meter, full life, start spot, Stand; helpers removed) -> Over after `roundsToWin` rounds. **Assumed, not read from the exe** (no state names or hashes exist in it for any of this, and the original match code goes through slot-by-name lookups): the settle rule, the 90-tick go delay, the time-over winner (more life), a draw giving both sides the round and the win pose, and that push boxes are off once ROUNDOVER > 0 (Fukua's `WinPose_RunAway` runs through the opponent and only ends at TOUCHSCREENLR >= 2). All 19 shipped fighters (hitspark is an effect pack) go through intro -> time over -> win / lose pose -> over without a pose timeout in both directions (local battery script). **Reading note**: for test nodes the VM compares operand A with operand **C** (`a > c`); older dumps printed operand B, which only matched C when both were 0 - `describeChain` now prints C (and `{b=...}` when B differs).
* **Hooked into the GGPO-shaped match layer (5-Nov, `src/core/foit_duel.*`, tool `zdroid_foitduel`)**: `FoitDuel` holds one `rt::World` and exposes the four callbacks `fight::Callbacks` wants (`foitDuelCallbacks`): *advance* = fight.h button bits -> one `rt::Sample` per fighter (macros expanded, opposite directions cancel, LP MP HP LK MK HK = bits 0..5) -> `tickAll`; *save / load* = `saveWorld` / `loadWorld` + the pad's edge state (the previous sample and the previous raw buttons) next to the 188-byte `MatchState`; *checksum* = the 32-bit fold of the world hash is written into `MatchState::rng`, so `SyncTestSession` / a future GGPO backend compare the real fight and not just the numbers the HUD reads. The HUD / AI numbers are projected into `MatchState` every tick (health = LIFEBAR / the life the fighter started with x 1000, meter = SUPERBAR x 10 (100 per level, 5 levels), combo = slot 11, clock = `Match::clock`, `roundOver` from the phase). The round flow is entirely inside the world: `MatchOptions` (`introRounds`, `readyTicks` = the "Take N" card hold, `endHold` = minimum K.O. banner, `endless` = training: no clock and the life is topped up before every tick so no hit can kill) and the phases `Intro -> Ready -> Fight -> Settle -> Ending -> (next round | Over)`; the app only reads the phase. Nobody can act outside Fight / Settle (the pad is replaced by a neutral sample: the shipped engine cuts the control outside the `.foit` scripts), and an attack button *pressed* during the entrance cuts it short (`matchSkipIntro`), decided from the replicated input so both peers agree. Save format v3 (adds `Match::introRounds / readyTicks / endHold / endless / maxLife`, `teams`, `anchor`). `zdroid_foitduel A.foit [B.foit] --intro --ready 90 --hold 90 --synctest --rollback 700,30` runs a whole match through `SyncTestSession` (every tick replayed from a snapshot) and a deep rollback (save, 30 ticks, load, replay: every checksum repeats); Annie vs Eliza: 4441 frames, 0 mismatches, about 25 microseconds per tick without the replay. The teams code (side-based input routing, TAGTO / TAGINFO, point anchor) is in the source but is off by default and not tested yet.
* **The fight screen runs on it (6-Nov, `PlayScreen`)**: when both characters have their `.foit`, `PlayScreen` builds a `FoitDuel` and drives it through the same `LocalSession` as before (`foitDuelCallbacks`); the log says `fight engine: the .foit data itself (foit_duel)`. Without the `.foit` data (or with `ZDROID_PLAY_OLDDUEL=1`) the old move-table fight (`duel.cpp`) still runs. The screen is a reader of the world: the phase picks the card (Intro = entrance, Ready = "Take N", the first Fight ticks = "Action!", Ending = K.O. / time-over banner, Over = match-end overlay), `FoitDuel::views()` lists what every object shows (the two bodies, then the projectiles / effects / helpers they spawned), drawn with the frame's own offsets and the object's XSCALE / YSCALE / ALPHA slots (164 / 165 / 166; an entrance starts with XSCALE 0, so a hidden fighter also casts no shadow). F1 (`ZDROID_PLAY_BOXES=1`) draws the real push / hurt / hit boxes of every frame. The camera keeps both fighters in view: the floor sinks a little when somebody is launched and then the camera zooms out instead of cutting the other fighter's feet. Checked headless against Annie vs Filia: entrance, "Take 1", "Action!", hit effects, round 1 won by time / KO, "Take 2", round 2. The tie-break rule (a drawn deciding round plays one more round) is an assumption and is off unless the match asks for it. `testFoitDuelReal` (needs `ZDROID_TEST_FOIT=<folder with annie.foit>`) covers input conversion, synctest, deep rollback, a whole match in order (Intro, Ready, Fight, Ending, Over), training, tie-break and skip-intro.
* Not done yet: tag / assist logic and the real timing of the match layer (announcer delay, KO settle), the game's own camera (the zoom above is a stand-in), root motion from frame pivots, stage config. Tools: `zdroid_sim --dump-state NAME` (nodes, records, hit codes), `--dump-state '?refs'` (refBe / refBf / refC0), `ZSIM_P2=A-B` (P2 every tick), `ZRT_TRACE_THROW=1`. Also `--p1-life/--p2-life/--p1-meter/--p2-meter N` (start values), `--rollback-check K` (save at tick K, replay, every hash must match), `--match SECS[,ROUNDS]` (run the round flow, neutral inputs after the script), `--print-slots 38,66,84` (final value of those slots for both fighters), `--dizzy FILE` (stun table), env `ZSIM_P2`.

## HUD events and the end-of-round banner (7-Nov)
* **Round result** (`PrePostHud.gbs`, view "Round Win", animations "Fight Win Animation" / "Fight Win Animation Perfect", 208 ticks): the game reduces the round to a code - 4 / 5 = K.O. win of P1 / P2, 1 / 2 = win on time of P1 / P2, 3 = draw, 6 = double K.O. - the winner's life in percent and a "perfect" flag (the winner lost nothing; only K.O. wins can be perfect). Top line: `P1Win` / `P2Win` / `P1WinByTO` / `P2WinByTO` / `DrawTop` / `DoubleKOTop`; bottom line: `WinnerLife100` (perfect), `75` (>= 75 %, also 99), `50`, `35`, `25`, `10`, `1`, or `TimeOut` / `Draw` / `DoubleKO`; all are keys of `<lang>_loc.json`. The "Posthud_Header" sprite swaps to the *perfect* header texture for the perfect variant; the two holder sprites and the translator helper label are parked off screen.
* **HUD view animations** (`InGame.gbs`, view "HUD", 134 animations): `OnActivate` is the opening pose of all 235 nodes (combo panels and filmstrips off screen, pips and glows at alpha 0). Bars look built to be *sampled*: `P?_F1_HealthAnim` is 1000 ticks long (the energy bar grows 10 -> 375 px, `HealthDivide` travels with it) and `P?_Stun_Anim_On` shrinks the stun cover over 1000 ticks, so the value is most likely the tick (the port crops the bar sprites instead, which looks the same). Events: `P?_Combo_ShowAnim` (5 ticks, off screen -> in place) / `P?_Combo_HideAnim` (65 ticks: holds 59, slides out in 5), `P?_SuperBar_Complete` (65: `SuperBallFull` fades 255 -> 0), `P?_SuperMeter_FullGlow` (loop 0..41) / `_Off`, `P?_Super_Anim_In` (80 ticks, loop 18..49: filmstrip, face flicker, wear strips, vignette) / `_Out` (11), `P?_Stun_Glow_On`, `PauseNotification` (30), `P?_Data_Frame_ShowAnim` / `HideAnim`, `Show/HideTrainingDetails`, `P?_OneFighterAnim` / `TwoFighterAnim` (drop the health plate into its slot). The exact trigger conditions of the super-meter animations are reconstructed from the names (a stock gained, the meter full), not read from the code.
* **Combo words**: `HUD_Combo` has `P?_Combo_Label` (+ "Label Backer" shadow); the text is the loc key `Combo_<hits>` (2 = POETIC, 3 = CUTE ... 999); counts without a key keep the last word at or below them (assumed).
* **Face art** for the super filmstrip is `super_<character>01.dds` (382x796) in the pack's `true_color_hud` folder, next to `dead_<character>.dds`.


## Main menu: what the animations do and what is left to the code (7-Nov)
* The scene `MainMenu_New` (Main.gbs) is built so that the code only picks animations. Film strips, glow, header art and the label fades are all keys; nothing in the program moves them by itself (the earlier continuous film scroll and pulsing glow of this port were inventions).
* **List window**: eleven labels `MenuItemOffTop`, `MenuItem0..10`. The highlighted entry is not a label but the header art (`MenuItemActive`), so rest shows entry `sel-4..sel-1` in `MenuItem0..3`, nothing in `MenuItem4`, `sel+1..sel+5` in `MenuItem5..9`; `OffTop`, `4` and `10` are the spare labels that slide in (their rest alpha is 0).
* **One step down the list** (highlight to the next entry) plays together: `MenuFilmUp` (group -39 px, horizontal film +73, vertical film -74; 6 ticks), `Menu0Up<L|U>` (top label leaves), `MenuCenterSwap` or `CenterSwapLocked` (header art grows from 352x86 at 727,104 to 587x144 at 102,74), `Menu4Up<a><b>` (label 5 leaves, label 4 joins), `MenuUp8<a><b>` (bottom label joins). **One step up**: `MenuFilmDown`, `MenuDown0<a><b>`, `CenterSwap*`, `Menu4D<a><b>`, `Menu8D<a>`. The letters are `U` (usable) or `L` (locked: the label is tinted 0x404040 by that animation); each letter names the label it touches (`Menu4Up`: a = label 5, b = label 4; `MenuDown0`: a = label 0, b = OffTop; `Menu4D`: a = label 4, b = label 3; `MenuUp8`: a = label 8, b = label 9). Variants with 6, 7 and 9 exist in the file but nothing selects them.
* When the step is over the labels are bound one entry over and `MenuReset2` (group, films, spare labels back to rest) plus one 1-tick `Menu<N>` / `Menu<N>Locked` pose per label (visible, tinted or not) restore the rest state. The film repeats every 73 px, so the jump is invisible.
* `MenuIntro` (70 ticks): horizontal film slides from x=-383 to 187, vertical film from y=-86 to -599, labels / header / glow / hint line fade in at ticks 64-69, 8 tick sounds every 8 ticks. `SubMenuIntro` (15): band y 255 -> 88, vertical film -599 -> -767, glow off at tick 4, one sound; `SubMenuOutro` (5) goes back. Highlighting a sub-page row slides it 40 px right in 5 ticks (`SubAdvSelect` / `SubChoiceSelect`).
* The "Main Menu Icon" sprite is hidden in every animation, so the original must show it from code (this port draws it, scaled by the glow ring's alpha).


## Scene runtime rules, checked against the real OtterUI runtime (8-Nov)
The `.gbs` files are OtterUI scenes. The MIT-licensed OtterUI source was used only as a reference to check these rules (a small harness
outside this repository ran the real runtime on the user's `Main.gbs` and its per-node pose/colour at every tick was diffed against
this port's); the code in `src/core/gbs_anim.*`, `gbs_text.*` and `game_ui.*` is an independent implementation.
* **Easing**: a segment is eased by the key it *starts* on. Type 0 none, 1 in, 2 out, 3 in-out; power `1 + 4 * min(amount, 100) / 100`; the
  segment factor is quantised to 1/1000 before easing (in: `f^p`, out: `1 - (1-f)^p`, in-out: the two halves). Keys whose tick is >= the
  animation length are dropped, and a channel whose first key is not inside the animation is not evaluated.
* **Pivot**: position is where the pivot sits. A control's top-left is `position - pivot`; rotation turns about `position`.
* **Node data** (offsets from the start of the chunk): control block `anchorFlags` +104, `maskId` +108; sprite colour +116, skew +120, flip +124;
  label colour +116, scale x/y +120/+124, skew +128, drop shadow +132, h-align +136, v-align +140, leading +144, tracking +148, fit +152.
  A sprite key is 7 words + colour + skew, a label key 7 words + colour, scale x, scale y, skew, drop shadow.
* **Text**: pen advance per glyph is `advance + tracking`; a word is placed when it fits (`curX + width <= box`), else the line breaks and a
  white-space word at a line start is dropped; a line whose top would be below the box ends the text. Glyph top is
  `lineTop + baseline - (bearingY + 1)`. Fit modes 0 wrap / 1 no wrap / 2 scale to the box width / 3 scale only when wider / 4 truncate with "...".
  The next line starts `fontSize + (int)(fontSize * leading)` lower.
* **Where this port still differs from the real runtime** on the main menu: the "SINGLE PLAYER" header sprite edge (sub-pixel sampling), the main
  menu icon (drawn by game code, not by the scene), and +-1/255 of alpha on the "Fade Up" node.
* **Arc nodes (`CCRA`, keys `TLRA`)** - decoded in `gbs_scene` (`GbsNode::Type::Arc`, `GbsKey` kind `'R'`) and drawn by `GameUi`; geometry in `core/gbs_arc.*`.
  Node: seven words after the generic header = segment count, centre-line radius, thickness, colour 0xAARRGGBB, sweep (deg), start angle (deg),
  direction flag. A key (`TLRA`, 12 words) is the seven pose words followed by radius, thickness, colour, sweep, start - the five values an
  animation can move (the colour word is the key's colour; before this it was misread as the radius). Only `InGame.gbs` has them: 28 nodes, the
  super-meter rings of the HUD (per player: groove `SuperBar_0`, one ring per stock `SuperBar_1..5`, and `SuperBarFlash`; every one twice).
  The band is centred on the middle of the node's box, outer radius `radius + thickness/2` (89 px for 80 + 18), inner `radius - thickness/2`.
  Point at fraction t of the sweep: angle = start + t*sweep for flag 1, else start - t*sweep with the point turned half a circle (both
  coordinates negated); x = r*cos(angle), y = r*sin(angle), y down - so angle 0 is 3 o'clock and angles grow clockwise on screen. A segment is a
  flat quad between two neighbouring angles. Nothing is drawn with fewer than 2 segments, no thickness or no sweep.
  Player 1 and 2 are mirror images (start 44 / 316, 19 / 341, opposite flags). The groove has flag 1 for P1, the rings flag 0 (and the reverse
  for P2), so a ring *shrinks towards its start point*: `P?_SuperBar<N>_Anim` (1000 ticks) takes the sweep from 155 at tick 1 to 0 at tick 999
  while the colour goes to a brighter shade of the stock's own.
* **How the HUD drives the rings**: the game does not set a sweep, it puts the stock's animation on a tick. Per player, with `stocks` = whole
  stocks (meter/100 in the game's units, clamped 0..5) and `frac` = how far the next one is: rings `N <= stocks` rest on tick 1 (full) and only
  `N == stocks` is shown; ring `N == stocks + 1` is put on `1000 - floor(frac*1000)` (clamped to 1..1000) and shown; every other ring rests on tick
  1000 and is hidden. The groove is always shown; the flash is an alpha animation (`P?_SuperBar_Complete`). `GbsAnimPlayer::seek` is the
  "put this animation on that tick and hold it" call; an existing copy keeps its place in the start order (a node two animations share goes to the
  one started last), so a ring's glow animation still wins over its seek.

## Render effects (9-Nov; behaviour read from the user's `core/shaders/*.fx`, the numbers are NOT copied into the repo)
What the game's shaders do, as far as the Android effect programs (`src/fx/`, docs/PAFX.md) need it. The numbers (flash colours, curve
constants, death-look colours) are read out of the player's own Core at pack time into the sealed `.pafx`; this section only records the
structure.

* **Fighters (`PaletteGradient`)**: a sprite texel is three bytes. R = a packed normal (two 4-bit components), G = line level in the top
  3 bits and tone in the low 5, B = palette index. The pixel colour is the palette row's gradient at the tone, blended between the four
  nearest texels with the sprite's own bilinear weights; the line level then lerps towards the line colour and raises alpha. Lighting
  (up to 3 directional + 4 point lights, ambient) only touches texels whose palette entry is flagged "lit". A per-row hue mask and a
  "palette FX" colour with its own no-light flag come from a second small texture; a world-space "chowder" field tints by floor position.
  The engine's CPU path (`toRgba(buildRgbaTables)`) is the same gradient / line maths without lighting; `sprite_palette` reproduces it on
  the GPU to within one 8-bit step.
* **Super flash**: two stages. The background is darkened towards a tinted luminance (`Apply_SuperShadowBG`: lerp(colour, tint * luma,
  t)); then the whole picture is remapped between a low and a high colour by luminance (`Apply_SuperFlash`): the remap is scaled by
  `brightness * (1.2 + lerp(0.5, luma - 0.5, contrast))` and pushed away from / towards grey by `-saturation`, then lerped in by `t`.
  There are four looks (super, DHC once, DHC twice, counter-out) per family; the sprite family (fighters, stages) and the Otter UI
  family have different tables but the same maths. The look index is the game's flash number minus one.
* **`SuperOutline`**: the no-light sprite with its colour replaced by a lerp between two palette-FX colours; coverage / alpha unchanged.
* **`FloorShadow`**: discards when `|z| > fade` or `z * fade > 0` (the sign of the fade distance picks the side that casts), and where the
  texel has neither palette index nor line; otherwise black with alpha `maxAlpha * (1 - |z| / fade)`. (The geometry flattening is in the
  25-oct section above.)
* **Jojos death (Otter UI)**: portrait = stroke / fill colours mixed by the coverage channel, alpha from the larger of two samples one
  texel apart (widens the stroke); background = radial gradient through five colour stops measured from the centre with x stretched by
  the aspect ratio.
* Shader files present in a Core: `.fx` (HLSL, the readable source), `glsl12/*.glsl` (the compiled desktop variants, one file per
  technique) and `pssl/*.sb` (PS4 binaries, not read).

## Super move switches and the PALFX slots (measured on a running fight)

* A super raises `SUPERSHADOWS` (slot 72) on its owner for the whole move and `SUPERFLASH` (slot 71) for the first stretch; both are plain
  on / off values (the flash value is the 1-based look).  Helpers of the owner can raise them too.  See `docs/PAFX.md` ("The super move picture").
* The `PALFX*` slots (73-79, 46) hold 32-bit patterns: the hash of an effect name from `_palette_effects.ini` (case-insensitive, `h = 5381; h = toupper(c) ^ (h * 33)`;
  the earlier hash attempts failed because they were additive variants).  Scripts copy them around (`PALFXHIT = set-ifnot(FLAG0, BLOCKED)`, `PALFXSELF2 = set(FLAG0, 0)`),
  so the value is chosen by data.  Resolved: see `docs/PALFX.md` (format, playback, what is drawn, what is still interpretation).

## Stage lights, stage meshes, stage music (measured 15-nov)

* **Stage meshes are not lit.** The mesh shaders of a Core (`renderer_*-Rigid`, `-Skin`) output `texture * vertex colour` plus the same super flash / super shadow
  maths as the sprites, and nothing else: the `.lvl`'s `Light:` lines never reach them. (The vertex colours are nearly all white in the data we have; the shader does multiply by them.)
* **The `.lvl` lights are for the fighters.** `Amb` = ambient colour, `Dir` (up to 4: colour + direction of travel, unit length), `Pt` (up to 4: colour, position in stage units, radius);
  colours are 0..255 integers. In the `PaletteGradient` fragment shader a lit texel becomes `base * (ambient + diffuse) + specular`, mixed in by the palette entry's lit flag.
  Diffuse of a directional light is `clamp(-dir.z, 0, 1) * colour` (the normal is NOT used: fighters are flat planes facing the camera); the rim ("specular") is
  `clamp(-dot(normal, dir'), 0, 1) * colour` with `dir'` = the direction with its z squashed to >= 0 and renormalised. Point lights: attenuation `1 - clamp(dist / radius)`, diffuse `clamp(dirToLight.z, 0, 1)`,
  rim from the direction with z squashed to <= 0, damped near the sprite when the light is behind it. The stage carries two ambient lines: `Amb` and `VitaAmb` (values may exceed 255, the handheld build's brighter one).
  Measured against an original PC screenshot (asg_labs), the fighters' colours only fit **`Amb`** (with `VitaAmb` they come out far too bright), so `Amb` is the default and `VitaAmb` is kept for comparison.
* **What a sprite carries for the light (measured 16-nov).**
  - *Lit flag*: the palette texture's alpha, one value per (band, tone); from 128 up the colour takes the light. Band 0 (outline layer) never does.
  - *Normal plane*: the first RLE plane of each image. It is stored as byte differences down each column of a 16-row cell strip (prefix sum mod 256 per column, restarting in each cell strip);
    a texel's high nibble is x and its low nibble is y, `(n - 8) / 7` clamped to >= -1 (8 = zero). +y is up, edge normals point outward. A mirrored sprite flips x with it.
  - The rim side was checked against the original: the warm rim of asg_labs falls on the left edges of both fighters (the flipped one included), so for a sprite that is not mirrored the normal's x is taken the other way round.
* **Open (honest):** with the shader's own maths our lit fighters come out somewhat more orange than the original (the blue channel of the multiplier is ~0.5 against ~0.7 measured). It is not the stage's point
  lights: on asg_labs they sit at y = 1500 (stage units) with radius <= 1050, out of reach of a fighter standing on the floor. The screenshot may also carry post-processing we do not know. The `.lvl` `3D` tilt line is not read.
  Point lights are parsed and `FighterLight::pointDiffuse` exists and is tested, but the game does not call it yet.
* **Music lines of a `.lvl`:** `Music_Intro` / `Music_Loop` name the stage's own theme (file stems), `Music_InterruptIntro N` (>0: the loop starts at FIGHT! even if the intro has not ended),
  `Music_Outro`, `Music_Alt_Intro` / `Music_Alt_Loop` (replace the intro / loop when the game option "alternate stage music" is Yes; `NONE` = no intro). The value NONE means none.
  A stage that names none falls back to the music table.

