package org.th08reconstruction.port;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import java.io.File;
import java.io.IOException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * The first-run screen.
 *
 * The user picks the folder of the original Japanese TH08 1.00d installation
 * through the Storage Access Framework, chooses the on-screen control layout,
 * and starts the game. The archives are copied into app-private storage with
 * a visible progress bar, because the reconstructed game opens its archives
 * and config through plain file paths, and Android's scoped storage only
 * allows those writes inside the app's own directories.
 */
public class SetupActivity extends Activity {
    private static final int PICK_FOLDER = 1;

    private final ExecutorService worker = Executors.newSingleThreadExecutor();

    private TextView statusView;
    private Button pickButton;
    private Button startButton;
    private Button exportButton;
    private RadioGroup stickModeGroup;
    private CheckBox leftHandedBox;
    private SeekBar opacityBar;
    private SeekBar scaleBar;
    private SeekBar deadZoneBar;
    private LinearLayout progressRow;
    private ProgressBar progressBar;
    private TextView progressText;

    private TouchSettings settings;
    private String dataDir;
    private boolean busy;

    /** Forwards the importer's callbacks from the worker thread to the UI. */
    private final GameFolderImporter.Listener importListener =
            new GameFolderImporter.Listener() {
                @Override
                public void stage(final int stageRes) {
                    runOnUiThread(() -> {
                        statusView.setText(stageRes);
                        progressText.setText("");
                        progressBar.setProgress(0);
                        progressBar.setIndeterminate(stageRes == R.string.status_working);
                    });
                }

                @Override
                public void onProgress(final int percent) {
                    runOnUiThread(() -> {
                        if (percent < 0) {
                            progressBar.setIndeterminate(true);
                            progressText.setText("");
                        } else {
                            progressBar.setIndeterminate(false);
                            progressBar.setProgress(percent);
                            progressText.setText(percent + "%");
                        }
                    });
                }
            };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        settings = GameData.loadTouchSettings(this);
        dataDir = GameData.currentDataDir(this);
        // Older builds stored a shared-storage folder that may look readable
        // while rejecting the game's config writes, or hold a foreign
        // th08.cfg. Only the app-private copy is usable now, so anything else
        // is cleared and the folder has to be picked once more.
        boolean clearedLegacyDir = false;
        if (dataDir != null && !isAppPrivateGameDir(dataDir)) {
            dataDir = null;
            GameData.clearDataDir(this);
            clearedLegacyDir = true;
        }
        buildUi();
        refreshStatus();
        if (clearedLegacyDir) {
            statusView.setText(R.string.status_refresh_needed);
        }
    }

    private boolean isAppPrivateGameDir(String dir) {
        // The imported directory IS getFilesDir()/game itself, so the exact
        // path has to count as app-private: a startsWith check with the
        // trailing separator rejected it, which cleared the stored directory
        // on every restart and forced a full re-import each launch.
        final String base = new File(getFilesDir(), "game").getAbsolutePath();
        return (dir.equals(base) || dir.startsWith(base + File.separator))
                && GameData.hasGameData(dir);
    }

    @Override
    protected void onPause() {
        super.onPause();
        persistSettings();
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }

    private void buildUi() {
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        int pad = dp(16);
        content.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(R.string.setup_title);
        title.setTextSize(22f);
        content.addView(title, matchWrap());

        TextView explanation = new TextView(this);
        explanation.setText(R.string.setup_explanation);
        explanation.setPadding(0, dp(8), 0, dp(12));
        content.addView(explanation, matchWrap());

        statusView = new TextView(this);
        statusView.setText(R.string.status_no_folder);
        statusView.setPadding(0, 0, 0, dp(8));
        content.addView(statusView, matchWrap());

        progressBar = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progressBar.setMax(100);
        progressText = new TextView(this);
        progressText.setTextSize(14f);
        progressRow = new LinearLayout(this);
        progressRow.setOrientation(LinearLayout.HORIZONTAL);
        progressRow.setGravity(Gravity.CENTER_VERTICAL);
        progressRow.addView(progressBar, new LinearLayout.LayoutParams(
                0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        LinearLayout.LayoutParams percentParams = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        percentParams.leftMargin = dp(8);
        progressRow.addView(progressText, percentParams);
        progressRow.setPadding(0, 0, 0, dp(12));
        progressRow.setVisibility(View.GONE);
        content.addView(progressRow, matchWrap());

        pickButton = new Button(this);
        pickButton.setText(R.string.pick_folder);
        pickButton.setOnClickListener(v -> pickGameFolder());
        content.addView(pickButton, matchWrap());

        content.addView(sectionLabel(R.string.controls_header), matchWrap());

        stickModeGroup = new RadioGroup(this);
        RadioButton fixed = new RadioButton(this);
        fixed.setId(View.generateViewId());
        fixed.setText(R.string.stick_fixed);
        RadioButton floating = new RadioButton(this);
        floating.setId(View.generateViewId());
        floating.setText(R.string.stick_floating);
        stickModeGroup.addView(fixed);
        stickModeGroup.addView(floating);
        stickModeGroup.check(settings.stickMode == TouchSettings.STICK_FIXED
                ? fixed.getId() : floating.getId());
        stickModeGroup.setOnCheckedChangeListener((group, checkedId) -> {
            settings.stickMode = checkedId == fixed.getId()
                    ? TouchSettings.STICK_FIXED : TouchSettings.STICK_FLOATING;
        });
        content.addView(stickModeGroup, matchWrap());

        leftHandedBox = new CheckBox(this);
        leftHandedBox.setText(R.string.left_handed);
        leftHandedBox.setChecked(settings.leftHanded);
        leftHandedBox.setOnCheckedChangeListener((button, checked) -> settings.leftHanded = checked);
        content.addView(leftHandedBox, matchWrap());

        RadioGroup displayModeGroup = new RadioGroup(this);
        RadioButton fill = new RadioButton(this);
        fill.setId(View.generateViewId());
        fill.setText(R.string.display_fill);
        RadioButton fit = new RadioButton(this);
        fit.setId(View.generateViewId());
        fit.setText(R.string.display_fit);
        displayModeGroup.addView(fill);
        displayModeGroup.addView(fit);
        displayModeGroup.check(settings.displayMode == TouchSettings.DISPLAY_FIT
                ? fit.getId() : fill.getId());
        displayModeGroup.setOnCheckedChangeListener((group, checkedId) -> {
            settings.displayMode = checkedId == fit.getId()
                    ? TouchSettings.DISPLAY_FIT : TouchSettings.DISPLAY_FILL;
        });
        content.addView(displayModeGroup, matchWrap());

        opacityBar = slider(content, R.string.opacity_label, 10, 100,
                Math.round(settings.opacity * 100f), value -> settings.opacity = value / 100f);
        scaleBar = slider(content, R.string.scale_label, 50, 200,
                Math.round(settings.scale * 100f), value -> settings.scale = value / 100f);
        deadZoneBar = slider(content, R.string.dead_zone_label, 0, 60,
                Math.round(settings.deadZone * 100f), value -> settings.deadZone = value / 100f);

        startButton = new Button(this);
        startButton.setText(R.string.start_game);
        startButton.setEnabled(false);
        startButton.setOnClickListener(v -> startGame());
        LinearLayout.LayoutParams startParams = matchWrap();
        startParams.topMargin = dp(16);
        content.addView(startButton, startParams);

        exportButton = new Button(this);
        exportButton.setText(R.string.export_logs);
        exportButton.setOnClickListener(v -> exportLogs());
        LinearLayout.LayoutParams exportParams = matchWrap();
        exportParams.topMargin = dp(8);
        content.addView(exportButton, exportParams);

        ScrollView scroll = new ScrollView(this);
        scroll.addView(content);
        setContentView(scroll);
    }

    private TextView sectionLabel(int textRes) {
        TextView label = new TextView(this);
        label.setText(textRes);
        label.setPadding(0, dp(16), 0, dp(4));
        return label;
    }

    private interface SliderTarget {
        void accept(float value);
    }

    private SeekBar slider(LinearLayout content, int labelRes, int min, int max, int initial,
                           SliderTarget target) {
        TextView label = new TextView(this);
        label.setText(labelRes);
        content.addView(label, matchWrap());
        SeekBar bar = new SeekBar(this);
        bar.setMax(max - min);
        bar.setProgress(Math.max(0, Math.min(initial - min, max - min)));
        bar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                target.accept((min + progress) / 100f);
            }

            @Override public void onStartTrackingTouch(SeekBar seekBar) {}

            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });
        content.addView(bar, matchWrap());
        return bar;
    }

    private void pickGameFolder() {
        if (busy) return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
            startActivityForResult(intent, PICK_FOLDER);
        } catch (android.content.ActivityNotFoundException e) {
            statusView.setText(R.string.error_no_document_provider);
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent result) {
        super.onActivityResult(requestCode, resultCode, result);
        if (requestCode != PICK_FOLDER || resultCode != RESULT_OK || result == null
                || result.getData() == null) {
            return;
        }
        final Uri tree = result.getData();
        final int takeFlags = result.getFlags()
                & (Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
        try {
            getContentResolver().takePersistableUriPermission(tree, takeFlags);
        } catch (SecurityException ignored) {
            // Some providers do not offer persistable grants; the copy below
            // still works within this session.
        }

        busy = true;
        pickButton.setEnabled(false);
        startButton.setEnabled(false);
        statusView.setText(R.string.status_working);
        progressRow.setVisibility(View.VISIBLE);
        progressBar.setIndeterminate(true);
        progressBar.setProgress(0);
        progressText.setText("");
        worker.execute(() -> {
            final GameFolderImporter.Result imported =
                    new GameFolderImporter(getContentResolver())
                            .importTree(tree, getFilesDir(), importListener);
            runOnUiThread(() -> completeImport(imported));
        });
    }

    /** Applies the worker's result; runs on the UI thread. */
    private void completeImport(GameFolderImporter.Result imported) {
        busy = false;
        pickButton.setEnabled(true);
        progressRow.setVisibility(View.GONE);
        progressBar.setIndeterminate(false);
        progressText.setText("");
        if (imported.directory == null) {
            // The failure message must survive on screen. The previous code
            // called refreshStatus() right after it, which replaced the
            // message with the neutral "no folder" text in the same frame and
            // made a failed import look like nothing had happened at all.
            if (imported.detail != null) {
                statusView.setText(getString(imported.errorRes, imported.detail));
            } else {
                statusView.setText(imported.errorRes);
            }
            startButton.setEnabled(dataDir != null && GameData.hasGameData(dataDir));
            return;
        }
        dataDir = imported.directory;
        try {
            GameData.storeDataDir(this, dataDir);
        } catch (IOException e) {
            dataDir = null;
            statusView.setText(getString(R.string.error_storage, e.getMessage()));
            startButton.setEnabled(false);
            return;
        }
        refreshStatus();
    }

    /** Bundles the port's logs and offers them through the share sheet. */
    private void exportLogs() {
        if (busy) return;
        busy = true;
        pickButton.setEnabled(false);
        startButton.setEnabled(false);
        exportButton.setEnabled(false);
        statusView.setText(R.string.status_exporting);
        worker.execute(() -> {
            LogExporter.Result exported;
            try {
                exported = new LogExporter(getApplicationContext()).export();
            } catch (IOException e) {
                runOnUiThread(() -> {
                    busy = false;
                    restoreIdleButtons();
                    statusView.setText(getString(R.string.status_export_failed,
                            e.getMessage() != null ? e.getMessage() : e.toString()));
                });
                return;
            }
            runOnUiThread(() -> {
                busy = false;
                restoreIdleButtons();
                statusView.setText(getString(R.string.status_exported, exported.where));
                if (exported.shareUri != null) {
                    final Intent send = new Intent(Intent.ACTION_SEND);
                    send.setType("text/plain");
                    send.putExtra(Intent.EXTRA_STREAM, exported.shareUri);
                    send.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
                    startActivity(Intent.createChooser(send,
                            getString(R.string.share_logs_title)));
                }
            });
        });
    }

    /** Button states after a background task finished. */
    private void restoreIdleButtons() {
        pickButton.setEnabled(true);
        exportButton.setEnabled(true);
        startButton.setEnabled(dataDir != null && GameData.hasGameData(dataDir));
    }

    private void startGame() {
        if (dataDir == null) {
            return;
        }
        persistSettings();
        try {
            GameData.storeDataDir(this, dataDir);
            GameData.storeTouchSettings(this, settings);
        } catch (IOException e) {
            statusView.setText(getString(R.string.error_storage, e.getMessage()));
            return;
        }
        startActivity(new Intent(this, MainActivity.class));
    }

    private void persistSettings() {
        try {
            GameData.storeTouchSettings(this, settings);
        } catch (IOException ignored) {
            // Written again when the game starts.
        }
    }

    private void refreshStatus() {
        if (dataDir == null) {
            statusView.setText(R.string.status_no_folder);
            startButton.setEnabled(false);
            return;
        }
        if (!GameData.hasGameData(dataDir)) {
            statusView.setText(R.string.status_invalid_folder);
            startButton.setEnabled(false);
            return;
        }
        String suffix = GameData.hasMusicData(dataDir)
                ? getString(R.string.status_ready)
                : getString(R.string.status_no_music);
        statusView.setText(getString(R.string.status_folder, dataDir, suffix));
        startButton.setEnabled(true);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
