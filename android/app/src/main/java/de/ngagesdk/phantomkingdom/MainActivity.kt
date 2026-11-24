/*
 * PhantomKingdom for Android, start screen: imports the user's own
 * Pocket Kingdom game data, holds the PhantomLink server and Arena account
 * and starts the game (PocketKingdomActivity, the SDL build of the port).
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom

import android.content.Intent
import android.graphics.Color
import android.net.Uri
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import de.ngagesdk.phantomkingdom.ui.HomeScreen
import de.ngagesdk.phantomkingdom.ui.LogScreen
import de.ngagesdk.phantomkingdom.ui.PhantomTheme
import de.ngagesdk.phantomkingdom.ui.SettingsScreen
import de.ngagesdk.phantomkingdom.ui.isDark

class MainActivity : ComponentActivity() {

    private enum class Source { ZIP, FOLDER }

    private lateinit var store: SettingsStore
    private var message by mutableStateOf<String?>(null)
    private var tab by mutableIntStateOf(0)

    /** Waiting for "Replace game data?" to be answered. */
    private var confirmReplace by mutableStateOf<Source?>(null)
    /** A ZIP opened from another app, waiting for confirmation. */
    private var pendingZip by mutableStateOf<Uri?>(null)
    private var confirmRemove by mutableStateOf(false)
    /** The game's IMEI from identity.cfg; read again when the screen comes back. */
    private var imei by mutableStateOf<String?>(null)

    private val pickZip = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) GameData.importZip(this, uri)
    }

    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) GameData.importFolder(this, uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        store = SettingsStore.get(this)
        GameData.init(this)
        tab = savedInstanceState?.getInt("tab") ?: 0
        if (savedInstanceState == null) {
            AppLog.d("PhantomKingdom ${versionName()} started")
            if (ServerCheck.state.value == ServerStatus.Unknown) ServerCheck.check(store.settings.value)
            handleIntent(intent)
        }
        setContent {
            val settings by store.settings.collectAsStateWithLifecycle()
            val dark = isDark(settings.theme)
            DisposableEffect(dark) {
                enableEdgeToEdge(
                    statusBarStyle = if (dark) SystemBarStyle.dark(Color.TRANSPARENT)
                    else SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT),
                    navigationBarStyle = if (dark) SystemBarStyle.dark(Color.TRANSPARENT)
                    else SystemBarStyle.light(Color.TRANSPARENT, Color.TRANSPARENT),
                )
                onDispose { }
            }
            PhantomTheme(dark) { App(settings) }
        }
    }

    override fun onResume() {
        super.onResume()
        // The game (other process) may have created identity.cfg, or files
        // may have been copied over USB meanwhile.
        imei = Identity.imei(this)
        GameData.refresh(this)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt("tab", tab)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleIntent(intent)
    }

    /** "Open with" a ZIP archive from a file manager. */
    private fun handleIntent(intent: Intent?) {
        if (intent?.action != Intent.ACTION_VIEW) return
        val uri = intent.data ?: return
        tab = 0
        if (GameData.busy) {
            message = "An import is already running."
        } else if (GameData.info.value != null) {
            pendingZip = uri
        } else {
            GameData.importZip(this, uri)
        }
    }

    @Composable
    private fun App(settings: AppSettings) {
        val game by GameData.info.collectAsStateWithLifecycle()
        val importState by GameData.state.collectAsStateWithLifecycle()
        val server by ServerCheck.state.collectAsStateWithLifecycle()
        val log by AppLog.log.collectAsStateWithLifecycle()
        val snackbar = remember { SnackbarHostState() }
        LaunchedEffect(message) {
            message?.let {
                snackbar.showSnackbar(it)
                message = null
            }
        }
        LaunchedEffect(importState) {
            val s = importState
            if (s is ImportState.Done) {
                message = "Game data imported: ${s.info.files} files."
                GameData.dismissResult()
            }
        }
        Scaffold(
            containerColor = MaterialTheme.colorScheme.background,
            snackbarHost = { SnackbarHost(snackbar) },
            bottomBar = {
                NavigationBar(containerColor = MaterialTheme.colorScheme.surfaceContainerLowest, tonalElevation = 0.dp) {
                    listOf(
                        Triple("Game", Icons.Filled.Home, 0),
                        Triple("Log", Icons.AutoMirrored.Filled.List, 1),
                        Triple("Settings", Icons.Filled.Settings, 2),
                    ).forEach { (label, icon, i) ->
                        NavigationBarItem(
                            selected = tab == i, onClick = { tab = i },
                            icon = { Icon(icon, contentDescription = null) }, label = { Text(label) },
                            colors = NavigationBarItemDefaults.colors(indicatorColor = MaterialTheme.colorScheme.primaryContainer),
                        )
                    }
                }
            },
        ) { padding ->
            when (tab) {
                0 -> HomeScreen(
                    settings = settings,
                    game = game,
                    importState = importState,
                    server = server,
                    onPlay = { play(settings) },
                    onImportZip = { requestImport(Source.ZIP) },
                    onImportFolder = { requestImport(Source.FOLDER) },
                    onCancelImport = GameData::cancel,
                    onRemoveData = { confirmRemove = true },
                    onTestServer = { ServerCheck.check(settings) },
                    onServerSettings = { tab = 2 },
                    contentPadding = padding,
                )
                1 -> LogScreen(log, onShare = ::shareLog, onClear = AppLog::clear, contentPadding = padding)
                else -> SettingsScreen(settings, imei, versionName(), onChange = ::updateSettings, contentPadding = padding)
            }
        }
        Dialogs()
    }

    @Composable
    private fun Dialogs() {
        val replace = confirmReplace
        val zip = pendingZip
        if (replace != null || zip != null) {
            AlertDialog(
                onDismissRequest = { confirmReplace = null; pendingZip = null },
                title = { Text("Replace game data?") },
                text = { Text("The imported game data is replaced by the new files. Your settings stay as they are.") },
                confirmButton = {
                    TextButton(onClick = {
                        confirmReplace = null
                        pendingZip = null
                        when {
                            zip != null -> GameData.importZip(this@MainActivity, zip)
                            replace != null -> launchPicker(replace)
                        }
                    }) { Text("Replace") }
                },
                dismissButton = { TextButton(onClick = { confirmReplace = null; pendingZip = null }) { Text("Cancel") } },
            )
        }
        if (confirmRemove) {
            AlertDialog(
                onDismissRequest = { confirmRemove = false },
                title = { Text("Remove game data?") },
                text = { Text("The imported files are deleted from this phone. You can import them again at any time.") },
                confirmButton = {
                    TextButton(onClick = { confirmRemove = false; GameData.remove(this@MainActivity) }) { Text("Remove") }
                },
                dismissButton = { TextButton(onClick = { confirmRemove = false }) { Text("Cancel") } },
            )
        }
    }

    /* ------------------------------------------------------------------ */

    private fun updateSettings(transform: (AppSettings) -> AppSettings) {
        val before = store.settings.value
        store.update(transform)
        val after = store.settings.value
        if (before.effectiveHost != after.effectiveHost) ServerCheck.reset()
    }

    private fun requestImport(source: Source) {
        if (GameData.busy) return
        if (GameData.info.value != null) confirmReplace = source else launchPicker(source)
    }

    private fun launchPicker(source: Source) {
        try {
            when (source) {
                // Some file managers label ZIPs differently; the content is checked on import.
                Source.ZIP -> pickZip.launch(arrayOf("application/zip", "application/x-zip-compressed", "application/octet-stream"))
                Source.FOLDER -> pickFolder.launch(null)
            }
        } catch (_: android.content.ActivityNotFoundException) {
            message = "No file picker is available on this device."
        }
    }

    private fun play(settings: AppSettings) {
        when (val r = GameLauncher.launch(this, settings)) {
            GameLauncher.Result.Started -> Unit
            GameLauncher.Result.NoData -> message = "Import the game data first."
            is GameLauncher.Result.Failed -> message = r.message
        }
    }

    private fun shareLog() {
        val send = Intent(Intent.ACTION_SEND).setType("text/plain")
            .putExtra(Intent.EXTRA_SUBJECT, "PhantomKingdom log")
            .putExtra(Intent.EXTRA_TEXT, AppLog.text())
        startActivity(Intent.createChooser(send, "Share log"))
    }

    private fun versionName(): String =
        runCatching { packageManager.getPackageInfo(packageName, 0).versionName }.getOrNull() ?: ""
}
