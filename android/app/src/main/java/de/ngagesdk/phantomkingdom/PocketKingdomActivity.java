// SPDX-License-Identifier: MIT
package de.ngagesdk.phantomkingdom;

import android.content.Context;
import android.media.AudioAttributes;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.util.Log;

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
    private static final AudioAttributes HAPTIC_ATTRIBUTES = new AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_MEDIA).build();
    private boolean hapticLogged;

    /* SDL calls this on its game thread with the values of pk_haptic.
     * Media usage keeps these app vibrations separate from Android's system
     * touch-feedback setting; Android can still suppress media vibrations. */
    public void haptic(int kind) {
        Vibrator vib;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager vm = (VibratorManager) getSystemService(Context.VIBRATOR_MANAGER_SERVICE);
            vib = vm != null ? vm.getDefaultVibrator() : null;
        } else {
            vib = (Vibrator) getSystemService(Context.VIBRATOR_SERVICE);
        }
        if (vib == null || !vib.hasVibrator()) {
            if (!hapticLogged) {
                hapticLogged = true;
                Log.w(TAG, "haptics: no vibrator available");
            }
            return;
        }
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                vib.vibrate(VibrationEffect.createPredefined(kind == 2 ? VibrationEffect.EFFECT_HEAVY_CLICK
                        : kind == 1 ? VibrationEffect.EFFECT_TICK : VibrationEffect.EFFECT_CLICK), HAPTIC_ATTRIBUTES);
            } else if (Build.VERSION.SDK_INT >= 26) {
                vib.vibrate(VibrationEffect.createOneShot(kind == 2 ? 40 : kind == 1 ? 15 : 25,
                        VibrationEffect.DEFAULT_AMPLITUDE), HAPTIC_ATTRIBUTES);
            } else {
                vib.vibrate(kind == 2 ? 40 : kind == 1 ? 15 : 25, HAPTIC_ATTRIBUTES);
            }
            if (!hapticLogged) {
                hapticLogged = true;
                Log.i(TAG, "haptics: vibrator request submitted with media usage");
            }
        } catch (SecurityException | IllegalArgumentException e) {
            Log.w(TAG, "haptics: vibrator request failed", e);
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
