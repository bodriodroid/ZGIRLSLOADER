package org.bodriodroid.zgirlsmobile;

import org.libsdl.app.SDLActivity;

/**
 * ZGirlsMobile - SDL entry point. Loads libSDL2.so + libmain.so (the loader).
 *
 * The app needs no storage permission: the player's Core is copied into the app's private folder by CoreSetupActivity (the screen that
 * starts first) and sealed by the loader into one encrypted file (src/core/vault.cpp); anything else the loader reads lives in the app's own
 * folder (Android/data/<package>/files).
 */
public class ZGirlsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
