package org.th08reconstruction.port;

import android.content.pm.ActivityInfo;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

/**
 * The game itself. SDLActivity owns the surface and the native thread; the
 * game library provides main (renamed to SDL_main by SDL's bootstrap), which
 * resolves the configured data directory and runs the reconstructed entry
 * point.
 */
public class MainActivity extends SDLActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // A shoot'em'up must not dim or sleep in the middle of a run.
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected String[] getLibraries() {
        // Order matters: SDL2 provides the bootstrap the game library links
        // against.
        return new String[] {"SDL2", "th08-modern"};
    }

    @Override
    public void setOrientationBis(int width, int height, boolean resizable, String hint) {
        // The game starts and stays horizontal. SDL's default logic derives a
        // sensor-landscape request from its orientation hint, which some ROMs
        // (a Transsion/Unisoc device among them) then settle into portrait
        // anyway, so the orientation is locked outright.
        setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            // Immersive sticky: the game covers the whole panel; a swipe from
            // an edge briefly shows the system bars, which slide away again.
            getWindow().getDecorView().setSystemUiVisibility(
                    View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }

    @Override
    protected String[] getArguments() {
        // The native runtime also reads th08_data_dir.txt from internal
        // storage; the explicit argument wins over a stale file.
        String dir = GameData.currentDataDir(this);
        if (dir == null || dir.isEmpty()) {
            return new String[0];
        }
        return new String[] {"--data-dir=" + dir};
    }
}
