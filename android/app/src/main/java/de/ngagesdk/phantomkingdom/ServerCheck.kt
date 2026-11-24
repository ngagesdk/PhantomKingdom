/*
 * "Test" on the server row: opens a TCP connection to the PhantomLink
 * server's Arena login port (80) and closes it again. Nothing is sent.
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom

import java.net.InetSocketAddress
import java.net.Socket
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

sealed interface ServerStatus {
    /** Not tested yet, or the address changed since the last test. */
    data object Unknown : ServerStatus
    data class Checking(val address: String) : ServerStatus
    data class Reachable(val address: String, val millis: Long) : ServerStatus
    data class Unreachable(val address: String, val reason: String) : ServerStatus
}

object ServerCheck {
    private const val TIMEOUT_MS = 5000
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private val status = MutableStateFlow<ServerStatus>(ServerStatus.Unknown)
    val state: StateFlow<ServerStatus> = status.asStateFlow()

    fun check(settings: AppSettings) {
        val host = settings.effectiveHost
        val port = ARENA_PORT
        val address = formatAddress(host, port)
        job?.cancel()
        status.value = ServerStatus.Checking(address)
        job = scope.launch {
            val started = System.nanoTime()
            val result = try {
                Socket().use { it.connect(InetSocketAddress(host, port), TIMEOUT_MS) }
                val ms = (System.nanoTime() - started) / 1_000_000
                AppLog.i("Server $address reachable ($ms ms)")
                ServerStatus.Reachable(address, ms)
            } catch (e: Exception) {
                val reason = when (e) {
                    is java.net.SocketTimeoutException -> "No answer"
                    is java.net.UnknownHostException -> "Unknown address"
                    is java.net.ConnectException -> "Connection refused"
                    else -> e.message ?: e.javaClass.simpleName
                }
                AppLog.w("Server $address not reachable: $reason")
                ServerStatus.Unreachable(address, reason)
            }
            // Ignore the result if the address changed meanwhile.
            val now = status.value
            if (now is ServerStatus.Checking && now.address == address) status.value = result
        }
    }

    /** Called when the address changes. */
    fun reset() {
        job?.cancel()
        status.value = ServerStatus.Unknown
    }
}
