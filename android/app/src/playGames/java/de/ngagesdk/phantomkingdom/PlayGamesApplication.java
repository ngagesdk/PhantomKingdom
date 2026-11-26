// SPDX-License-Identifier: MIT
package de.ngagesdk.phantomkingdom;

import android.app.Application;
import com.google.android.gms.games.PlayGamesSdk;

public final class PlayGamesApplication extends Application {
    @Override public void onCreate() {
        super.onCreate();
        PlayGamesSdk.initialize(this);
    }
}
