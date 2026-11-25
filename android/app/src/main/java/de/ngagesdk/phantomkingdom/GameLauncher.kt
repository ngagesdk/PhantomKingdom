/*
 * Starts the game (PocketKingdomActivity, SDL) with its command line:
 *   --data <folder with 6rah.app>   only for imported data; data copied
 *                                   over USB is found by the game itself
 *   --arena-host <server>           every start (the game's default would
 *                                   be localhost)
 *   --arena-user / --arena-pass     when set on the start screen
 *   --no-haptics                    when vibration is turned off
 *   --smooth                        when smooth rendering is enabled
 * The game stores host, user and password in its arena.cfg
 * (pk_arena_set_account in src/game/scene_arena.c).
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent

object GameLauncher {

    sealed interface Result {
        data object Started : Result
        data object NoData : Result
        data class Failed(val message: String) : Result
    }

    fun arguments(settings: AppSettings, game: GameInfo): List<String> = buildList {
        game.dataDir?.let { add("--data"); add(it) }
        add("--arena-host"); add(settings.effectiveHost)
        if (settings.arenaUser.isNotBlank()) { add("--arena-user"); add(settings.arenaUser.trim()) }
        if (settings.arenaPass.isNotEmpty()) { add("--arena-pass"); add(settings.arenaPass) }
        if (!settings.haptics) add("--no-haptics")
        if (settings.smooth) add("--smooth")
    }

    fun launch(context: Context, settings: AppSettings): Result {
        val game = GameData.info.value ?: return Result.NoData
        val args = arguments(settings, game)
        val intent = Intent(context, PocketKingdomActivity::class.java)
            .putExtra(PocketKingdomActivity.EXTRA_ARGS, args.toTypedArray())
        return try {
            context.startActivity(intent)
            AppLog.i("Game started: " + redact(args).joinToString(" "))
            Result.Started
        } catch (e: ActivityNotFoundException) {
            AppLog.e("Could not start the game: ${e.message}")
            Result.Failed("The game could not be started.")
        }
    }

    /** The command line for the log, without the password. */
    private fun redact(args: List<String>): List<String> =
        args.mapIndexed { i, a -> if (i > 0 && args[i - 1] == "--arena-pass") "***" else a }
}
