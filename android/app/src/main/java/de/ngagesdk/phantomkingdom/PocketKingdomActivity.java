// SPDX-License-Identifier: MIT
package de.ngagesdk.phantomkingdom;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.provider.Settings;
import android.util.Log;
import android.view.HapticFeedbackConstants;
import android.view.View;

import org.libsdl.app.SDLActivity;

/* The game is native code (libmain.so, built from the top-level CMake
 * project); SDL's activity loads it and runs SDL_main.
 *
 * MainActivity (the start screen) starts it with the command line in
 * EXTRA_ARGS: --data <imported folder>, --arena-host, --arena-user,
 * --arena-pass (see GameLauncher.kt). It runs in its own process
 * (":game" in the manifest), which is ended when the game closes, so the
 * native code's static state is fresh on every start. */
public class PocketKingdomActivity extends SDLActivity {
    public static final String EXTRA_ARGS = "de.ngagesdk.phantomkingdom.ARGS";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "main" };
    }

    @Override
    protected String[] getArguments() {
        String[] args = getIntent() != null ? getIntent().getStringArrayExtra(EXTRA_ARGS) : null;
        return args != null ? args : new String[0];
    }

    private static final String TAG = "PhantomKingdom";
    private int hapticLogs; /* the first calls are logged (logcat, "haptics") */

    /* Touch feedback for the game (src/input/pk_haptics.c, called on the SDL
     * thread): 0 a tap was taken, 1 a swipe step, 2 back by long press or
     * two-finger tap (the values of pk_haptic).
     * First the system's touch feedback (the same click as the keyboard,
     * tuned for the phone). Phones only give it while their touch feedback
     * setting allows it (Android "Touch feedback", Samsung "Touch
     * interactions", ...); when the system declines, the vibrator plays a
     * click itself (VIBRATE permission). The game's own switch for this
     * is --no-haptics ("Vibrate on touch" on the start screen). */
    public void haptic(final int kind) {
        runOnUiThread(() -> {
            int effect = kind == 2 ? HapticFeedbackConstants.LONG_PRESS
                    : kind == 1 ? HapticFeedbackConstants.CLOCK_TICK
                    : HapticFeedbackConstants.KEYBOARD_TAP;
            View view = getWindow() != null ? getWindow().getDecorView() : null;
            boolean system = view != null && view.performHapticFeedback(effect);
            boolean vibrator = false;
            if (!system) {
                vibrator = vibrate(kind);
            }
            if (hapticLogs < 3) {
                hapticLogs++;
                int setting = Settings.System.getInt(getContentResolver(), Settings.System.HAPTIC_FEEDBACK_ENABLED, -1);
                Log.i(TAG, "haptics: kind " + kind + ", system feedback " + (system ? "played" : "declined")
                        + ", vibrator " + (system ? "not needed" : vibrator ? "played" : "unavailable")
                        + " (touch feedback setting " + setting + ")");
            }
        });
    }

    /* A short click on the vibrator; false if there is none. */
    private boolean vibrate(int kind) {
        Vibrator vib;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager vm = (VibratorManager) getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
            vib = vm != null ? vm.getDefaultVibrator() : null;
        } else {
            vib = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        }
        if (vib == null || !vib.hasVibrator()) {
            return false;
        }
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                vib.vibrate(VibrationEffect.createPredefined(kind == 2 ? VibrationEffect.EFFECT_HEAVY_CLICK
                        : kind == 1 ? VibrationEffect.EFFECT_TICK : VibrationEffect.EFFECT_CLICK));
            } else if (Build.VERSION.SDK_INT >= 26) {
                vib.vibrate(VibrationEffect.createOneShot(kind == 2 ? 40 : kind == 1 ? 15 : 25,
                        VibrationEffect.DEFAULT_AMPLITUDE));
            } else {
                vib.vibrate(kind == 2 ? 40 : kind == 1 ? 15 : 25);
            }
            return true;
        } catch (RuntimeException e) {
            Log.w(TAG, "haptics: vibrator failed: " + e);
            return false;
        }
    }

    @Override
    protected void onDestroy() {
        boolean closing = isFinishing();
        super.onDestroy();
        if (closing) {
            // Only the game's process: the start screen keeps running.
            System.exit(0);
        }
    }
}
