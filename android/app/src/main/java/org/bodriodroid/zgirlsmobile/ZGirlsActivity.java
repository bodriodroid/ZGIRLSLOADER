package org.bodriodroid.zgirlsmobile;

import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;

import java.io.File;

import org.libsdl.app.SDLActivity;

/**
 * ZGirlsMobile - SDL entry point. Loads libSDL2.so + libmain.so (the loader).
 *
 * "All files" access is only needed to read a game folder that lives outside the app's own storage (e.g. /sdcard/sg_dump).
 * It is therefore asked for at most once per app version, and not at all when the app's own folder
 * (Android/data/<package>/files) already holds a game dump.
 */
public class ZGirlsActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager() && !ownFolderHasGameData()
                && !alreadyAsked()) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (Exception e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        }
    }

    /** True when Android/data/<package>/files already contains something the loader can use (a "core" or "ui" folder, or any .lvl/.dds). */
    private boolean ownFolderHasGameData() {
        File dir = getExternalFilesDir(null);
        if (dir == null || !dir.isDirectory()) return false;
        String[] names = dir.list();
        if (names == null) return false;
        for (String n : names) {
            String l = n.toLowerCase(java.util.Locale.ROOT);
            if (l.equals("core") || l.equals("ui") || l.equals("unpacked") || l.equals("stages") || l.endsWith(".dds") || l.endsWith(".lvl")) return true;
        }
        return false;
    }

    /** Remembers (per versionCode) that the permission screen was shown, so it does not come up on every launch. */
    private boolean alreadyAsked() {
        SharedPreferences sp = getSharedPreferences("zg_permissions", MODE_PRIVATE);
        int version = 0;
        try {
            version = getPackageManager().getPackageInfo(getPackageName(), 0).versionCode;
        } catch (Exception ignored) {
        }
        String key = "allFilesAsked_" + version;
        boolean asked = sp.getBoolean(key, false);
        if (!asked) sp.edit().putBoolean(key, true).apply();
        return asked;
    }
}
