/*
 * Settings: PhantomLink server, Arena account, player, appearance. Applied
 * the next time the game is started.
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import de.ngagesdk.phantomkingdom.AppSettings
import de.ngagesdk.phantomkingdom.ARENA_PASS_MAX
import de.ngagesdk.phantomkingdom.ARENA_USER_MAX
import de.ngagesdk.phantomkingdom.DEFAULT_SERVER_HOST
import de.ngagesdk.phantomkingdom.ThemeMode
import de.ngagesdk.phantomkingdom.isValidHost

@Composable
fun SettingsScreen(
    settings: AppSettings,
    imei: String?,
    versionName: String,
    onChange: ((AppSettings) -> AppSettings) -> Unit,
    contentPadding: PaddingValues,
) {
    Column(
        Modifier.verticalScroll(rememberScrollState()).padding(contentPadding).padding(horizontal = 20.dp)
            .padding(bottom = 24.dp),
    ) {
        LogoHeader()

        SectionTitle("Server")
        PlainCard {
            Column(Modifier.padding(horizontal = 18.dp, vertical = 10.dp)) {
                // Local text state, so a half-typed address is not replaced while typing.
                var host by rememberSaveable { mutableStateOf(settings.serverHost) }
                LaunchedEffect(settings.serverHost) { if (settings.serverHost != host) host = settings.serverHost }
                val hostValid = host.isBlank() || isValidHost(host)
                OutlinedTextField(
                    value = host,
                    onValueChange = { v ->
                        host = v.filterNot { it.isWhitespace() }.take(127)
                        if (host.isBlank() || isValidHost(host)) onChange { it.copy(serverHost = host) }
                    },
                    label = { Text("Server address") },
                    placeholder = { Text(DEFAULT_SERVER_HOST) },
                    supportingText = {
                        Text(if (!hostValid) "Enter an IP address or a host name" else "PhantomLink server; empty uses $DEFAULT_SERVER_HOST")
                    },
                    isError = !hostValid,
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Uri, imeAction = ImeAction.Next),
                    modifier = Modifier.fillMaxWidth(),
                )
                val isDefault = settings.serverHost == DEFAULT_SERVER_HOST
                TextButton(onClick = { onChange { it.copy(serverHost = DEFAULT_SERVER_HOST) } }, enabled = !isDefault) {
                    Text("Use the default server")
                }
            }
        }

        SectionTitle("N-Gage Arena account")
        PlainCard {
            Column(Modifier.padding(horizontal = 18.dp, vertical = 10.dp)) {
                OutlinedTextField(
                    value = settings.arenaUser,
                    onValueChange = { v -> onChange { it.copy(arenaUser = v.filterNot { c -> c.isWhitespace() }.take(ARENA_USER_MAX)) } },
                    label = { Text("User name") },
                    singleLine = true,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii, imeAction = ImeAction.Next),
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(6.dp))
                OutlinedTextField(
                    value = settings.arenaPass,
                    onValueChange = { v -> onChange { it.copy(arenaPass = v.take(ARENA_PASS_MAX)) } },
                    label = { Text("Password") },
                    singleLine = true, visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password),
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(8.dp))
                Text(
                    "Your account on the PhantomLink server, for N-Gage Arena in the game's title menu. " +
                        "Passed to the game on startup; an account is automatically created on first login.",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        SectionTitle("Player")
        PlainCard {
            Column(Modifier.padding(horizontal = 18.dp, vertical = 12.dp)) {
                Text("N-Gage ID (IMEI)", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(
                    imei ?: "Created when the game starts for the first time",
                    style = if (imei != null) MaterialTheme.typography.bodyLarge else MaterialTheme.typography.bodyMedium,
                    fontFamily = if (imei != null) FontFamily.Monospace else null,
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    "PhantomLink binds your Arena account to this number, like to an N-Gage's IMEI. " +
                        "It is random and stays the same until the app is uninstalled.",
                    style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }

        SectionTitle("Touch")
        PlainCard {
            Row(
                Modifier.padding(horizontal = 18.dp, vertical = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1f)) {
                    Text("Vibrate on touch", style = MaterialTheme.typography.bodyLarge)
                    Text(
                        "A short vibration when the game takes a tap, a swipe step or a back gesture.",
                        style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                Spacer(Modifier.width(12.dp))
                Switch(checked = settings.haptics, onCheckedChange = { on -> onChange { it.copy(haptics = on) } })
            }
        }

        SectionTitle("Appearance")
        PlainCard {
            Column(Modifier.padding(horizontal = 18.dp, vertical = 12.dp)) {
                Choice(listOf("Light", "Dark", "System"), settings.theme.ordinal) { i ->
                    onChange { it.copy(theme = ThemeMode.entries[i]) }
                }
            }
        }

        Spacer(Modifier.height(20.dp))
        Text(
            "PhantomKingdom $versionName · online play through PhantomLink",
            style = MaterialTheme.typography.bodySmall, color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 4.dp),
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun Choice(options: List<String>, selected: Int, onSelect: (Int) -> Unit) {
    SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth()) {
        options.forEachIndexed { i, label ->
            SegmentedButton(
                selected = i == selected, onClick = { onSelect(i) },
                shape = SegmentedButtonDefaults.itemShape(index = i, count = options.size),
            ) { Text(label, maxLines = 1) }
        }
    }
}
