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
| manifest | package `org.bodriodroid.zgirlsmobile` (antes `org.bodriodroid.sgloader`), minSdk 24, targetSdk 34, INTERNET + storage permissions |

The module needs `android/app/build.gradle` and `android/app/CMakeLists.txt` (both in the repo): the first
one also compiles SDL's own Java (`SDLActivity`) straight out of the checkout, the second one includes the
top-level `CMakeLists.txt` so `libmain.so` is the same C++ as the desktop viewer.

**Not verified:** running on a real device (no device/emulator in the sandbox), and the DepotDownloader
child process (needs the bionic build above). Everything else is the same code path as the desktop build.

## Using it on a device

1. Install the apk. On first start it asks for "all files access" (MANAGE_EXTERNAL_STORAGE); grant it.
2. Put the **unpacked** game data on the device (e.g. `/sdcard/sg_dump/unpacked`). The `.gfs` packages are
   not unpacked by this project yet.
3. Start the app, type that path into the Home screen (soft keyboard) and press enter/rescan.
   `SDL_AndroidGetExternalStoragePath()` is used as the default when nothing is stored yet.
4. The Steam screen only works if `libdepotdownloader.so` was bundled; it runs the downloader from the app's
   `nativeLibraryDir` (hence `useLegacyPackaging = true` in `app/build.gradle`).

## Como sacar la APK (es)

1. **GitHub (lo mas facil, no instalas nada):** crea un repo, sube el contenido de esta carpeta, ve a *Actions -> android-apk ->
   Run workflow*. Al terminar, descarga el artefacto `zgirlsmobile-debug-apk` (adentro esta `app-debug.apk`) e instalala en el telefono.
2. **Android Studio:** abre la carpeta `android/`, instala SDK 34 + NDK 26.1.10909125, descarga SDL2 2.30.9
   (`git clone --depth 1 --branch release-2.30.9 https://github.com/libsdl-org/SDL.git`) y ejecuta
   `./gradlew -PsdlDir=/ruta/a/SDL assembleDebug`.

La app es solo horizontal (landscape, ambos sentidos) y usa el logo de `branding/` como icono.
