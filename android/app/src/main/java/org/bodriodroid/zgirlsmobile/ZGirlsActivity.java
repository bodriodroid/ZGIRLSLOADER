package org.bodriodroid.zgirlsmobile;

import org.libsdl.app.SDLActivity;

/**
 * ZGirlsMobile - SDL entry point. Loads libSDL2.so + libmain.so (the loader).
 *
 * Where the game's data comes from is decided by CoreSetupActivity (the screen that starts first): a private copy (the Core sealed by the
 * loader into one encrypted file, src/core/vault.cpp; no storage permission) or, if the player chose it, the game folder read where it is
 * with Android's all-files access.  Anything else the loader reads lives in the app's own folder (Android/data/<package>/files).
 */
public class ZGirlsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }
}
