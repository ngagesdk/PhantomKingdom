// SPDX-License-Identifier: MIT
package de.ngagesdk.phantomkingdom;

import android.app.Activity;

public final class PlayGamesBridge {
    private PlayGamesBridge() {}
    public static boolean enabled() { return false; }
    public static void initialize(Activity activity) {}
    public static void treasures(Activity activity, int mask) {}
    public static void rankedResult(Activity activity, int won, int owned) {}
    public static void showAchievements(Activity activity) {}
    public static void showLeaderboards(Activity activity) {}
}
