# ZGirlsMobile - Android build (arm64-v8a)

Builds the same C++ code as the desktop viewer: `libmain.so` (the loader, exports `SDL_main`) +
`libSDL2.so`, glued by SDL's own Java `SDLActivity` (compiled straight from the SDL checkout).

## Requirements

* JDK 17 (JDK 20 also works)
* Android SDK **platform 34**, **build-tools 34.0.0**, **NDK 26.1.10909125** (`ANDROID_HOME` or `local.properties`)
* an **SDL2 2.30.9** source checkout (`git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git`)
* optional: `libdepotdownloader.so` in `app/src/main/jniLibs/arm64-v8a/` for the Steam screen
  (see `../tools/build_depotdownloader_android.sh`; **not** verified - NuGet/bionic runtime pack was never built)

## Build

```
cd android
./gradlew -PsdlDir=/path/to/SDL assembleDebug      # or set sdlDir=... in gradle.properties
```

Apk: `app/build/outputs/apk/debug/app-debug.apk`
Gradle wrapper pins 8.9; the Android Gradle Plugin is 8.7.3 (see `build.gradle`).

## Build record (carried over from an earlier session)

> The table below was written in an earlier session and could not be re-run in the latest sandbox: its egress policy blocks
> `dl.google.com`, Gradle and Maven (HTTP 403), so no SDK/NDK/AGP can be downloaded there. The later changes (launcher icon
> from `branding/logo_1024.png`, landscape-only: manifest `sensorLandscape` + `SDL_IOS_ORIENTATIONS` hint, notch theme)
> were only checked offline (XML well-formed, referenced resources exist, desktop build compiles). Build it with the
> GitHub Actions workflow or locally to confirm.

### Earlier record (SDK 34 / NDK 26.1.10909125 / Gradle 8.9 / AGP 8.7.3)

| check | result |
|-------|--------|
| `assembleDebug` | BUILD SUCCESSFUL (SDK 34 / NDK 26.1.10909125 / Gradle 8.9 / AGP 8.7.3 / JDK 20) |
| native libs in the apk | `lib/arm64-v8a/libmain.so`, `libSDL2.so`, `libc++_shared.so` |
| `libmain.so` symbols / deps | exports `SDL_main`; `NEEDED libSDL2.so, libc++_shared.so, liblog.so` |
| SDL JNI wiring | `libSDL2.so` exports 52 `Java_org_libsdl_app_*` symbols incl. `nativeRunMain`, `nativeSetupJNI` |
| the Play screen is inside the apk | `libmain.so` strings: `PLAY - CHOOSE YOUR FIGHTERS`, `CPU SKILL: *`, `TRAINING`, `DUMMY` |
| manifest | package `org.bodriodroid.zgirlsmobile` (antes `org.bodriodroid.sgloader`), minSdk 24, targetSdk 34, INTERNET only (the storage permissions were dropped in 0.9) |

The module needs `android/app/build.gradle` and `android/app/CMakeLists.txt` (both in the repo): the first
one also compiles SDL's own Java (`SDLActivity`) straight out of the checkout, the second one includes the
top-level `CMakeLists.txt` so `libmain.so` is the same C++ as the desktop viewer.

**Not verified:** running on a real device (no device/emulator in the sandbox), and the DepotDownloader
child process (needs the bionic build above). Everything else is the same code path as the desktop build.

## Using it on a device

1. Install the apk. It asks for **no storage permission** (the only permission is INTERNET, for the Steam screen).
2. On first start the **Core setup** screen asks for your game's `core` folder. Pick it with Android's own folder picker:
   the app copies it into its private storage (`files/core_import`), the loader seals the copy into one encrypted file
   (`files/fx/core.vault`, key = `pafx.key` of this install) and deletes the plain copy. Your original files are never
   changed. "Continue without Core" skips this; **long-press the app icon -> "Change Core folder"** to redo it later.
   Details: `docs/CORE_VAULT.md`.
3. The rest of the data (characters, sprites, stages ...) goes in the app's own folder, `Android/data/org.bodriodroid.zgirlsmobile/files`
   (no permission needed there; a file manager / USB reaches it). That folder is the default data folder of the Home screen.
   A path outside it (e.g. `/sdcard/sg_dump`) can no longer be read: earlier builds asked for "all files access" for that, this one
   deliberately does not.
4. The Steam screen only works if `libdepotdownloader.so` was bundled; it runs the downloader from the app's
   `nativeLibraryDir` (hence `useLegacyPackaging = true` in `app/build.gradle`).

## Signing (developer name "Destroyer And Nitro")

The release APK is signed with a certificate whose subject is `CN=Destroyer And Nitro, O=Destroyer And Nitro`; the same name is on the
notice screen and the Core setup screen. CI makes the key like this:

* with the repository secrets `ZG_KEYSTORE_B64` + `ZG_KEYSTORE_PASSWORD` (+ optional `ZG_KEY_ALIAS`, `ZG_KEY_PASSWORD`) it signs with
  **your stable key**, so every build installs over the previous one. `tools/make_release_keystore.sh` makes that key and prints the values;
* without them it makes a key with the same name **for that build only** (the APK is right, but it must be uninstalled before the next one).

Locally: `./gradlew -PsdlDir=... -PzgStoreFile=... -PzgStorePassword=... -PzgKeyAlias=... -PzgKeyPassword=... assembleRelease`.

What a name can and cannot do: it is what the installer shows as "signed by". It does **not** make Android's "unknown developer" /
Play Protect warning go away by itself. Since September 2026 Google's *Android developer verification* ties a package name + signing key
to a registered developer (Android Developer Console; there is a free "limited distribution" account for hobbyists, up to 20 devices, no
ID or fee); an app not registered there still installs through the "advanced flow" on certified devices. To be "known", register
`org.bodriodroid.zgirlsmobile` with the SHA-256 of the signing certificate (`apksigner verify --print-certs`) there.

## Como sacar la APK (es)

1. **GitHub (lo mas facil, no instalas nada):** crea un repo, sube el contenido de esta carpeta, ve a *Actions -> android-apk ->
   Run workflow*. Al terminar, descarga el artefacto `zgirlsmobile-debug-apk` (adentro esta `app-debug.apk`) e instalala en el telefono.
2. **Android Studio:** abre la carpeta `android/`, instala SDK 34 + NDK 26.1.10909125, descarga SDL2 2.30.9
   (`git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git`) y ejecuta
   `./gradlew -PsdlDir=/ruta/a/SDL assembleDebug`.

La app es solo horizontal (landscape, ambos sentidos) y usa el logo de `branding/` como icono.
