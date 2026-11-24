/*
 * The user's own game data: import from a ZIP archive or a folder, chosen
 * with the system file picker, into the app's private storage.
 *
 * Layout after an import:
 *   files/game/                 everything that was selected
 *   files/game/<root>/          the folder with 6rah.app (the original
 *                               \System\Apps\6RAH\), passed to the game
 *                               as --data
 * The rest of files/ belongs to the game (save.dat, identity.cfg,
 * arena.cfg) and is never touched here.
 *
 * Data copied over USB into Android/data/<package>/files/6rah (the way
 * before this screen existed) is recognised too; the game finds it on its
 * own, so no --data is passed for it.
 *
 * An import is extracted into files/game.import/ first and only replaces
 * files/game/ once it has finished and holds a complete data set, so a
 * failed or cancelled import keeps the previous data.
 *
 * SPDX-License-Identifier: MIT
 */
package de.ngagesdk.phantomkingdom

import android.content.Context
import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import java.io.File
import java.io.IOException
import java.io.InputStream
import java.nio.charset.Charset
import java.util.zip.ZipException
import java.util.zip.ZipInputStream
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlin.coroutines.coroutineContext

/**
 * What is installed now. [dataDir] is passed to the game as --data; null
 * for data copied over USB, which the game finds by itself.
 */
data class GameInfo(
    val files: Int,
    val bytes: Long,
    val source: String,
    val importedAt: Long,
    val dataDir: String?,
) {
    val copiedOverUsb: Boolean get() = dataDir == null
}

sealed interface ImportState {
    data object Idle : ImportState
    data class Running(val source: String, val files: Int, val bytes: Long) : ImportState
    data class Failed(val message: String) : ImportState
    data class Done(val info: GameInfo) : ImportState
}

class ImportException(message: String) : IOException(message)

object GameData {
    /** Upper limits, so a broken or hostile archive cannot fill the phone. */
    private const val MAX_FILES = 50_000
    private const val MAX_BYTES = 4L * 1024 * 1024 * 1024
    private const val MAX_DEPTH = 32

    /** The folder with this file is the data folder (\System\Apps\6RAH\). */
    private const val MARKER = "6rah.app"
    /** How deep below the selection the data folder may be (e/System/Apps/6RAH is 4). */
    private const val MARKER_DEPTH = 8

    /**
     * The minimum data set, the same list as k_required in src/core/pk_fs.c
     * (the game will not start without these). Names only, matched
     * case-insensitively like the game does.
     */
    private val REQUIRED = listOf(
        "6rah.app", "pokekin.mbm", "pokeport.mbm",
        "mestext.dat", "unit.dat", "itemdata.dat", "mapinfo.dat",
        "partydata.dat", "unitcol.dat", "name.dat",
        "bgm01.pcm", "sys01.pcm",
        "english/mestext.dat",
    )

    /** Where the game looks for data copied over USB: Android/data/<package>/files/6rah */
    private fun usbDir(context: Context): File? = context.getExternalFilesDir(null)?.let { File(it, "6rah") }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var job: Job? = null
    private var cleanup: Job? = null

    private val importState = MutableStateFlow<ImportState>(ImportState.Idle)
    val state: StateFlow<ImportState> = importState.asStateFlow()

    private val infoState = MutableStateFlow<GameInfo?>(null)
    /** null: no game data. */
    val info: StateFlow<GameInfo?> = infoState.asStateFlow()

    fun dataDir(context: Context) = File(context.filesDir, "game")
    private fun stagingDir(context: Context) = File(context.filesDir, "game.import")
    private fun oldDir(context: Context) = File(context.filesDir, "game.old")
    private fun prefs(context: Context) = context.getSharedPreferences("game", Context.MODE_PRIVATE)

    /** Reads the stored info at start-up and removes leftovers of an interrupted import. */
    fun init(context: Context) {
        if (cleanup == null) {
            val app = context.applicationContext
            cleanup = scope.launch {
                stagingDir(app).deleteRecursively()
                oldDir(app).deleteRecursively()
            }
        }
        refresh(context)
    }

    val busy: Boolean get() = job?.isActive == true

    /**
     * Looks at what is there now: an import, or else data copied over USB.
     * Called at start and whenever the start screen comes back, since files
     * may have been copied over USB meanwhile.
     */
    fun refresh(context: Context) {
        if (busy) return
        val app = context.applicationContext
        scope.launch {
            cleanup?.join()
            if (busy) return@launch
            infoState.value = readImport(app) ?: readUsb(app)
        }
    }

    private fun readImport(context: Context): GameInfo? {
        val p = prefs(context)
        val root = File(dataDir(context), p.getString("root", "") ?: "")
        if (!p.contains("files") || missingFiles(root).isNotEmpty()) return null
        return GameInfo(
            p.getInt("files", 0), p.getLong("bytes", 0), p.getString("source", "") ?: "",
            p.getLong("time", 0), root.absolutePath,
        )
    }

    private fun readUsb(context: Context): GameInfo? {
        val dir = usbDir(context)?.takeIf { it.isDirectory && missingFiles(it).isEmpty() } ?: return null
        val files = dir.walkTopDown().filter { it.isFile }.toList()
        return GameInfo(
            files.size, files.sumOf { it.length() }, "Copied over USB",
            files.maxOfOrNull { it.lastModified() } ?: 0L, null,
        )
    }

    /** REQUIRED entries that are not in dir (case-insensitive, like the game). */
    private fun missingFiles(dir: File): List<String> =
        if (!dir.isDirectory) REQUIRED else REQUIRED.filter { resolve(dir, it) == null }

    private fun resolve(base: File, rel: String): File? {
        var cur = base
        for (part in rel.split('/')) {
            cur = cur.listFiles()?.firstOrNull { it.name.equals(part, ignoreCase = true) } ?: return null
        }
        return cur
    }

    /** The shallowest folder below top that contains 6rah.app. */
    private fun findDataRoot(top: File): File? {
        var level = listOf(top)
        repeat(MARKER_DEPTH + 1) {
            for (dir in level) {
                if (dir.listFiles()?.any { it.isFile && it.name.equals(MARKER, ignoreCase = true) } == true) return dir
            }
            level = level.flatMap { d -> d.listFiles()?.filter { it.isDirectory }.orEmpty() }.sortedBy { it.path }
            if (level.isEmpty()) return null
        }
        return null
    }

    fun importZip(context: Context, uri: Uri) = start(context, displayName(context, uri) ?: "ZIP archive") { staging, progress ->
        try {
            extractZip(context, uri, staging, Charsets.UTF_8, progress)
        } catch (e: IllegalArgumentException) {
            // Entry names that are not UTF-8: older Windows tools use the DOS code page.
            AppLog.d("Entry names are not UTF-8, retrying as code page 437")
            staging.deleteRecursively()
            staging.mkdirs()
            progress.reset()
            extractZip(context, uri, staging, Charset.forName("IBM437"), progress)
        }
    }

    fun importFolder(context: Context, treeUri: Uri) = start(context, folderName(treeUri)) { staging, progress ->
        val rootId = DocumentsContract.getTreeDocumentId(treeUri)
        copyTree(context, treeUri, rootId, staging, 0, progress)
    }

    fun cancel() { job?.cancel() }

    fun dismissResult() {
        if (importState.value !is ImportState.Running) importState.value = ImportState.Idle
    }

    /** Deletes the imported data. Data copied over USB is left alone (the user manages that folder). */
    fun remove(context: Context) {
        if (busy) return
        val app = context.applicationContext
        scope.launch {
            dataDir(app).deleteRecursively()
            prefs(app).edit().clear().apply()
            importState.value = ImportState.Idle
            AppLog.i("Imported game data removed")
            infoState.value = readUsb(app)
        }
    }

    /* ------------------------------------------------------------------ */

    private class Progress(val source: String) {
        var files = 0
        var bytes = 0L
        private var lastReport = 0L

        fun reset() { files = 0; bytes = 0 }

        fun addFile() {
            files++
            if (files > MAX_FILES) throw ImportException("More than $MAX_FILES files. Is this the right archive?")
        }

        fun addBytes(n: Int) {
            bytes += n
            if (bytes > MAX_BYTES) throw ImportException("The game data is larger than 4 GB. Is this the right archive?")
            val now = System.currentTimeMillis()
            if (now - lastReport > 150) { lastReport = now; publish() }
        }

        fun publish() { importState.value = ImportState.Running(source, files, bytes) }
    }

    private fun start(
        context: Context,
        source: String,
        work: suspend (staging: File, progress: Progress) -> Unit,
    ) {
        if (busy) return
        val app = context.applicationContext
        job = scope.launch {
            cleanup?.join()
            val staging = stagingDir(app)
            val progress = Progress(source)
            progress.publish()
            AppLog.i("Importing game data from $source")
            try {
                staging.deleteRecursively()
                if (!staging.mkdirs()) throw IOException("Could not create ${staging.name}")
                work(staging, progress)
                coroutineContext.ensureActive()
                if (progress.files == 0) throw ImportException("No files found in $source.")
                val root = findDataRoot(staging) ?: throw ImportException(
                    "6rah.app was not found in $source. Select the \\System\\Apps\\6RAH folder of your " +
                        "Pocket Kingdom game card, the whole card, or a ZIP archive of either.",
                )
                val missing = missingFiles(root)
                if (missing.isNotEmpty()) {
                    throw ImportException("Some game files are missing in $source: ${missing.joinToString(", ")}")
                }
                val rel = root.relativeTo(staging).path
                install(app, staging)
                val info = GameInfo(
                    progress.files, progress.bytes, source, System.currentTimeMillis(),
                    File(dataDir(app), rel).absolutePath,
                )
                prefs(app).edit()
                    .putInt("files", info.files).putLong("bytes", info.bytes)
                    .putString("source", info.source).putLong("time", info.importedAt)
                    .putString("root", rel)
                    .apply()
                infoState.value = info
                importState.value = ImportState.Done(info)
                AppLog.i("Imported ${info.files} files (${info.bytes} bytes) from $source")
            } catch (e: kotlinx.coroutines.CancellationException) {
                importState.value = ImportState.Idle
                AppLog.w("Import cancelled; the previous game data is unchanged")
                throw e
            } catch (e: ImportException) {
                importState.value = ImportState.Failed(e.message ?: "Import failed")
                AppLog.e("Import failed: ${e.message}")
            } catch (e: ZipException) {
                importState.value = ImportState.Failed("$source is not a valid ZIP archive.")
                AppLog.e("Import failed: ${e.message}")
            } catch (e: Exception) {
                importState.value = ImportState.Failed("Import failed: ${e.message ?: e.javaClass.simpleName}")
                AppLog.e("Import failed: $e")
            } finally {
                staging.deleteRecursively()
            }
        }
    }

    /** Swaps the new data in; the old data is removed only after the new data is in place. */
    private fun install(context: Context, root: File) {
        val target = dataDir(context)
        val old = oldDir(context)
        old.deleteRecursively()
        if (target.exists() && !target.renameTo(old)) throw IOException("Could not replace the old game data")
        if (!root.renameTo(target)) {
            old.renameTo(target)
            throw IOException("Could not move the game data into place")
        }
        old.deleteRecursively()
    }

    private suspend fun extractZip(context: Context, uri: Uri, staging: File, charset: Charset, progress: Progress) {
        val base = staging.canonicalPath + File.separator
        val input = context.contentResolver.openInputStream(uri) ?: throw ImportException("Could not open the archive.")
        ZipInputStream(input.buffered(), charset).use { zip ->
            var any = false
            while (true) {
                coroutineContext.ensureActive()
                val entry = zip.nextEntry ?: break
                any = true
                val name = cleanPath(entry.name) ?: continue
                val out = File(staging, name)
                if (!out.canonicalPath.startsWith(base)) throw ImportException("The archive contains an unsafe path: ${entry.name}")
                if (entry.isDirectory) {
                    out.mkdirs()
                } else {
                    progress.addFile()
                    out.parentFile?.mkdirs()
                    copy(zip, out, progress)
                }
            }
            if (!any) throw ImportException("The archive is empty or not a ZIP file.")
        }
    }

    private suspend fun copyTree(context: Context, tree: Uri, docId: String, dir: File, depth: Int, progress: Progress) {
        if (depth > MAX_DEPTH) throw ImportException("The folder is nested too deeply.")
        val resolver = context.contentResolver
        val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId)
        val projection = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        )
        val entries = mutableListOf<Triple<String, String, Boolean>>()
        resolver.query(children, projection, null, null, null)?.use { c ->
            while (c.moveToNext()) {
                val id = c.getString(0) ?: continue
                val name = c.getString(1) ?: continue
                val isDir = c.getString(2) == DocumentsContract.Document.MIME_TYPE_DIR
                entries += Triple(id, name, isDir)
            }
        } ?: throw ImportException("Could not read the folder.")
        for ((id, rawName, isDir) in entries) {
            coroutineContext.ensureActive()
            val name = cleanName(rawName) ?: continue
            val out = File(dir, name)
            if (isDir) {
                out.mkdirs()
                copyTree(context, tree, id, out, depth + 1, progress)
            } else {
                progress.addFile()
                val doc = DocumentsContract.buildDocumentUriUsingTree(tree, id)
                (resolver.openInputStream(doc) ?: throw ImportException("Could not read $rawName")).use { copy(it, out, progress) }
            }
        }
    }

    private suspend fun copy(input: InputStream, out: File, progress: Progress) {
        out.outputStream().use { output ->
            val buffer = ByteArray(64 * 1024)
            while (true) {
                coroutineContext.ensureActive()
                val n = input.read(buffer)
                if (n < 0) break
                progress.addBytes(n)
                output.write(buffer, 0, n)
            }
        }
    }

    /** Normalised relative path of a ZIP entry, or null to skip it (macOS metadata, "." only). */
    private fun cleanPath(raw: String): String? {
        val parts = raw.replace('\\', '/').split('/').filter { it.isNotEmpty() && it != "." }
        if (parts.isEmpty()) return null
        if (parts.any { it == ".." }) throw ImportException("The archive contains an unsafe path: $raw")
        if (parts.first() == "__MACOSX" || parts.last() == ".DS_Store") return null
        return parts.joinToString(File.separator)
    }

    private fun cleanName(raw: String): String? {
        if (raw == "." || raw == ".." || raw == ".DS_Store" || raw.isEmpty()) return null
        return raw.replace('/', '_').replace('\u0000', '_')
    }

    private fun displayName(context: Context, uri: Uri): String? = runCatching {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { c ->
            if (c.moveToFirst()) c.getString(0) else null
        }
    }.getOrNull()

    /** "primary:Games/PocketKingdom" -> "PocketKingdom" */
    private fun folderName(treeUri: Uri): String {
        val id = runCatching { DocumentsContract.getTreeDocumentId(treeUri) }.getOrNull() ?: return "folder"
        return id.substringAfterLast(':').substringAfterLast('/').ifEmpty { id.substringBefore(':') }.ifEmpty { "folder" }
    }
}
