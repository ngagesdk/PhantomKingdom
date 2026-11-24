/*
 * Main screen: play, game data and server state, import.
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom.ui

import android.text.format.DateUtils
import android.text.format.Formatter
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilledIconButton
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import de.ngagesdk.phantomkingdom.AppSettings
import de.ngagesdk.phantomkingdom.GameInfo
import de.ngagesdk.phantomkingdom.ImportState
import de.ngagesdk.phantomkingdom.ServerStatus

@Composable
fun HomeScreen(
    settings: AppSettings,
    game: GameInfo?,
    importState: ImportState,
    server: ServerStatus,
    onPlay: () -> Unit,
    onImportZip: () -> Unit,
    onImportFolder: () -> Unit,
    onCancelImport: () -> Unit,
    onRemoveData: () -> Unit,
    onTestServer: () -> Unit,
    onServerSettings: () -> Unit,
    contentPadding: PaddingValues,
) {
    val sc = LocalStatusColors.current
    val context = LocalContext.current
    val importing = importState is ImportState.Running
    Column(
        Modifier.verticalScroll(rememberScrollState()).padding(contentPadding).padding(horizontal = 20.dp)
            .padding(bottom = 24.dp),
    ) {
        LogoHeader()
        Spacer(Modifier.height(12.dp))
        PlayCard(ready = game != null && !importing, onPlay = onPlay)

        if (importState is ImportState.Failed) {
            Spacer(Modifier.height(12.dp))
            Card(
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.errorContainer),
                shape = MaterialTheme.shapes.large, modifier = Modifier.fillMaxWidth(),
            ) {
                Text(importState.message, style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onErrorContainer, modifier = Modifier.padding(18.dp))
            }
        }

        SectionTitle("Status")
        PlainCard {
            when {
                importState is ImportState.Running -> {
                    StatusRow("Game data", "Importing… ${importState.files} files, " +
                        Formatter.formatShortFileSize(context, importState.bytes), sc.busy,
                        action = { TextButton(onClick = onCancelImport) { Text("Cancel") } })
                    LinearProgressIndicator(Modifier.fillMaxWidth().padding(horizontal = 18.dp).padding(bottom = 10.dp))
                }
                game != null -> StatusRow(
                    "Game data",
                    "${game.files} files, ${Formatter.formatShortFileSize(context, game.bytes)}",
                    sc.ok,
                )
                else -> StatusRow("Game data", "Not imported", sc.off)
            }
            HorizontalDivider(Modifier.padding(horizontal = 18.dp), color = MaterialTheme.colorScheme.outlineVariant)
            val (srvText, srvColor) = when (server) {
                ServerStatus.Unknown -> settings.serverLabel to sc.off
                is ServerStatus.Checking -> "Testing ${server.address}" to sc.busy
                is ServerStatus.Reachable -> "${server.address} reachable (${server.millis} ms)" to sc.ok
                is ServerStatus.Unreachable -> "${server.address}: ${server.reason}" to sc.bad
            }
            StatusRow("Server", srvText, srvColor, action = {
                TextButton(onClick = onTestServer, enabled = server !is ServerStatus.Checking) { Text("Test") }
            })
            HorizontalDivider(Modifier.padding(horizontal = 18.dp), color = MaterialTheme.colorScheme.outlineVariant)
            StatusRow(
                "N-Gage Arena account",
                if (settings.hasAccount) settings.arenaUser else "Not set, needed for online play",
                if (settings.hasAccount) sc.ok else sc.off,
            )
        }
        TextButton(onClick = onServerSettings) { Text("Server and account") }

        SectionTitle("Game data")
        PlainCard {
            Column(Modifier.padding(horizontal = 18.dp, vertical = 10.dp)) {
                if (game != null) {
                    Text(game.source, style = MaterialTheme.typography.titleMedium)
                    Text(
                        if (game.copiedOverUsb) "In Android/data/${context.packageName}/files/6rah. Importing replaces it for this app."
                        else "Imported " + DateUtils.getRelativeTimeSpanString(game.importedAt).toString().lowercase(),
                        style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Spacer(Modifier.height(12.dp))
                } else {
                    HowTo()
                    Spacer(Modifier.height(14.dp))
                }
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    FilledTonalButton(onClick = onImportZip, enabled = !importing, modifier = Modifier.weight(1f)) {
                        Text("ZIP archive", maxLines = 1)
                    }
                    FilledTonalButton(onClick = onImportFolder, enabled = !importing, modifier = Modifier.weight(1f)) {
                        Text("Folder", maxLines = 1)
                    }
                }
                if (game != null && !game.copiedOverUsb) {
                    Spacer(Modifier.height(4.dp))
                    OutlinedButton(onClick = onRemoveData, enabled = !importing, modifier = Modifier.fillMaxWidth()) {
                        Text("Remove game data")
                    }
                }
                Spacer(Modifier.height(8.dp))
                Text(
                    "Use the original game files from your own Pocket Kingdom game card. The app does not include or download any game data.",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun PlayCard(ready: Boolean, onPlay: () -> Unit) {
    val container by animateColorAsState(
        if (ready) MaterialTheme.colorScheme.primaryContainer else MaterialTheme.colorScheme.surfaceContainerLow,
        label = "play",
    )
    Card(
        shape = MaterialTheme.shapes.large,
        colors = CardDefaults.cardColors(containerColor = container),
        border = if (ready) null else androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(20.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(
                    if (ready) "Ready to play" else "Game data needed",
                    style = MaterialTheme.typography.titleLarge, fontWeight = FontWeight.SemiBold,
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    if (ready) "Your kingdom is waiting online." else "Import the game files below to start.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.width(16.dp))
            FilledIconButton(
                onClick = onPlay,
                enabled = ready,
                modifier = Modifier.size(68.dp),
                shape = CircleShape,
            ) {
                Icon(Icons.Filled.PlayArrow, contentDescription = "Play", modifier = Modifier.size(32.dp))
            }
        }
    }
}

@Composable
private fun HowTo() {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Step(1, "Copy the original game files from your Pocket Kingdom game card to this phone, as a folder or a ZIP archive.")
        Step(2, "Tap ZIP archive or Folder and select them.")
        Step(3, "Tap the play button above.")
    }
}

@Composable
private fun Step(n: Int, text: String) {
    Row(verticalAlignment = Alignment.Top) {
        Text("$n", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary,
            fontWeight = FontWeight.Bold, modifier = Modifier.width(22.dp))
        Text(text, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurface)
    }
}
