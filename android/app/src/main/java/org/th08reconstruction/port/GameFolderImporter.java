package org.th08reconstruction.port;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.HashSet;
import java.util.Set;

/**
 * Imports the picked game folder into app-private storage with progress.
 *
 * The reconstructed game opens its archives through plain file paths and
 * writes its config, saves and replays next to them, so the working directory
 * has to be readable and writable on every device — which only the app's own
 * storage is under Android's scoped storage. The archives are therefore
 * staged in files/game.new and swapped into place: an interrupted import
 * never destroys a copy that already works.
 *
 * Two passes keep the progress honest: the first walks the document tree and
 * records the wanted files with their reported sizes, the second copies them
 * while bytes are counted against that total. When a provider reports no
 * sizes, progress falls back to files completed over files found so the bar
 * still moves. Every failure path returns a distinct message instead of
 * doing nothing on screen.
 */
final class GameFolderImporter {
    /** What the bar is doing; the values are label string resources. */
    interface Listener {
        /** A new stage starts: a label to show (R.string.*). */
        void stage(int stageRes);

        /** Determinate percentage 0..100, or -1 while the total is unknown. */
        void onProgress(int percent);
    }

    /** The import outcome: a directory path, or an error to display. */
    static final class Result {
        /** The directory the game can run from; null on failure. */
        final String directory;
        /** R.string.* describing the failure; 0 on success. */
        final int errorRes;
        /** Extra detail for the message; may be null. */
        final String detail;

        private Result(String directory, int errorRes, String detail) {
            this.directory = directory;
            this.errorRes = errorRes;
            this.detail = detail;
        }

        static Result ok(String directory) {
            return new Result(directory, 0, null);
        }

        static Result fail(int errorRes, String detail) {
            return new Result(null, errorRes, detail);
        }
    }

    /** One file to copy, addressed by its document URI and relative path. */
    private static final class Entry {
        final Uri uri;
        final String relativePath;
        final long size;

        Entry(Uri uri, String relativePath, long size) {
            this.uri = uri;
            this.relativePath = relativePath;
            this.size = size;
        }
    }

    /** Byte counter that reports percentage changes through the listener. */
    private static final class Progress {
        private final Listener listener;
        private final long totalBytes;
        private final int totalFiles;
        private final boolean byBytes;
        private long copiedBytes;
        private int doneFiles;
        private int lastPercent = -1;

        Progress(Listener listener, long totalBytes, int totalFiles) {
            this.listener = listener;
            this.totalBytes = totalBytes;
            this.totalFiles = Math.max(1, totalFiles);
            this.byBytes = totalBytes > 0;
            listener.onProgress(byBytes ? 0 : -1);
        }

        void bytes(long count) {
            if (!byBytes) return;
            if (count > 0 && copiedBytes <= Long.MAX_VALUE - count) {
                copiedBytes += count;
            } else {
                copiedBytes = totalBytes;
            }
            final long percent = copiedBytes < totalBytes
                    ? ((copiedBytes / totalBytes) * 100L)
                            + (((copiedBytes % totalBytes) * 100L) / totalBytes)
                    : 100L;
            report((int) Math.min(100L, percent));
        }

        void fileDone() {
            doneFiles++;
            if (byBytes) return;
            report((int) Math.min(100L, (long) doneFiles * 100L / totalFiles));
        }

        private void report(int percent) {
            if (percent == lastPercent) return;
            lastPercent = percent;
            listener.onProgress(percent);
        }
    }

    private final ContentResolver resolver;

    GameFolderImporter(ContentResolver resolver) {
        this.resolver = resolver;
    }

    /**
     * Copies the wanted files out of the picked tree into filesDir/game.
     * Runs on the caller's (worker) thread; listener callbacks arrive there
     * too and are expected to hop to the UI thread themselves.
     */
    Result importTree(Uri tree, File filesDir, Listener listener) {
        final File staging = new File(filesDir, "game.new");
        final File destination = new File(filesDir, "game");
        try {
            listener.stage(R.string.status_working);
            final List<Entry> entries = new ArrayList<>();
            collect(tree, DocumentsContract.getTreeDocumentId(tree), "", entries, new HashSet<String>());

            boolean foundData = false;
            for (Entry entry : entries) {
                if (entry.relativePath.equalsIgnoreCase("th08.dat")) {
                    foundData = true;
                    break;
                }
            }
            if (!foundData) {
                // The clearest failure: the user picked a folder one level up
                // or down from the installation directory.
                return Result.fail(R.string.status_invalid_folder, null);
            }

            long totalBytes = 0;
            for (Entry entry : entries) {
                if (entry.size > 0) {
                    if (totalBytes <= Long.MAX_VALUE - entry.size) {
                        totalBytes += entry.size;
                    } else {
                        totalBytes = 0;
                        break;
                    }
                }
            }

            deleteRecursively(staging);
            listener.stage(R.string.status_copying);
            final Progress progress = new Progress(listener, totalBytes, entries.size());
            for (Entry entry : entries) {
                copyFile(entry, new File(staging, entry.relativePath), progress);
                progress.fileDone();
            }
            listener.onProgress(100);

            swapStagedDirectory(staging, destination);
            return Result.ok(destination.getAbsolutePath());
        } catch (IOException e) {
            deleteRecursively(staging);
            return Result.fail(R.string.status_copy_failed, describe(e));
        } catch (SecurityException e) {
            deleteRecursively(staging);
            return Result.fail(R.string.status_copy_failed, describe(e));
        } catch (IllegalArgumentException e) {
            deleteRecursively(staging);
            return Result.fail(R.string.status_copy_failed, describe(e));
        }
    }

    private static void swapStagedDirectory(File staging, File destination) throws IOException {
        final File backup = new File(destination.getParentFile(), "game.old");
        deleteRecursively(backup);
        if (destination.exists() && !destination.renameTo(backup)) {
            throw new IOException("cannot preserve the previous game copy");
        }
        if (!staging.renameTo(destination)) {
            if (backup.exists() && !backup.renameTo(destination)) {
                throw new IOException("cannot install the staged copy or restore the previous copy");
            }
            throw new IOException("cannot install the staged copy");
        }
        deleteRecursively(backup);
    }

    private static String describe(Exception e) {
        final String message = e.getMessage();
        return message != null && !message.isEmpty() ? message : e.getClass().getSimpleName();
    }

    /**
     * First pass: records every wanted file below the given document with its
     * URI, path relative to the game root and reported size (negative when
     * the provider does not say).
     */
    private void collect(Uri tree, String documentId, String relativePath, List<Entry> entries,
                         Set<String> seenPaths) throws IOException {
        final Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        try (Cursor cursor = resolver.query(children, new String[]{
                DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                DocumentsContract.Document.COLUMN_MIME_TYPE,
                DocumentsContract.Document.COLUMN_SIZE}, null, null, null)) {
            if (cursor == null) {
                throw new IOException("the document provider returned no listing");
            }
            final int idIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DOCUMENT_ID);
            final int nameIndex =
                    cursor.getColumnIndex(DocumentsContract.Document.COLUMN_DISPLAY_NAME);
            final int mimeIndex =
                    cursor.getColumnIndex(DocumentsContract.Document.COLUMN_MIME_TYPE);
            final int sizeIndex = cursor.getColumnIndex(DocumentsContract.Document.COLUMN_SIZE);
            while (cursor.moveToNext()) {
                final String id = cursor.getString(idIndex);
                final String name = cursor.getString(nameIndex);
                final String mime = cursor.getString(mimeIndex);
                if (id == null || name == null || !isSafeDocumentName(name)) {
                    continue;
                }
                String path = relativePath.isEmpty() ? name : relativePath + "/" + name;
                final String lower = name.toLowerCase(Locale.US);
                if (lower.equals("th08.dat")) {
                    path = relativePath.isEmpty() ? "th08.dat" : relativePath + "/th08.dat";
                } else if (lower.equals("thbgm.dat")) {
                    path = relativePath.isEmpty() ? "thbgm.dat" : relativePath + "/thbgm.dat";
                }
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    collect(tree, id, path, entries, seenPaths);
                } else if (isWantedDataFile(name)) {
                    final String collisionKey = path.toLowerCase(Locale.US);
                    if (!seenPaths.add(collisionKey)) continue;
                    final long size = sizeIndex >= 0 ? cursor.getLong(sizeIndex) : -1;
                    entries.add(new Entry(
                            DocumentsContract.buildDocumentUriUsingTree(tree, id), path, size));
                }
            }
        }
    }

    private void copyFile(Entry entry, File target, Progress progress) throws IOException {
        final File parent = target.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("cannot create " + parent);
        }
        try (InputStream in = resolver.openInputStream(entry.uri)) {
            if (in == null) {
                throw new IOException("cannot open " + entry.relativePath);
            }
            try (OutputStream out = new FileOutputStream(target)) {
                final byte[] buffer = new byte[256 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0) {
                    out.write(buffer, 0, read);
                    progress.bytes(read);
                }
            }
        }
    }

    private static boolean isSafeDocumentName(String name) {
        return !name.isEmpty() && !name.equals(".") && !name.equals("..")
                && !name.contains("/") && !name.contains("\\") && !name.contains("\0");
    }

    /** Only the files the game opens are imported: the two archives and any
     *  font dropped next to them; saves and config are created here anyway. */
    private static boolean isWantedDataFile(String name) {
        final String lower = name.toLowerCase(Locale.US);
        return lower.equals("th08.dat")
                || lower.equals("thbgm.dat")
                || lower.endsWith(".ttf")
                || lower.endsWith(".otf");
    }

    private static void deleteRecursively(File file) {
        if (file.isDirectory()) {
            final File[] children = file.listFiles();
            if (children != null) {
                for (File child : children) {
                    deleteRecursively(child);
                }
            }
        }
        //noinspection ResultOfMethodCallIgnored
        file.delete();
    }
}
