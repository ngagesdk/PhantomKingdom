/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import java.io.ByteArrayOutputStream
import java.io.IOException
import java.io.InputStream
import java.io.OutputStream
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

internal data class SaveArchive(val save: ByteArray, val identity: ByteArray) {
    companion object {
        private const val SAVE_SIZE = 0x2958
        private const val IDENTITY_LIMIT = 4096

        fun isZip(data: ByteArray): Boolean = data.size >= 4 &&
            data[0] == 0x50.toByte() && data[1] == 0x4b.toByte() &&
            data[2] == 0x03.toByte() && data[3] == 0x04.toByte()

        fun readLimited(input: InputStream, limit: Int): ByteArray {
            val out = ByteArrayOutputStream()
            val buffer = ByteArray(4096)
            while (true) {
                val count = input.read(buffer)
                if (count < 0) break
                if (out.size() + count > limit) throw IOException("Backup entry is too large.")
                out.write(buffer, 0, count)
            }
            return out.toByteArray()
        }

        fun isPlainSave(save: ByteArray): Boolean {
            if (save.size != SAVE_SIZE) return false
            val crc = CRC32().apply { update(save, 0, SAVE_SIZE - 4) }.value
            val stored = (0 until 4).fold(0L) { v, i -> v or ((save[SAVE_SIZE - 4 + i].toLong() and 255) shl (8 * i)) }
            return crc == stored
        }

        fun validate(save: ByteArray, identity: ByteArray): SaveArchive {
            if (save.size != SAVE_SIZE && save.size != SAVE_SIZE + 4) throw IOException("Invalid save size.")
            if (save.size == SAVE_SIZE && !isPlainSave(save)) throw IOException("The save checksum is invalid.")
            identityImei(identity)
            return SaveArchive(save, identity)
        }

        fun identityImei(identity: ByteArray): String {
            if (identity.size > IDENTITY_LIMIT) throw IOException("Identity file is too large.")
            val lines = identity.toString(Charsets.UTF_8).split('\n', '\r')
            val ids = lines.filter { it.startsWith("imei=") }
            val imei = ids.singleOrNull()?.removePrefix("imei=")
            if (imei == null || imei.length != 15 || imei.any { it !in '0'..'9' }) throw IOException("Invalid N-Gage ID.")
            val sum = imei.mapIndexed { i, c ->
                val n = (c - '0') * if (i < 14 && i % 2 == 1) 2 else 1
                if (n > 9) n - 9 else n
            }.sum()
            if (sum % 10 != 0) throw IOException("Invalid N-Gage ID check digit.")
            return imei
        }

        fun readImport(input: InputStream): SaveArchive {
            val archive = read(input)
            if (!PhoneSave.isPhoneSave(archive.save)) throw IOException("The backup does not contain an encrypted save.")
            PhoneSave.decrypt(archive.save, identityImei(archive.identity))
            return archive
        }

        fun read(input: InputStream): SaveArchive {
            var save: ByteArray? = null
            var identity: ByteArray? = null
            ZipInputStream(input).use { zip ->
                while (true) {
                    val entry = zip.nextEntry ?: break
                    when (entry.name) {
                        "save.dat" -> {
                            if (save != null || entry.isDirectory) throw IOException("Duplicate save entry.")
                            save = readLimited(zip, SAVE_SIZE + 4)
                        }
                        "identity.cfg" -> {
                            if (identity != null || entry.isDirectory) throw IOException("Duplicate identity entry.")
                            identity = readLimited(zip, IDENTITY_LIMIT)
                        }
                        else -> throw IOException("Unexpected backup entry: ${entry.name}")
                    }
                    zip.closeEntry()
                }
            }
            return validate(save ?: throw IOException("Backup has no save."), identity ?: throw IOException("Backup has no N-Gage ID."))
        }
    }

    fun write(output: OutputStream) {
        ZipOutputStream(output).use { zip ->
            for ((name, bytes) in listOf("save.dat" to save, "identity.cfg" to identity)) {
                zip.putNextEntry(ZipEntry(name))
                zip.write(bytes)
                zip.closeEntry()
            }
        }
    }
}
