# Core vault - the player's own copy of the Core, sealed inside the app

The Android build used to ask for "all files access" so it could read the game's `core` folder from wherever it lived. That permission
is what makes an app look like something it is not, so it is gone. Instead:

1. **Setup screen** (`CoreSetupActivity`, the first screen). The player picks the **game folder** once with Android's folder picker (no
   permission): the one that holds `core`, `ui`, `stages`, `characters` ... (the screen looks up to two levels down for it), or just the Core.
   The Core part (the folder with `shaders/` or `stages.ini` / `music.ini` / `toc.ini`, > 192 MB or > 64 MB per file is refused) takes the vault
   path below; **everything else is copied as it is to `files/data`**, which the loader mounts next to the vault (menus, stages, textures,
   characters). Files already copied (same name and size) are skipped, `.exe`/`.dll`/archives are not copied, there is a progress line, a
   cancel button and a free-space check; limit 8 GB / 200 000 files so a whole drive is not copied by mistake. A folder that looks like
   nothing of the game is refused with a reason before anything is kept.
2. **Copy.** The picked files are copied to the app's private storage, `files/core_import` (through `core_import.tmp`, so a half copy is
   never seen). The originals are only read.
3. **Seal** (`src/core/vault.cpp`, on the next start of the loader). `vault::importPending` packs the copy into `files/fx/core.vault`
   and **deletes the plain copy**, whether it worked or not (a bad copy would only fail again on every start). The container is the PAFX
   one (`docs/PAFX.md`): ChaCha20-Poly1305, a sealed table and sealed entries, every entry bound to its slot, with the per-install key
   `files/fx/pafx.key` that the effects pack already uses. A vault copied to another phone does not open; one flipped byte is refused,
   not read. The outcome is written to `files/fx/core_import_status.txt` for the setup screen.
4. **Read.** `Vfs::mountVault` mounts the vault like a folder (same suffix rules); entries get virtual paths `vault:<id>/<name>`, which
   `readFile()` understands, so the code that reads Core files does not know the difference. The vault is the lowest layer: any folder or mod
   mounted after it overrides it. The effects pack reads its shaders out of the vault too (same fingerprint as a plain folder, so it is not
   rebuilt when the source changes between a folder and the vault).

What it is and is not:

* It is private storage **plus** encryption at rest with a key that only exists on that install. It keeps the player's copy out of reach
  of other apps and out of backups (`allowBackup=false`), and it means no loose game files are left lying around on the phone.
* It is not a way to ship game data: the vault is made on the device from the player's own files and is never part of the repository, the
  public source zips or the APK. Anyone who can read the app's private storage *and* the key (root) can of course read it; this is about not
  being a loose-files app, not about defeating the owner of the phone.
* The rest of the data (menus, stages, textures, characters) is not in the vault: it is a plain copy in the app's private folder
  (`files/data`), or whatever the player put by hand in `Android/data/<package>/files`. Mount order, lowest first: vault, `files/data`,
  the app's data folder, extra folders, mods.

Tests: `tests/test_vault.cpp` (`zdroid_vault_tests`): round trip, no plaintext in the file, wrong key / missing key / damaged table / truncated /
one flipped entry, the virtual file system and its layering, the shaders and effects pack from the vault, the staging import (good, wrong
folder, replacement). With `ZDROID_TEST_DATA` pointing at a folder with a real Core it seals it and compares every file byte for byte.
