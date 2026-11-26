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
import androidx.compose.material3.OutlinedTextField
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
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import java.io.ByteArrayInputStream
import java.io.IOException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
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
    private var confirmRestore by mutableStateOf(false)
    private var saveBusy by mutableStateOf(false)
    private var phoneSave by mutableStateOf<ByteArray?>(null)
    private var phoneImei by mutableStateOf("")
    private var phoneError by mutableStateOf<String?>(null)
    private var exportPhoneDialog by mutableStateOf(false)
    /** The game's IMEI from identity.cfg; read again when the screen comes back. */
    private var imei by mutableStateOf<String?>(null)

    private val exportSave = registerForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
        if (uri != null) manageSave("Save exported.") { SaveBackup.export(applicationContext, uri) }
    }
    private val restoreSave = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) readSaveSelection(uri)
    }
    private val pickPhoneSalt = registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) readPhoneSalt(uri)
    }
    private val exportPhone = registerForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
        if (uri != null) manageSave("Encrypted N-Gage save exported.") { SaveBackup.exportPhone(applicationContext, uri) }
    }

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
        phoneSave = savedInstanceState?.getByteArray("phoneSave")
        phoneImei = savedInstanceState?.getString("phoneImei") ?: ""
        phoneError = savedInstanceState?.getString("phoneError")
        exportPhoneDialog = savedInstanceState?.getBoolean("exportPhoneDialog") ?: false
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
        PlayGamesBridge.initialize(this)
        // The game (other process) may have created identity.cfg, or files
        // may have been copied over USB meanwhile.
        imei = Identity.imei(this)
        GameData.refresh(this)
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        outState.putInt("tab", tab)
        outState.putByteArray("phoneSave", phoneSave)
        outState.putString("phoneImei", phoneImei)
        outState.putString("phoneError", phoneError)
        outState.putBoolean("exportPhoneDialog", exportPhoneDialog)
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
                    playGamesEnabled = PlayGamesBridge.enabled(),
                    onAchievements = { PlayGamesBridge.showAchievements(this) },
                    onLeaderboards = { PlayGamesBridge.showLeaderboards(this) },
                    contentPadding = padding,
                )
                1 -> LogScreen(log, onShare = ::shareLog, onClear = AppLog::clear, contentPadding = padding)
                else -> SettingsScreen(
                    settings, imei, versionName(), onChange = ::updateSettings,
                    onExportSave = { openSavePicker(false) }, onRestoreSave = { confirmRestore = true },
                    onExportPhone = { exportPhoneDialog = true },
                    saveBusy = saveBusy, contentPadding = padding,
                )
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
        if (confirmRestore) {
            AlertDialog(
                onDismissRequest = { confirmRestore = false },
                title = { Text("Restore save and N-Gage ID?") },
                text = { Text("Select an exported backup ZIP or an encrypted N-Gage save.dat. This replaces your current save and N-Gage ID. Your Arena account may be bound to a different ID. Export your current save first if you want to keep it. Close the game completely before restoring.") },
                confirmButton = { TextButton(onClick = { confirmRestore = false; openSavePicker(true) }) { Text("Choose file") } },
                dismissButton = { TextButton(onClick = { confirmRestore = false }) { Text("Cancel") } },
            )
        }
        if (exportPhoneDialog) {
            AlertDialog(
                onDismissRequest = { exportPhoneDialog = false },
                title = { Text("Export for N-Gage") },
                text = { Text("The ZIP contains save.dat and 101fd3dd.ini. Copy them to the phone's System/Apps/6RAH and System/Shareddata folders respectively. The phone's IMEI must match the app's N-Gage ID. Close the game first. Current ID: ${imei ?: "not available"}.") },
                confirmButton = { TextButton(onClick = ::choosePhoneExport) { Text("Choose location") } },
                dismissButton = { TextButton(onClick = { exportPhoneDialog = false }) { Text("Cancel") } },
            )
        }
        if (phoneSave?.size == PhoneSave.PHONE_FILE_SIZE) {
            AlertDialog(
                onDismissRequest = { if (!saveBusy) { phoneSave = null; phoneError = null } },
                title = { Text("Select N-Gage salt file") },
                text = { Text("The original save needs the matching 101fd3dd.ini file from the phone's C:\\System\\Shareddata folder. ${phoneError ?: ""}") },
                confirmButton = { TextButton(onClick = ::choosePhoneSalt, enabled = !saveBusy) { Text("Choose file") } },
                dismissButton = { TextButton(onClick = { phoneSave = null; phoneError = null }, enabled = !saveBusy) { Text("Cancel") } },
            )
        } else if (phoneSave != null) {
            AlertDialog(
                onDismissRequest = { if (!saveBusy) { phoneSave = null; phoneImei = ""; phoneError = null } },
                title = { Text("N-Gage save detected") },
                text = {
                    androidx.compose.foundation.layout.Column {
                        Text("Enter the 15-digit IMEI of the N-Gage that created this save. Import replaces your current save and N-Gage ID.")
                        OutlinedTextField(
                            value = phoneImei,
                            onValueChange = { phoneImei = it.filter { c -> c in '0'..'9' }.take(15); phoneError = null },
                            label = { Text("Original N-Gage IMEI") },
                            singleLine = true,
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            isError = phoneError != null,
                            supportingText = { phoneError?.let { Text(it) } },
                        )
                    }
                },
                confirmButton = { TextButton(onClick = ::importPhoneSave, enabled = !saveBusy) { Text("Import") } },
                dismissButton = { TextButton(onClick = { phoneSave = null; phoneImei = ""; phoneError = null }, enabled = !saveBusy) { Text("Cancel") } },
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

    private fun openSavePicker(restore: Boolean) {
        if (saveBusy) return
        try {
            if (restore) restoreSave.launch(arrayOf("*/*"))
            else exportSave.launch("phantomkingdom-save.zip")
        } catch (_: android.content.ActivityNotFoundException) {
            message = "No file picker is available on this device."
        }
    }

    private fun choosePhoneExport() {
        try {
            exportPhoneDialog = false
            exportPhone.launch("ngage-save.zip")
        } catch (_: android.content.ActivityNotFoundException) {
            message = "No file picker is available on this device."
        }
    }

    private fun readSaveSelection(uri: Uri) {
        if (saveBusy) return
        saveBusy = true
        lifecycleScope.launch {
            try {
                val data = withContext(Dispatchers.IO) { SaveBackup.readSelection(applicationContext, uri) }
                if (!SaveArchive.isZip(data) && (PhoneSave.isPhoneSave(data) ||
                        (data.size == PhoneSave.PHONE_FILE_SIZE && !SaveArchive.isPlainSave(data)))) {
                    phoneSave = data
                    phoneImei = ""
                    phoneError = null
                } else {
                    withContext(Dispatchers.IO) {
                        if (!SaveArchive.isZip(data)) PhoneSave.requirePhoneSave(data)
                        val archive = SaveArchive.readImport(ByteArrayInputStream(data))
                        SaveBackup.restore(applicationContext, archive)
                    }
                    imei = Identity.imei(this@MainActivity)
                    message = "Save and N-Gage ID restored."
                    AppLog.i("Save and N-Gage ID restored.")
                }
            } catch (e: IOException) {
                reportSaveError(e)
            } catch (e: SecurityException) {
                AppLog.e("Save backup access denied: ${e.message}")
                message = "Save backup failed: access to the selected file was denied."
            } finally {
                saveBusy = false
            }
        }
    }

    private fun choosePhoneSalt() {
        try {
            pickPhoneSalt.launch(arrayOf("*/*"))
        } catch (_: android.content.ActivityNotFoundException) {
            phoneError = "No file picker is available on this device."
        }
    }

    private fun readPhoneSalt(uri: Uri) {
        val data = phoneSave ?: return
        if (saveBusy) return
        saveBusy = true
        lifecycleScope.launch {
            try {
                phoneSave = withContext(Dispatchers.IO) {
                    PhoneSave.joinPhoneFiles(data, SaveBackup.readSelection(applicationContext, uri))
                }
                phoneError = null
            } catch (e: IOException) {
                phoneError = e.message ?: "Could not read the salt file."
                AppLog.e("N-Gage salt import failed: ${e.message}")
            } catch (e: SecurityException) {
                phoneError = "Access to the selected salt file was denied."
                AppLog.e("N-Gage salt import access denied: ${e.message}")
            } finally {
                saveBusy = false
            }
        }
    }

    private fun importPhoneSave() {
        val data = phoneSave ?: return
        val enteredImei = phoneImei
        if (saveBusy) return
        saveBusy = true
        lifecycleScope.launch {
            try {
                withContext(Dispatchers.IO) {
                    val archive = PhoneSave.validate(data, enteredImei)
                    SaveBackup.restore(applicationContext, archive)
                }
                phoneSave = null
                phoneImei = ""
                phoneError = null
                imei = Identity.imei(this@MainActivity)
                message = "N-Gage save and ID imported."
                AppLog.i("N-Gage save and ID imported.")
            } catch (e: IOException) {
                AppLog.e("N-Gage save import failed: ${e.message}")
                phoneError = if (SaveBackup.hasPending(this@MainActivity)) {
                    "Restore incomplete: ${e.message}. Close the game and retry before uninstalling."
                } else e.message ?: "Could not import save."
            } finally {
                saveBusy = false
            }
        }
    }

    private fun reportSaveError(e: IOException) {
        AppLog.e("Save backup failed: ${e.message}")
        message = if (SaveBackup.hasPending(this)) {
            "Restore incomplete: ${e.message}. Close the game and retry before uninstalling."
        } else {
            "Save backup failed: ${e.message}"
        }
    }

    private fun manageSave(success: String, action: () -> Unit) {
        if (saveBusy) return
        saveBusy = true
        lifecycleScope.launch {
            try {
                withContext(Dispatchers.IO) { action() }
                imei = Identity.imei(this@MainActivity)
                message = success
                AppLog.i(success)
            } catch (e: IOException) {
                reportSaveError(e)
            } catch (e: SecurityException) {
                AppLog.e("Save backup access denied: ${e.message}")
                message = "Save backup failed: access to the selected file was denied."
            } finally {
                saveBusy = false
            }
        }
    }

    private fun play(settings: AppSettings) {
        if (saveBusy) { message = "Wait for save backup to finish."; return }
        if (SaveBackup.hasPending(this)) {
            manageSave("Interrupted save restore completed. Start the game again.") { SaveBackup.recover(applicationContext) }
            return
        }
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
