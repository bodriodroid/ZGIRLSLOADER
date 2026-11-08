# Disclaimer / Aviso

The same text is shown by the app on every launch, on the red-on-gradient splash that comes up while the game
data is being indexed. Only an explicit **Continue / Continuar** moves on; **Exit / Salir** closes the app.
The strings live in one place: `own()` in `src/app/menu.cpp`, keys `dis.title`, `dis.1`, `dis.2`, `dis.h`,
`dis.a`, `dis.b`, `dis.c`, `dis.5`, `dis.go`, `dis.exit` (English and Spanish).

## Español

Este es un intento de port de Skullgirls **NO OFICIAL**. El material que no está autorizado, por ejemplo
elementos, dibujos, imágenes, voces, etc., no se distribuye ni se comparte.

ZGirls Loader es un cargador que lee los archivos de tu copia del juego original. ZDroid-Engine es una
reimplementación no oficial e independiente, compatible con los formatos de archivo del juego; no contiene código
ni material del original. El material no debe respaldarse ni distribuirse junto con una versión que incluya los
archivos originales. Todos los materiales pertenecen a Lab Zero y a sus respectivos autores.

Al ingresar al Loader, USTED acepta que:

- Tengo una copia legal y NO pirata del juego (edición de Steam u otra edición oficial).
- NO distribuiré los archivos del juego ni copias alteradas de ellos (mods o hacks que incluyan material del juego).
- No usaré archivos corruptos ni dañaré el APK para afectar el rendimiento o volverlo fácilmente hackeable.

Si NO tienes una copia del juego NO debes poseer este Loader: debes comprar Skullgirls 2nd Encore en Steam (o gastar
más dinero en su versión "mobile") y jugarlo por la vía legal.

## English

This is an **UNOFFICIAL** Skullgirls port attempt. Material that is not authorised, for example elements,
drawings, images, voices, etc., is not being distributed or shared.

ZGirls Loader is a loader that reads the files of your copy of the original game. ZDroid-Engine is an
unofficial, independent reimplementation that is compatible with the game's file formats; it contains no code or
material from the original. The material must not be backed up or distributed together with a version that
includes the original files. All materials belong to Lab Zero and their respective authors.

By entering the Loader, YOU accept that:

- I own a legal, NON-pirated copy of the game (Steam edition or another official edition).
- I will NOT distribute the game's files, nor altered copies of them (mods or hacks that include game material).
- I will not use corrupted files, nor damage the APK to hurt performance or make it easy to hack.

If you do NOT own a copy of the game you must NOT have this Loader: buy Skullgirls 2nd Encore on Steam (or spend
more on its "mobile" version) and play it the legal way.

## What the project does and does not do (facts, for whoever reads the repo)

- It ships **no** game content: no sprites, art, audio, text, `.gfs` packages, or localisation files. The apk and
  the source zip contain code only. Everything on screen is decoded at run time from folders the user supplies.
- It reads the user's **own** extracted files, or downloads them with the user's **own** Steam account through
  DepotDownloader (Steam decides ownership; there is no anonymous, shared or leaked-manifest path).
- It does not touch DRM, anticheat, or the original executable. It does not talk to Skullgirls' online services;
  online entries in the menu are shown dimmed with a note.
- It is an independent reimplementation written from reverse-engineering notes (`docs/RE_NOTES.md`). It contains no
  code from Lab Zero's engine.

### A note on the wording (updated)

An earlier version called ZDroid "a fork" that uses "the original Z-Engine". That was inaccurate for this repository
(ZDroid is a from-scratch reimplementation that reads the game's file formats, with no Lab Zero source) and could be
read as saying original code was used, so `dis.2` now says "unofficial, independent reimplementation". The mods line
(`dis.b`) used to forbid distributing *any* altered content, which contradicted the mod loader (`docs/MODS.md`,
`docs/MODLOADER.md`); it now forbids distributing the game's own files or altered copies of them, and mods made of
your own material are not covered. Both are one-string changes in `own()` (`src/app/menu.cpp`).

The title screen also shows the game's own copyright line from the user's `Copyright` localisation key, which names
the rights holder the game itself names. The notice says "Lab Zero" because that is what was asked for.
