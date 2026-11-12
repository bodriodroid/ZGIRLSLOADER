package org.bodriodroid.zgirlsmobile;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.provider.DocumentsContract;
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
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * First screen of ZGirlsMobile: where the player's own copy of the game's Core comes from.
 *
 * The app asks for NO storage permission.  The player picks the Core folder once with Android's own folder picker; this screen copies
 * it into the app's private folder (files/core_import) and starts the game.  The loader (src/core/vault.cpp) seals that copy into
 * one encrypted file, files/fx/core.vault, with a key that only exists on this install, and deletes the plain copy.  Nothing outside
 * the app's own folders is read or changed, and the player's original files are left exactly as they were.
 *
 * Shown on a fresh install, when the last import failed, or from the "Change Core folder" shortcut (long-press the app icon).
 * "Continue without Core" starts the game as before (it then runs with whatever the app's own folder holds).
 */
public class CoreSetupActivity extends Activity {
    static final String ACTION_REIMPORT = "org.bodriodroid.zgirlsmobile.REIMPORT";
    static final String DEVELOPER = "Destroyer And Nitro";

    private static final int REQ_PICK = 4201;
    private static final String PREFS = "zg_setup";
    private static final String KEY_SKIPPED = "skipped";

    // what a Core copy may weigh: a real Core is ~11 MB / ~900 files; anything near these limits is the wrong folder
    private static final long MAX_FILE_BYTES = 64L << 20;
    private static final long MAX_TOTAL_BYTES = 192L << 20;
    private static final int MAX_FILES = 20000;
    private static final int MAX_DEPTH = 10;

    private TextView status;
    private Button pick, skip, remove;
    private volatile boolean busy;

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

    /** A fresh install (no vault yet, not skipped before), or the last import failed and there is still no vault. */
    private boolean needsSetup() {
        if (vaultFile().isFile()) return false;
        if (lastStatus().startsWith("error ")) return true;
        return !prefs().getBoolean(KEY_SKIPPED, false);
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
        col.addView(text(t("Tu copia del Core del juego", "Your copy of the game's Core"), 18, Color.rgb(0xE8, 0xC5, 0xFF), true));
        col.addView(text(t(
                "Elige una sola vez la carpeta \"core\" de tu juego. La app hace una copia dentro de su propia carpeta privada y la guarda cifrada "
                        + "con una clave que solo existe en este teléfono. No pide permiso de almacenamiento, no toca ni borra tus archivos originales "
                        + "y no sale nada de aquí.",
                "Pick your game's \"core\" folder once. The app makes a copy inside its own private folder and keeps it encrypted with a key that "
                        + "only exists on this phone. It asks for no storage permission, never changes or deletes your original files, and nothing "
                        + "leaves the device."), 15, Color.rgb(0xDD, 0xDD, 0xDD), false));

        pick = button(t("Elegir carpeta Core", "Choose Core folder"));
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

        skip = button(t("Continuar sin Core", "Continue without Core"));
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

        remove = button(t("Borrar el Core cifrado", "Delete the encrypted Core"));
        remove.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                if (busy) return;
                vaultFile().delete();
                new File(storeDir(), "core_import_status.txt").delete();
                prefs().edit().putBoolean(KEY_SKIPPED, true).apply();
                showIdleStatus();
            }
        });
        col.addView(remove);

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
                if (skip != null) skip.setEnabled(!b);
                if (remove != null) remove.setEnabled(!b);
            }
        });
    }

    private void showIdleStatus() {
        boolean has = vaultFile().isFile();
        String last = lastStatus();
        if (remove != null) remove.setVisibility(has ? View.VISIBLE : View.GONE);
        if (has) setStatus(t("Ya hay un Core cifrado en este teléfono. Puedes cambiarlo eligiendo otra carpeta.",
                "An encrypted Core is already stored on this phone. You can replace it by picking another folder."));
        else if (last.startsWith("error ")) setStatus(t("La última importación falló: ", "The last import failed: ") + last.substring(6));
        else setStatus("");
    }

    // ---------------------------------------------------------------- picking and copying

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_PICK || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        final Uri tree = data.getData();
        setBusy(true);
        setStatus(t("Copiando…", "Copying…"));
        new Thread(new Runnable() {
            @Override
            public void run() {
                String err = null;
                try {
                    copyCore(tree);
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
                // the loader seals the copy on its next start
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

    /** A problem the player can act on (wrong folder, too big ...): the message is shown as is. */
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

    private static final class Counter {
        long bytes;
        int files;
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

    private static boolean isCore(List<Child> kids) {
        for (Child k : kids) {
            String n = k.name.toLowerCase(Locale.ROOT);
            if (k.dir && n.equals("shaders")) return true;
            if (!k.dir && (n.equals("stages.ini") || n.equals("music.ini") || n.equals("toc.ini"))) return true;
        }
        return false;
    }

    private static Child coreChild(List<Child> kids) {
        for (Child k : kids)
            if (k.dir && k.name.toLowerCase(Locale.ROOT).equals("core")) return k;
        return null;
    }

    /** Copies the picked folder (or the "core" folder inside it, up to two levels down) into files/core_import, all or nothing. */
    private void copyCore(Uri tree) throws Failure, IOException {
        String rootId = DocumentsContract.getTreeDocumentId(tree);
        List<Child> kids = children(tree, rootId);
        if (!isCore(kids)) {
            for (int level = 0; level < 2 && !isCore(kids); ++level) {
                Child c = coreChild(kids);
                if (c == null) break;
                rootId = c.id;
                kids = children(tree, rootId);
            }
        }
        if (!isCore(kids))
            throw new Failure(t("Esa carpeta no parece un Core (no tiene la carpeta shaders ni stages.ini / music.ini / toc.ini). "
                    + "Elige la carpeta llamada \"core\".", "That folder does not look like a Core (no shaders folder and no stages.ini / "
                    + "music.ini / toc.ini). Pick the folder named \"core\"."));

        File tmp = new File(getFilesDir(), "core_import.tmp");
        deleteTree(tmp);
        deleteTree(importDir());
        if (!tmp.mkdirs()) throw new Failure(t("No se pudo crear la carpeta privada de la app.", "Could not create the app's private folder."));
        Counter n = new Counter();
        try {
            copyDir(tree, rootId, tmp, 0, n);
            if (!tmp.renameTo(importDir())) throw new Failure(t("No se pudo guardar la copia.", "Could not store the copy."));
        } finally {
            deleteTree(tmp);   // still there only when something above failed
        }
    }

    private void copyDir(Uri tree, String docId, File dest, int depth, Counter n) throws Failure, IOException {
        if (depth > MAX_DEPTH) throw new Failure(t("Esa carpeta es demasiado profunda para ser un Core.", "That folder is too deep to be a Core."));
        ContentResolver cr = getContentResolver();
        for (Child k : children(tree, docId)) {
            String name = k.name;
            // a name that could climb out of the destination folder is never followed
            if (name.isEmpty() || name.equals(".") || name.equals("..") || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) continue;
            File out = new File(dest, name);
            if (k.dir) {
                if (!out.mkdirs() && !out.isDirectory()) throw new Failure(t("No se pudo crear una carpeta.", "Could not create a folder."));
                copyDir(tree, k.id, out, depth + 1, n);
                continue;
            }
            if (k.size > MAX_FILE_BYTES) throw tooBig();
            if (++n.files > MAX_FILES) throw tooBig();
            Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, k.id);
            InputStream in = cr.openInputStream(doc);
            if (in == null) throw new Failure(t("No se pudo leer ", "Could not read ") + name);
            OutputStream os = new FileOutputStream(out);
            try {
                byte[] buf = new byte[64 * 1024];
                long fileBytes = 0;
                int r;
                while ((r = in.read(buf)) > 0) {
                    fileBytes += r;
                    n.bytes += r;
                    if (fileBytes > MAX_FILE_BYTES || n.bytes > MAX_TOTAL_BYTES) throw tooBig();
                    os.write(buf, 0, r);
                }
            } finally {
                try {
                    in.close();
                } catch (IOException ignored) {
                }
                os.close();
            }
        }
    }

    private Failure tooBig() {
        return new Failure(t("Esa carpeta es demasiado grande para ser un Core (¿elegiste la carpeta de todo el juego?). Elige la carpeta llamada \"core\".",
                "That folder is far too big to be a Core (did you pick the whole game folder?). Pick the folder named \"core\"."));
    }

    private static void deleteTree(File f) {
        if (f == null || !f.exists()) return;
        File[] kids = f.listFiles();
        if (kids != null)
            for (File k : kids) deleteTree(k);
        f.delete();
    }
}
