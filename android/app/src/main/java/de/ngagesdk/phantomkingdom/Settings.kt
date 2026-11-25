/*
 * Start screen settings: PhantomLink server, N-Gage Arena account,
 * vibration, appearance. The server and account go to the game as --arena-host,
 * --arena-user and --arena-pass on every start; the game keeps them in
 * its arena.cfg.
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom

import android.content.Context
import android.content.SharedPreferences
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

enum class ThemeMode { LIGHT, DARK, SYSTEM }

/** The public PhantomLink server. */
const val DEFAULT_SERVER_HOST = "188.68.43.183"

/** The Arena's Community service (SOAP over HTTP), see src/net/pk_online.c. */
const val ARENA_PORT = 80

data class AppSettings(
    val serverHost: String = DEFAULT_SERVER_HOST,
    val arenaUser: String = "",
    val arenaPass: String = "",
    /** Vibration when the game takes a tap, swipe or back gesture; off = --no-haptics. */
    val haptics: Boolean = true,
    val smooth: Boolean = true,
    val theme: ThemeMode = ThemeMode.LIGHT,
) {
    /** The address the game connects to; falls back to the default when the field is empty. */
    val effectiveHost: String get() = serverHost.trim().ifEmpty { DEFAULT_SERVER_HOST }
    val serverLabel: String get() = effectiveHost
    val hasAccount: Boolean get() = arenaUser.isNotBlank() && arenaPass.isNotEmpty()
}

/** host:port, with brackets around IPv6 addresses. */
fun formatAddress(host: String, port: Int) = if (':' in host) "[$host]:$port" else "$host:$port"

/** Loose check: a host name, an IPv4 or an IPv6 address, no spaces or URL parts. */
fun isValidHost(host: String): Boolean {
    val h = host.trim()
    if (h.isEmpty() || h.length > 127) return false // the game keeps 127 characters (scene_arena.c)
    if (':' in h) return h.all { it.isLetterOrDigit() || it == ':' || it == '.' || it == '%' }
    return h.split('.').all { label ->
        label.isNotEmpty() && label.length <= 63 && !label.startsWith('-') && !label.endsWith('-') &&
            label.all { it in 'a'..'z' || it in 'A'..'Z' || it in '0'..'9' || it == '-' }
    }
}

/** Field limits of the game's account buffers (user[32], pass[64] in scene_arena.c). */
const val ARENA_USER_MAX = 31
const val ARENA_PASS_MAX = 63

class SettingsStore internal constructor(private val prefs: SharedPreferences) {
    private val state = MutableStateFlow(load())
    val settings: StateFlow<AppSettings> = state.asStateFlow()

    fun update(transform: (AppSettings) -> AppSettings) {
        val s = transform(state.value)
        state.value = s
        prefs.edit()
            .putString("server", s.serverHost)
            .putString("arena_user", s.arenaUser)
            .putString("arena_pass", s.arenaPass)
            .putBoolean("haptics", s.haptics)
            .putBoolean("smooth", s.smooth)
            .putString("theme", s.theme.name)
            .apply()
    }

    private fun load(): AppSettings {
        val d = AppSettings()
        return AppSettings(
            serverHost = prefs.getString("server", null) ?: d.serverHost,
            arenaUser = prefs.getString("arena_user", null) ?: d.arenaUser,
            arenaPass = prefs.getString("arena_pass", null) ?: d.arenaPass,
            haptics = prefs.getBoolean("haptics", d.haptics),
            smooth = prefs.getBoolean("smooth", d.smooth),
            theme = runCatching { ThemeMode.valueOf(prefs.getString("theme", null) ?: "") }.getOrDefault(d.theme),
        )
    }

    companion object {
        @Volatile private var instance: SettingsStore? = null
        fun get(context: Context): SettingsStore =
            instance ?: synchronized(this) {
                instance ?: SettingsStore(
                    context.applicationContext.getSharedPreferences("settings", Context.MODE_PRIVATE),
                ).also { instance = it }
            }
    }
}

/**
 * The game's synthetic IMEI (src/net/pk_identity.c), which PhantomLink binds
 * the Arena account to. The game creates it on its first start in
 * files/identity.cfg; this only reads it, for display.
 */
object Identity {
    fun imei(context: Context): String? = runCatching {
        java.io.File(context.filesDir, "identity.cfg").readLines()
            .firstOrNull { it.startsWith("imei=") }?.removePrefix("imei=")?.trim()
            ?.takeIf { it.length == 15 && it.all(Char::isDigit) }
    }.getOrNull()
}
