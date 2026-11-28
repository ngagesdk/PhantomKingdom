// SPDX-License-Identifier: MIT
package de.ngagesdk.phantomkingdom;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import com.google.android.gms.games.PlayGames;
import com.google.android.gms.games.event.Event;
import com.google.android.gms.games.event.EventBuffer;
import com.google.android.gms.tasks.Task;
import com.google.android.gms.tasks.Tasks;

import java.util.ArrayList;
import java.util.List;

public final class PlayGamesBridge {
    private static final String TAG = "PlayGames";
    private static final String EVENT_OWNED = "CgkI28-4n5oKEAIQCw";
    private static final String EVENT_WINS = "CgkI28-4n5oKEAIQDA";
    private static final String BOARD_OWNED = "CgkI28-4n5oKEAIQDQ";
    private static final String BOARD_WINS = "CgkI28-4n5oKEAIQDg";
    private static final String ONLINE_CREST = "CgkI28-4n5oKEAIQDw";
    private static final String[] TREASURES = {
        "CgkI28-4n5oKEAIQAQ", "CgkI28-4n5oKEAIQAg", "CgkI28-4n5oKEAIQAw", "CgkI28-4n5oKEAIQBA",
        "CgkI28-4n5oKEAIQBQ", "CgkI28-4n5oKEAIQBg", "CgkI28-4n5oKEAIQBw", "CgkI28-4n5oKEAIQCA"
    };
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private PlayGamesBridge() {}
    public static boolean enabled() { return true; }

    public static void initialize(Activity activity) {
        activity.runOnUiThread(() -> PlayGames.getGamesSignInClient(activity).isAuthenticated()
            .addOnSuccessListener(auth -> { if (auth.isAuthenticated()) syncInBackground(activity); })
            .addOnFailureListener(e -> Log.w(TAG, "Authentication check failed", e)));
    }

    public static void onlineCrest(Activity activity) {
        activity.runOnUiThread(() -> PlayGames.getGamesSignInClient(activity).isAuthenticated()
            .addOnSuccessListener(auth -> {
                if (!auth.isAuthenticated()) {
                    Log.i(TAG, "Online Crest achievement sync deferred until sign-in and next save load");
                    return;
                }
                PlayGames.getAchievementsClient(activity).unlockImmediate(ONLINE_CREST)
                    .addOnFailureListener(e -> Log.w(TAG, "Online Crest unlock failed", e));
            }).addOnFailureListener(e -> Log.w(TAG, "Online Crest authentication check failed", e)));
    }

    public static void treasures(Activity activity, int mask) {
        activity.runOnUiThread(() -> PlayGames.getGamesSignInClient(activity).isAuthenticated()
            .addOnSuccessListener(auth -> {
                if (!auth.isAuthenticated()) {
                    Log.i(TAG, "Treasure sync deferred until sign-in and next save load");
                    return;
                }
                for (int i = 0; i < TREASURES.length; i++) {
                    if ((mask & (1 << i)) != 0) {
                        PlayGames.getAchievementsClient(activity).unlockImmediate(TREASURES[i])
                            .addOnFailureListener(e -> Log.w(TAG, "Treasure unlock failed", e));
                    }
                }
            }).addOnFailureListener(e -> Log.w(TAG, "Treasure authentication check failed", e)));
    }

    public static void rankedResult(Activity activity, int won, int owned) {
        activity.runOnUiThread(() -> PlayGames.getGamesSignInClient(activity).isAuthenticated()
            .addOnSuccessListener(auth -> {
                if (!auth.isAuthenticated()) {
                    Log.w(TAG, "Ranked result not recorded: Play Games is not signed in");
                    Toast.makeText(activity, "Play Games is not signed in; this ranked result was not recorded.", Toast.LENGTH_LONG).show();
                    return;
                }
                if (owned > 0) PlayGames.getEventsClient(activity).increment(EVENT_OWNED, owned);
                if (won != 0) PlayGames.getEventsClient(activity).increment(EVENT_WINS, 1);
                Log.i(TAG, "Ranked result queued: won=" + won + ", owned=" + owned);
                syncInBackground(activity);
                MAIN.postDelayed(() -> { if (!activity.isFinishing()) syncInBackground(activity); }, 30000);
            }).addOnFailureListener(e -> {
                Log.w(TAG, "Ranked result authentication check failed", e);
                Toast.makeText(activity, "Play Games unavailable; this ranked result was not recorded.", Toast.LENGTH_LONG).show();
            }));
    }

    private static void syncInBackground(Activity activity) {
        sync(activity).addOnFailureListener(e -> Log.w(TAG, "Leaderboard sync failed", e));
    }

    private static Task<Void> sync(Activity activity) {
        return PlayGames.getEventsClient(activity).loadByIds(true, EVENT_OWNED, EVENT_WINS)
            .onSuccessTask(data -> {
                EventBuffer events = data.get();
                if (events == null) {
                    return Tasks.forException(new IllegalStateException("Ranked event data unavailable"));
                }
                List<Task<?>> submissions = new ArrayList<>();
                try {
                    for (Event event : events) {
                        String board = EVENT_OWNED.equals(event.getEventId()) ? BOARD_OWNED
                            : EVENT_WINS.equals(event.getEventId()) ? BOARD_WINS : null;
                        if (board != null && event.getValue() > 0) {
                            submissions.add(PlayGames.getLeaderboardsClient(activity)
                                .submitScoreImmediate(board, event.getValue()));
                        }
                    }
                } finally {
                    events.release();
                }
                return Tasks.whenAll(submissions);
            });
    }

    public static void showAchievements(Activity activity) { show(activity, true); }
    public static void showLeaderboards(Activity activity) { show(activity, false); }

    private static void show(Activity activity, boolean achievements) {
        PlayGames.getGamesSignInClient(activity).isAuthenticated().addOnSuccessListener(auth -> {
            if (auth.isAuthenticated()) open(activity, achievements);
            else PlayGames.getGamesSignInClient(activity).signIn().addOnSuccessListener(result -> {
                if (result.isAuthenticated()) open(activity, achievements);
                else Toast.makeText(activity, "Play Games sign-in was not completed.", Toast.LENGTH_LONG).show();
            }).addOnFailureListener(e -> showError(activity, e));
        }).addOnFailureListener(e -> showError(activity, e));
    }

    private static void open(Activity activity, boolean achievements) {
        if (achievements) {
            PlayGames.getAchievementsClient(activity).getAchievementsIntent()
                .addOnSuccessListener(intent -> activity.startActivityForResult(intent, 9003))
                .addOnFailureListener(e -> showError(activity, e));
        } else {
            sync(activity).addOnCompleteListener(result -> {
                if (!result.isSuccessful()) {
                    Log.w(TAG, "Leaderboard sync failed", result.getException());
                    Toast.makeText(activity, "Could not update leaderboard scores. Please try again.", Toast.LENGTH_LONG).show();
                }
                if (!activity.isFinishing()) {
                    PlayGames.getLeaderboardsClient(activity).getAllLeaderboardsIntent()
                        .addOnSuccessListener(intent -> activity.startActivityForResult(intent, 9004))
                        .addOnFailureListener(e -> showError(activity, e));
                }
            });
        }
    }

    private static void showError(Activity activity, Exception e) {
        Log.w(TAG, "Could not open Play Games", e);
        Toast.makeText(activity, "Could not open Play Games. Please try again.", Toast.LENGTH_LONG).show();
    }
}
