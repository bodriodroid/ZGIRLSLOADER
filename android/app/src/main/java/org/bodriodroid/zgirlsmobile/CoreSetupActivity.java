package org.bodriodroid.zgirlsmobile;

import android.Manifest;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.StatFs;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * First screen of ZGirlsMobile: where the player's own copy of the game comes from.
 *
 * Two ways to give the loader the player's own copy of the game; the player chooses.
 *
 * (1) Private copy - asks for NO storage permission.  The player picks the game folder once with Android's own folder picker (the folder
 * that holds core, ui, stages, characters ... or just the Core).  This screen copies it into the app's private storage:
 *
 *  - the Core  -> files/core_import, which the loader seals into one encrypted file, files/fx/core.vault, with a key that only exists on
 *                 this install, and then deletes the plain copy (src/core/vault.cpp);
 *  - the rest  -> files/data (ui, stages, textures, characters ...), which the loader mounts next to the vault.
 *
 * Nothing outside the app's own folders is read or changed, and the player's original files are left exactly as they were.  Files that
 * are already copied (same name and size) are skipped, so picking the folder again after an update or an interrupted copy is quick.
 *
 * (2) All files access - the player grants Android's "all files access" (Android 11+: the system page for this app; Android 7-10: the
 * storage permission) and picks the folder; the loader then reads the game where it is (no copy, no extra space) and can write next to
 * it (mods.ini, Steam downloads).  Only the folder's real path is stored (files/dataroot.txt); the permission can be taken back at any
 * time in Android's settings, and the loader falls back to the app's own folder when the folder can no longer be read.
 *
 * Shown on a fresh install, when the last import failed, or from the "Change game folder" shortcut (long-press the app icon).
 * "Continue without" starts the game as before (it then runs with whatever the app's own folder holds).
 */
public class CoreSetupActivity extends Activity {
    static final String ACTION_REIMPORT = "org.bodriodroid.zgirlsmobile.REIMPORT";
    static final String DEVELOPER = "Destroyer And Nitro";

    private static final int REQ_PICK = 4201;
    private static final int REQ_ALLFILES = 4202;      // the system "all files access" page came back
    private static final int REQ_PICK_INPLACE = 4203;  // the folder picker of the all-files mode
    private static final int REQ_PERM = 4204;          // Android 7-10: the storage permission dialog
    private static final String PREFS = "zg_setup";
    private static final String KEY_SKIPPED = "skipped2";   // v2: builds before the data copy asked only for the Core

    // a real Core is ~11 MB / ~900 files; anything near these limits is the wrong folder
    private static final long MAX_CORE_FILE_BYTES = 64L << 20;
    private static final long MAX_CORE_BYTES = 192L << 20;
    private static final int MAX_CORE_FILES = 20000;
    // the rest of the game's data
    private static final long MAX_DATA_BYTES = 8L << 30;
    private static final int MAX_DATA_FILES = 200000;
    private static final int MAX_DEPTH = 12;

    // things a loader never reads (installers, executables, archives): skipped when copying the data
    private static final String[] SKIP_EXT = { ".exe", ".dll", ".bat", ".msi", ".iso", ".zip", ".7z", ".rar", ".apk" };
    // folders that tell "this is the game's data" (besides a Core)
    private static final String[] DATA_DIRS = { "ui", "stages", "textures", "characters", "sprites", "sounds", "music", "mods", "levels" };

    private TextView status;
    private Button pick, inPlace, skip, remove, cancel;
    private boolean wantInPlace;   // the player pressed "use my folder without copying": carry on to the folder picker once the permission is in
    private volatile boolean busy;
    private volatile boolean cancelled;

    // ---------------------------------------------------------------- life cycle

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        boolean reimport = ACTION_REIMPORT.equals(getIntent() == null ? null : getIntent().getAction());
        if (!reimport && !needsSetup()) {
            startGame();
            return;
        }
        buildUi();
        showIdleStatus();
    }

    /** A fresh install (nothing copied yet, not skipped before), or the last import failed and there is still no vault. */
    private boolean needsSetup() {
        if (externalHasData()) return false;   // the app's own folder already holds the game: nothing to ask
        if (hasAllFiles() && inPlaceRoot() != null) return false;   // the game is read where it is
        if (vaultFile().isFile() && dataDoneFile().isFile()) return false;
        if (!vaultFile().isFile() && lastStatus().startsWith("error ")) return true;
        return !prefs().getBoolean(KEY_SKIPPED, false);
    }

    /** Game data the player put in Android/data/<package>/files by hand (it needs no permission): the old way still works. */
    private boolean externalHasData() {
        File ext = getExternalFilesDir(null);
        if (ext == null) return false;
        for (String n : new String[] { "core", "ui", "characters" })
            if (new File(ext, n).isDirectory()) return true;
        return false;
    }

    private SharedPreferences prefs() {
        return getSharedPreferences(PREFS, MODE_PRIVATE);
    }

    private File storeDir() {
        return new File(getFilesDir(), "fx");
    }

    private File vaultFile() {
        return new File(storeDir(), "core.vault");
    }

    private File importDir() {
        return new File(getFilesDir(), "core_import");
    }

    private File dataDir() {
        return new File(getFilesDir(), "data");
    }

    private File dataDoneFile() {
        return new File(dataDir(), ".import_done");
    }

    /** The one-line result the loader left after the last sealing ("ok ..." / "error ..."), or "". */
    private String lastStatus() {
        File f = new File(storeDir(), "core_import_status.txt");
        if (!f.isFile()) return "";
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"))) {
            String line = r.readLine();
            return line == null ? "" : line;
        } catch (IOException e) {
            return "";
        }
    }

    private void startGame() {
        startActivity(new Intent(this, ZGirlsActivity.class));
        finish();
    }

    // ---------------------------------------------------------------- all files access (the game is read where it is)

    /** Android 11+: "all files access" for this app.  Android 7-10: the storage permission (read and write). */
    private boolean hasAllFiles() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
                && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    /** Opens Android's own page (Android 11+) or dialog (older) for the permission; the answer comes back to the two callbacks below. */
    private void requestAllFiles() {
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())),
                        REQ_ALLFILES);
            } catch (Exception e) {
                try {   // a phone without the per-app page: the general list, the player finds this app in it
                    startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), REQ_ALLFILES);
                } catch (Exception e2) {
                    wantInPlace = false;
                    setStatus(t("Este teléfono no tiene la pantalla de acceso a todos los archivos.", "This phone has no all-files-access screen."));
                }
            }
        } else {
            requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE }, REQ_PERM);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQ_PERM) afterPermission();
    }

    /** The permission question has been answered (either way): carry on to the folder picker, or say what is missing. */
    private void afterPermission() {
        if (!wantInPlace) {
            showIdleStatus();
            return;
        }
        wantInPlace = false;
        if (!hasAllFiles()) {
            setStatus(t("No se concedió el acceso a todos los archivos. Sin él no puedo leer tu carpeta donde está; puedes usar \"Elegir carpeta del juego\" (copia privada, sin permisos).",
                    "All files access was not granted. Without it I cannot read your folder where it is; you can use \"Choose game folder\" (private copy, no permission)."));
            return;
        }
        openInPlacePicker();
    }

    private void openInPlacePicker() {
        try {
            startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQ_PICK_INPLACE);
        } catch (Exception e) {
            setStatus(t("Este teléfono no tiene selector de carpetas.", "This phone has no folder picker."));
        }
    }

    /** files/dataroot.txt is where the loader looks for the data folder (one line, a real path). */
    private File dataRootFile() {
        return new File(getFilesDir(), "dataroot.txt");
    }

    /** The folder dataroot.txt names, when it is a game folder that can be read right now (so only with the permission); else null. */
    private File inPlaceRoot() {
        File f = dataRootFile();
        if (!f.isFile()) return null;
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"))) {
            String line = r.readLine();
            if (line == null || line.trim().isEmpty()) return null;
            File d = new File(line.trim());
            String own = getFilesDir().getPath();
            File ext = getExternalFilesDir(null);
            if (d.getPath().startsWith(own) || (ext != null && d.getPath().startsWith(ext.getPath()))) return null;   // the app's own folders are the copy mode
            return looksLikeGame(d) ? d : null;
        } catch (IOException e) {
            return null;
        }
    }

    private void writeDataRoot(File root) throws IOException {
        File tmp = new File(getFilesDir(), "dataroot.txt.tmp");
        try (OutputStreamWriter w = new OutputStreamWriter(new FileOutputStream(tmp), "UTF-8")) {
            w.write(root.getAbsolutePath() + "\n");
        }
        File out = dataRootFile();
        out.delete();
        if (!tmp.renameTo(out)) {
            tmp.delete();
            throw new IOException("cannot store the folder");
        }
    }

    /** The real path of a folder the player picked with the system picker (internal storage, SD card, "raw:" ids); null for any other provider. */
    @SuppressWarnings("deprecation")
    private static String treeToPath(Uri tree) {
        String id;
        try {
            id = DocumentsContract.getTreeDocumentId(tree);
        } catch (Exception e) {
            return null;
        }
        if (id == null) return null;
        if (id.startsWith("raw:")) return id.substring(4);
        int c = id.indexOf(':');
        if (c <= 0) return null;
        String vol = id.substring(0, c), rel = id.substring(c + 1);
        File base = vol.equalsIgnoreCase("primary") ? Environment.getExternalStorageDirectory() : new File("/storage/" + vol);
        if (!base.isDirectory()) return null;
        try {
            String bp = base.getCanonicalPath();
            String cp = (rel.isEmpty() ? base : new File(base, rel)).getCanonicalPath();
            // a document id can never lead out of its volume
            return cp.equals(bp) || cp.startsWith(bp + "/") ? cp : null;
        } catch (IOException e) {
            return null;
        }
    }

    /** A folder that holds the game's data (a Core, or one of the usual data folders). */
    private static boolean looksLikeGame(File d) {
        File[] kids = d.listFiles();
        if (kids == null) return false;
        for (File k : kids) {
            String n = lower(k.getName());
            if (k.isDirectory()) {
                if (n.equals("core") || n.equals("shaders")) return true;
                for (String x : DATA_DIRS)
                    if (n.equals(x)) return true;
            } else if (n.equals("stages.ini") || n.equals("music.ini") || n.equals("toc.ini")) {
                return true;
            }
        }
        return false;
    }

    /** `dir` itself, or the first game folder one or two levels below it (the way extractions nest the game); null when there is none. */
    private static File findGameRootFile(File dir) {
        if (looksLikeGame(dir)) return dir;
        File[] a = dir.listFiles();
        if (a == null) return null;
        for (File c : a)
            if (c.isDirectory() && looksLikeGame(c)) return c;
        for (File c : a) {
            if (!c.isDirectory()) continue;
            File[] b = c.listFiles();
            if (b == null) continue;
            for (File c2 : b)
                if (c2.isDirectory() && looksLikeGame(c2)) return c2;
        }
        return null;
    }

    /** The player picked a folder in the all-files mode: store its path and start the game on it. */
    private void useFolderInPlace(Uri tree) {
        String path = treeToPath(tree);
        if (path == null) {
            setStatus(t("No pude saber la ruta de esa carpeta (¿viene de otra app de archivos?). Elige una carpeta del almacenamiento del teléfono o de la tarjeta SD, o usa \"Elegir carpeta del juego\" (copia privada).",
                    "I could not tell the path of that folder (does it come from another files app?). Pick a folder from the phone's storage or SD card, or use \"Choose game folder\" (private copy)."));
            return;
        }
        File dir = new File(path);
        if (dir.listFiles() == null) {
            setStatus(t("No puedo leer esa carpeta: Android no me dio el acceso a todos los archivos.", "I cannot read that folder: Android did not give me all files access."));
            return;
        }
        File root = findGameRootFile(dir);
        if (root == null) {
            setStatus(t("Esa carpeta no parece el juego (no encuentro core, ui, stages, characters...). Elige la carpeta que los contiene.",
                    "That folder does not look like the game (no core, ui, stages, characters ...). Pick the folder that holds them."));
            return;
        }
        try {
            writeDataRoot(root);
        } catch (IOException e) {
            setStatus(t("No se pudo guardar la carpeta elegida.", "Could not store the chosen folder."));
            return;
        }
        new File(storeDir(), "core_import_status.txt").delete();   // an old failure must not bring this screen back
        prefs().edit().remove(KEY_SKIPPED).apply();
        startGame();
    }

    // ---------------------------------------------------------------- UI (built in code: no layout resources to keep in step)

    private boolean spanish() {
        return "es".equals(Locale.getDefault().getLanguage());
    }

    private String t(String es, String en) {
        return spanish() ? es : en;
    }

    private int dp(int v) {
        return Math.round(v * getResources().getDisplayMetrics().density);
    }

    private TextView text(String s, int sp, int color, boolean bold) {
        TextView tv = new TextView(this);
        tv.setText(s);
        tv.setTextSize(sp);
        tv.setTextColor(color);
        if (bold) tv.setTypeface(tv.getTypeface(), android.graphics.Typeface.BOLD);
        tv.setPadding(0, dp(6), 0, dp(6));
        return tv;
    }

    private Button button(String s) {
        Button b = new Button(this);
        b.setText(s);
        b.setAllCaps(false);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.topMargin = dp(8);
        b.setLayoutParams(lp);
        return b;
    }

    private void buildUi() {
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        col.setGravity(Gravity.CENTER_HORIZONTAL);
        col.setPadding(dp(24), dp(32), dp(24), dp(24));

        col.addView(text("ZGirlsMobile", 26, Color.WHITE, true));
        col.addView(text(t("Tu copia del juego", "Your copy of the game"), 18, Color.rgb(0xE8, 0xC5, 0xFF), true));
        col.addView(text(t(
                "Elige una sola vez la carpeta de tu juego (la que contiene core, ui, stages, characters...). La app hace una copia dentro de su "
                        + "propia carpeta privada: el Core se guarda cifrado con una clave que solo existe en este teléfono y el resto "
                        + "(menús, escenarios, personajes) en la carpeta privada de la app. No pide permiso de almacenamiento, no toca ni borra tus "
                        + "archivos originales y no sale nada de aquí. Ocupa lo mismo que los datos; si ya copiaste algo, no se repite.\n\n"
                        + "Si prefieres no copiar, usa tu carpeta donde está: Android te pedirá \"acceso a todos los archivos\" para esta app (la app lo "
                        + "usa solo para leer tu carpeta del juego y escribir mods/descargas junto a ella; puedes quitarlo cuando quieras en los ajustes de Android).",
                "Pick your game folder once (the one that holds core, ui, stages, characters...). The app makes a copy inside its own private "
                        + "folder: the Core is kept encrypted with a key that only exists on this phone, the rest (menus, stages, characters) in the "
                        + "app's private folder. It asks for no storage permission, never changes or deletes your original files, and nothing leaves "
                        + "the device. It takes as much space as the data; what is already copied is not copied again.\n\n"
                        + "If you would rather not copy, use your folder where it is: Android will ask for \"all files access\" for this app (the app "
                        + "uses it only to read your game folder and write mods/downloads next to it; you can take it back any time in Android's settings)."), 15, Color.rgb(0xDD, 0xDD, 0xDD), false));

        pick = button(t("Elegir carpeta del juego", "Choose game folder"));
        pick.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (busy) return;
                Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                try {
                    startActivityForResult(i, REQ_PICK);
                } catch (Exception e) {
                    setStatus(t("Este teléfono no tiene selector de carpetas.", "This phone has no folder picker."));
                }
            }
        });
        col.addView(pick);

        inPlace = button(t("Usar mi carpeta sin copiar (acceso a todos los archivos)", "Use my folder without copying (all files access)"));
        inPlace.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (busy) return;
                wantInPlace = true;
                if (hasAllFiles()) afterPermission();
                else requestAllFiles();
            }
        });
        col.addView(inPlace);

        skip = button(t("Continuar sin copiar", "Continue without copying"));
        skip.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (busy) return;
                new File(storeDir(), "core_import_status.txt").delete();   // an old failure must not bring this screen back
                prefs().edit().putBoolean(KEY_SKIPPED, true).apply();
                startGame();
            }
        });
        col.addView(skip);

        remove = button(t("Borrar lo copiado (Core cifrado y datos)", "Delete what was copied (encrypted Core and data)"));
        remove.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (busy) return;
                setBusy(true);
                setStatus(t("Borrando…", "Deleting…"));
                new Thread(new Runnable() {
                    @Override
                    public void run() {
                        vaultFile().delete();
                        new File(storeDir(), "core_import_status.txt").delete();
                        deleteTree(importDir());
                        deleteTree(dataDir());
                        prefs().edit().putBoolean(KEY_SKIPPED, true).apply();
                        setBusy(false);
                        runOnUiThread(new Runnable() {
                            @Override
                            public void run() {
                                showIdleStatus();
                            }
                        });
                    }
                }).start();
            }
        });
        col.addView(remove);

        cancel = button(t("Cancelar", "Cancel"));
        cancel.setVisibility(View.GONE);
        cancel.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                cancelled = true;
            }
        });
        col.addView(cancel);

        status = text("", 14, Color.rgb(0xFF, 0xE0, 0x82), false);
        status.setGravity(Gravity.CENTER_HORIZONTAL);
        col.addView(status);

        TextView dev = text(t("Desarrollador: ", "Developer: ") + DEVELOPER, 12, Color.GRAY, false);
        dev.setGravity(Gravity.CENTER_HORIZONTAL);
        col.addView(dev);

        ScrollView sv = new ScrollView(this);
        sv.setBackgroundColor(Color.rgb(0x14, 0x10, 0x1C));
        sv.addView(col);
        setContentView(sv);
    }

    private void setStatus(final String s) {
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (status != null) status.setText(s);
            }
        });
    }

    private void setBusy(final boolean b) {
        busy = b;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                if (pick != null) pick.setEnabled(!b);
                if (inPlace != null) inPlace.setEnabled(!b);
                if (skip != null) skip.setEnabled(!b);
                if (remove != null) remove.setEnabled(!b);
                if (cancel != null) cancel.setVisibility(b ? View.VISIBLE : View.GONE);
            }
        });
    }

    private void showIdleStatus() {
        boolean has = vaultFile().isFile() || dataDoneFile().isFile();
        String last = lastStatus();
        File inPlaceDir = hasAllFiles() ? inPlaceRoot() : null;
        if (remove != null) remove.setVisibility(has ? View.VISIBLE : View.GONE);
        if (inPlaceDir != null) setStatus(t("Leyendo el juego en: ", "Reading the game from: ") + inPlaceDir.getPath());
        else if (has) setStatus(t("Ya hay una copia en este teléfono. Puedes actualizarla eligiendo la carpeta otra vez.",
                "A copy is already stored on this phone. You can update it by picking the folder again."));
        else if (last.startsWith("error ")) setStatus(t("La última importación falló: ", "The last import failed: ") + last.substring(6));
        else setStatus("");
    }

    // ---------------------------------------------------------------- picking and copying

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_ALLFILES) {   // the settings page always answers "cancelled": what counts is whether the permission is in now
            afterPermission();
            return;
        }
        if (requestCode == REQ_PICK_INPLACE) {
            if (resultCode == RESULT_OK && data != null && data.getData() != null) useFolderInPlace(data.getData());
            return;
        }
        if (requestCode != REQ_PICK || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        cancelled = false;
        setBusy(true);
        setStatus(t("Mirando la carpeta…", "Looking at the folder…"));
        new Thread(new Runnable() {
            @Override
            public void run() {
                String err = null;
                try {
                    copyGame(tree);
                } catch (Failure f) {
                    err = f.getMessage();
                } catch (Exception e) {
                    err = String.valueOf(e.getMessage());
                }
                setBusy(false);
                if (err != null) {
                    setStatus(err);
                    return;
                }
                // the loader seals the Core on its next start
                prefs().edit().remove(KEY_SKIPPED).apply();
                runOnUiThread(new Runnable() {
                    @Override
                    public void run() {
                        startGame();
                    }
                });
            }
        }).start();
    }

    /** A problem the player can act on (wrong folder, too big, no space ...): the message is shown as is. */
    private static final class Failure extends Exception {
        Failure(String m) {
            super(m);
        }
    }

    private static final class Child {
        String id, name;
        boolean dir;
        long size;
    }

    /** One file to copy: where it is, where it goes (relative), how big. */
    private static final class Item {
        String id, rel;
        long size;
    }

    private List<Child> children(Uri tree, String docId) {
        List<Child> out = new ArrayList<Child>();
        Uri kids = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
        Cursor c = null;
        try {
            c = getContentResolver().query(kids, new String[] { DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE,
                    DocumentsContract.Document.COLUMN_SIZE }, null, null, null);
            while (c != null && c.moveToNext()) {
                Child k = new Child();
                k.id = c.getString(0);
                k.name = c.getString(1);
                k.dir = DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2));
                k.size = c.isNull(3) ? -1 : c.getLong(3);
                if (k.id != null && k.name != null) out.add(k);
            }
        } finally {
            if (c != null) c.close();
        }
        return out;
    }

    private static String lower(String s) {
        return s.toLowerCase(Locale.ROOT);
    }

    private static boolean isCore(List<Child> kids) {
        for (Child k : kids) {
            String n = lower(k.name);
            if (k.dir && n.equals("shaders")) return true;
            if (!k.dir && (n.equals("stages.ini") || n.equals("music.ini") || n.equals("toc.ini"))) return true;
        }
        return false;
    }

    private static boolean isGameData(List<Child> kids) {
        if (isCore(kids)) return true;
        for (Child k : kids) {
            if (!k.dir) continue;
            String n = lower(k.name);
            if (n.equals("core")) return true;
            for (String d : DATA_DIRS)
                if (n.equals(d)) return true;
        }
        return false;
    }

    private static Child childDir(List<Child> kids, String name) {
        for (Child k : kids)
            if (k.dir && lower(k.name).equals(name)) return k;
        return null;
    }

    private static boolean unsafeName(String name) {
        // a name that could climb out of the destination folder is never followed
        return name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0;
    }

    private static boolean skippedExt(String name) {
        String n = lower(name);
        for (String e : SKIP_EXT)
            if (n.endsWith(e)) return true;
        return false;
    }

    /** Lists every file below docId (relative paths with '/'), skipping the folder whose document id is `skipId`. */
    private void scan(Uri tree, String docId, String rel, int depth, boolean skipJunk, List<Item> out, String skipId, int maxFiles, long maxBytes,
            long[] total) throws Failure {
        if (cancelled) throw new Failure(t("Cancelado.", "Cancelled."));
        if (depth > MAX_DEPTH) throw new Failure(t("Esa carpeta es demasiado profunda para ser un juego.", "That folder is too deep to be a game."));
        for (Child k : children(tree, docId)) {
            if (unsafeName(k.name) || k.id.equals(skipId)) continue;
            String r = rel.isEmpty() ? k.name : rel + "/" + k.name;
            if (k.dir) {
                scan(tree, k.id, r, depth + 1, skipJunk, out, skipId, maxFiles, maxBytes, total);
                continue;
            }
            if (skipJunk && skippedExt(k.name)) continue;
            Item it = new Item();
            it.id = k.id;
            it.rel = r;
            it.size = k.size;
            out.add(it);
            total[0] += Math.max(0, k.size);
            if (out.size() > maxFiles || total[0] > maxBytes) throw tooBig();
        }
        setStatus(t("Mirando la carpeta… ", "Looking at the folder… ") + out.size());
    }

    /** The shallowest folder (up to two levels down) that looks like the game's data root; its children are returned through kidsOut. */
    private String findGameRoot(Uri tree, String rootId, List<Child>[] kidsOut) {
        List<Child> kids = children(tree, rootId);
        if (isGameData(kids)) {
            kidsOut[0] = kids;
            return rootId;
        }
        for (Child c : kids) {
            if (!c.dir || unsafeName(c.name)) continue;
            List<Child> k2 = children(tree, c.id);
            if (isGameData(k2)) {
                kidsOut[0] = k2;
                return c.id;
            }
        }
        for (Child c : kids) {
            if (!c.dir || unsafeName(c.name)) continue;
            for (Child c2 : children(tree, c.id)) {
                if (!c2.dir || unsafeName(c2.name)) continue;
                List<Child> k3 = children(tree, c2.id);
                if (isGameData(k3)) {
                    kidsOut[0] = k3;
                    return c2.id;
                }
            }
        }
        return null;
    }

    /** Copies the picked game folder: the Core to files/core_import (all or nothing), the rest to files/data (resumable). */
    @SuppressWarnings("unchecked")
    private void copyGame(Uri tree) throws Failure, IOException {
        List<Child>[] holder = new List[1];
        String rootId = findGameRoot(tree, DocumentsContract.getTreeDocumentId(tree), holder);
        if (rootId == null)
            throw new Failure(t("Esa carpeta no parece el juego (no encuentro core, ui, stages, characters...). Elige la carpeta que los contiene.",
                    "That folder does not look like the game (no core, ui, stages, characters ...). Pick the folder that holds them."));
        List<Child> kids = holder[0];

        // the Core: the folder itself, or the "core" folder in it (once or twice, the way extractions nest it)
        String coreId = null;
        if (isCore(kids)) {
            coreId = rootId;
        } else {
            Child c = childDir(kids, "core");
            for (int level = 0; level < 2 && c != null; ++level) {
                List<Child> k = children(tree, c.id);
                if (isCore(k)) {
                    coreId = c.id;
                    break;
                }
                c = childDir(k, "core");
            }
        }

        // what to copy
        List<Item> core = new ArrayList<Item>();
        long[] coreTotal = new long[1];
        if (coreId != null) scan(tree, coreId, "", 0, false, core, null, MAX_CORE_FILES, MAX_CORE_BYTES, coreTotal);
        List<Item> data = new ArrayList<Item>();
        long[] dataTotal = new long[1];
        if (coreId == null || !coreId.equals(rootId))   // the picked folder is the Core itself: nothing else to copy
            scan(tree, rootId, "", 0, true, data, coreId, MAX_DATA_FILES, MAX_DATA_BYTES, dataTotal);
        if (core.isEmpty() && data.isEmpty()) throw new Failure(t("No hay nada que copiar en esa carpeta.", "There is nothing to copy in that folder."));

        // space: what is not copied yet (same name and size = already there)
        long need = coreTotal[0];
        File dd = dataDir();
        for (Item it : data) {
            File f = new File(dd, it.rel);
            if (!(f.isFile() && it.size >= 0 && f.length() == it.size)) need += Math.max(0, it.size);
        }
        dd.mkdirs();
        long free = new StatFs(getFilesDir().getPath()).getAvailableBytes();
        if (need + (64L << 20) > free)
            throw new Failure(t("No hay espacio: hacen falta " + mb(need) + " MB y hay " + mb(free) + " MB libres.",
                    "Not enough space: " + mb(need) + " MB are needed and " + mb(free) + " MB are free."));

        long[] done = new long[2];   // files, bytes
        long[] lastUi = new long[1];
        long all = coreTotal[0] + dataTotal[0];
        int count = core.size() + data.size();

        if (!core.isEmpty()) {
            File tmp = new File(getFilesDir(), "core_import.tmp");
            deleteTree(tmp);
            deleteTree(importDir());
            if (!tmp.mkdirs()) throw new Failure(t("No se pudo crear la carpeta privada de la app.", "Could not create the app's private folder."));
            try {
                for (Item it : core) copyOne(tree, it, new File(tmp, it.rel), MAX_CORE_FILE_BYTES, false, done, lastUi, count, all);
                if (!tmp.renameTo(importDir())) throw new Failure(t("No se pudo guardar la copia.", "Could not store the copy."));
            } finally {
                deleteTree(tmp);   // still there only when something above failed
            }
        }
        if (!data.isEmpty()) {
            dataDoneFile().delete();   // not complete until the loop below ends
            for (Item it : data) copyOne(tree, it, new File(dd, it.rel), Integer.MAX_VALUE * 2L, true, done, lastUi, count, all);
            if (!dataDoneFile().createNewFile() && !dataDoneFile().isFile()) throw new Failure(t("No se pudo terminar la copia.", "Could not finish the copy."));
        }
    }

    private static long mb(long bytes) {
        return bytes >> 20;
    }

    private void copyOne(Uri tree, Item it, File out, long maxFile, boolean skipSame, long[] done, long[] lastUi, int count, long all)
            throws Failure, IOException {
        if (cancelled) throw new Failure(t("Cancelado.", "Cancelled."));
        if (it.size > maxFile) throw tooBig();
        if (skipSame && it.size >= 0 && out.isFile() && out.length() == it.size) {
            done[0]++;
            done[1] += it.size;
            progress(done, lastUi, count, all);
            return;
        }
        File parent = out.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory())
            throw new Failure(t("No se pudo crear una carpeta.", "Could not create a folder."));
        Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, it.id);
        ContentResolver cr = getContentResolver();
        InputStream in = cr.openInputStream(doc);
        if (in == null) throw new Failure(t("No se pudo leer ", "Could not read ") + it.rel);
        File part = new File(out.getPath() + ".part");
        OutputStream os = new FileOutputStream(part);
        boolean ok = false;
        try {
            byte[] buf = new byte[128 * 1024];
            long fileBytes = 0;
            int r;
            while ((r = in.read(buf)) > 0) {
                if (cancelled) throw new Failure(t("Cancelado.", "Cancelled."));
                fileBytes += r;
                if (fileBytes > maxFile) throw tooBig();
                os.write(buf, 0, r);
                done[1] += r;
                progress(done, lastUi, count, all);
            }
            ok = true;
        } finally {
            try {
                in.close();
            } catch (IOException ignored) {
            }
            os.close();
            if (!ok) part.delete();
        }
        out.delete();
        if (!part.renameTo(out)) {
            part.delete();
            throw new Failure(t("No se pudo guardar ", "Could not store ") + it.rel);
        }
        done[0]++;
        progress(done, lastUi, count, all);
    }

    private void progress(long[] done, long[] lastUi, int count, long all) {
        long now = System.currentTimeMillis();
        if (now - lastUi[0] < 300) return;
        lastUi[0] = now;
        setStatus(t("Copiando ", "Copying ") + done[0] + " / " + count + "  (" + mb(done[1]) + " / " + mb(all) + " MB)");
    }

    private Failure tooBig() {
        return new Failure(t("Esa carpeta es demasiado grande o tiene demasiados archivos para ser el juego. Elige la carpeta del juego, no el disco entero.",
                "That folder is far too big (or has far too many files) to be the game. Pick the game folder, not the whole drive."));
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null)
            for (File k : kids) deleteTree(k);
        f.delete();
    }
}
