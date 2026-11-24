/*
 * In-memory event log shown on the Log tab, newest at the bottom.
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom

import android.util.Log
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/** level: 0 error, 1 warning, 2 info, 3 debug */
data class LogLine(val time: String, val level: Int, val text: String)

object AppLog {
    private const val MAX_LINES = 1000
    private val lines = MutableStateFlow<List<LogLine>>(emptyList())
    val log: StateFlow<List<LogLine>> = lines.asStateFlow()

    fun e(text: String) = add(0, text)
    fun w(text: String) = add(1, text)
    fun i(text: String) = add(2, text)
    fun d(text: String) = add(3, text)

    private fun add(level: Int, text: String) {
        Log.println(if (level == 0) Log.ERROR else if (level == 1) Log.WARN else Log.INFO, "PhantomKingdom", text)
        val time = SimpleDateFormat("HH:mm:ss", Locale.ROOT).format(Date())
        lines.update { (it + LogLine(time, level, text)).takeLast(MAX_LINES) }
    }

    fun clear() { lines.value = emptyList() }

    fun text(): String = lines.value.joinToString("\n") { "${it.time} ${it.text}" }
}
