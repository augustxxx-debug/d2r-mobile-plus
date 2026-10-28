// 分享诊断日志用的 content:// 提供者：只开放 files/share/ 下的文件，只读；分享面板里的应用凭 Intent 带的临时权限读它。不依赖 androidx（工程 useAndroidX=false）。
// content:// provider for sharing diagnostics: exposes only files under files/share/, read-only; the app chosen in the share sheet reads it through the Intent's temporary grant. No androidx (useAndroidX=false).
package com.xiaoboluo.d2r;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;

public class DiagProvider extends ContentProvider {
    static String authority(android.content.Context c) { return c.getPackageName() + ".diag"; }
    static Uri uriFor(android.content.Context c, File f) { return new Uri.Builder().scheme("content").authority(authority(c)).path(f.getName()).build(); }
    private File resolve(Uri uri) throws FileNotFoundException {
        String name = uri.getLastPathSegment();
        if (name == null || name.isEmpty() || name.contains("/") || name.startsWith(".")) throw new FileNotFoundException();
        File dir = new File(getContext().getFilesDir(), "share"), f = new File(dir, name);
        try { if (!f.getCanonicalPath().startsWith(dir.getCanonicalPath() + File.separator)) throw new FileNotFoundException(); } catch (IOException e) { throw new FileNotFoundException(); }
        if (!f.isFile()) throw new FileNotFoundException();
        return f;
    }
    @Override public boolean onCreate() { return true; }
    @Override public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        if (!"r".equals(mode)) throw new FileNotFoundException("read-only");
        return ParcelFileDescriptor.open(resolve(uri), ParcelFileDescriptor.MODE_READ_ONLY);
    }
    @Override public String getType(Uri uri) { return "text/plain"; }
    @Override public Cursor query(Uri uri, String[] projection, String selection, String[] args, String sort) {
        File f; try { f = resolve(uri); } catch (FileNotFoundException e) { return null; }
        if (projection == null) projection = new String[] { OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE };
        MatrixCursor c = new MatrixCursor(projection, 1); Object[] row = new Object[projection.length];
        for (int i = 0; i < projection.length; i++) row[i] = OpenableColumns.DISPLAY_NAME.equals(projection[i]) ? f.getName() : OpenableColumns.SIZE.equals(projection[i]) ? f.length() : null;
        c.addRow(row); return c;
    }
    @Override public Uri insert(Uri uri, ContentValues values) { return null; }
    @Override public int delete(Uri uri, String selection, String[] args) { return 0; }
    @Override public int update(Uri uri, ContentValues values, String selection, String[] args) { return 0; }
}
