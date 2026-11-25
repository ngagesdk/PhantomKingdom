/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import android.content.Context
import android.net.Uri
import android.util.AtomicFile
import java.io.ByteArrayOutputStream
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.channels.OverlappingFileLockException
import java.security.SecureRandom
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

internal object SaveBackup {
    private fun <T> locked(context: Context, action: () -> T): T {
        RandomAccessFile(File(context.noBackupFilesDir, "game.lock"), "rw").use { file ->
            val lock = try { file.channel.tryLock() } catch (_: OverlappingFileLockException) { null }
                ?: throw IOException("Close the game completely before managing saves.")
            lock.use { return action() }
        }
    }

    private fun journal(context: Context) = AtomicFile(File(context.noBackupFilesDir, "save-restore.zip"))

    fun hasPending(context: Context): Boolean {
        val file = journal(context).baseFile
        return file.exists() || File(file.path + ".bak").exists()
    }

    private fun atomicWrite(file: AtomicFile, bytes: ByteArray) {
        val stream = file.startWrite()
        try {
            stream.write(bytes)
            file.finishWrite(stream)
        } catch (e: IOException) {
            file.failWrite(stream)
            throw e
        }
    }

    private fun recoverLocked(context: Context) {
        val pending = journal(context)
        if (!pending.baseFile.exists() && !File(pending.baseFile.path + ".bak").exists()) return
        val backup = pending.openRead().use { SaveArchive.read(it) }
        atomicWrite(AtomicFile(File(context.filesDir, "identity.cfg")), backup.identity)
        atomicWrite(AtomicFile(File(context.filesDir, "save.dat")), backup.save)
        pending.delete()
        if (pending.baseFile.exists() || File(pending.baseFile.path + ".bak").exists()) {
            throw IOException("Could not finish the pending restore.")
        }
    }

    fun recover(context: Context) = locked(context) { recoverLocked(context) }

    @JvmStatic
    @Throws(IOException::class)
    fun recoverUnderGameLock(context: Context) = recoverLocked(context)

    private fun currentSave(context: Context): SaveArchive {
        val save = File(context.filesDir, "save.dat")
        val identity = File(context.filesDir, "identity.cfg")
        if (!save.isFile || !identity.isFile) throw IOException("No save and N-Gage ID found. Save a game first.")
        if (save.length() > 0x295c || identity.length() > 4096) throw IOException("Save or N-Gage ID is too large.")
        return SaveArchive.validate(save.readBytes(), identity.readBytes())
    }

    private fun encryptedFor(archive: SaveArchive, targetImei: String): ByteArray {
        val plain = if (PhoneSave.isPhoneSave(archive.save)) {
            PhoneSave.decrypt(archive.save, SaveArchive.identityImei(archive.identity))
        } else archive.save
        val salt = ByteArray(4).also { SecureRandom().nextBytes(it) }
        return PhoneSave.encrypt(plain, targetImei, salt)
    }

    fun export(context: Context, uri: Uri) = locked(context) {
        recoverLocked(context)
        val archive = currentSave(context)
        val encrypted = encryptedFor(archive, SaveArchive.identityImei(archive.identity))
        val output = context.contentResolver.openOutputStream(uri, "wt") ?: throw IOException("Cannot open the selected file.")
        output.use { SaveArchive(encrypted, archive.identity).write(it) }
    }

    fun exportPhone(context: Context, uri: Uri) = locked(context) {
        recoverLocked(context)
        val archive = currentSave(context)
        val encrypted = encryptedFor(archive, SaveArchive.identityImei(archive.identity))
        val output = context.contentResolver.openOutputStream(uri, "wt") ?: throw IOException("Cannot open the selected file.")
        ZipOutputStream(output).use { zip ->
            zip.putNextEntry(ZipEntry("save.dat"))
            zip.write(encrypted, 0, PhoneSave.PHONE_FILE_SIZE)
            zip.closeEntry()
            zip.putNextEntry(ZipEntry("101fd3dd.ini"))
            zip.write(encrypted, PhoneSave.PHONE_FILE_SIZE, 4)
            zip.closeEntry()
        }
    }

    fun readSelection(context: Context, uri: Uri): ByteArray {
        val input = context.contentResolver.openInputStream(uri) ?: throw IOException("Cannot open the selected file.")
        return input.use { SaveArchive.readLimited(it, 128 * 1024) }
    }

    fun restore(context: Context, archive: SaveArchive) = locked(context) {
        recoverLocked(context)
        val bytes = ByteArrayOutputStream().also { archive.write(it) }.toByteArray()
        atomicWrite(journal(context), bytes)
        recoverLocked(context)
    }
}
