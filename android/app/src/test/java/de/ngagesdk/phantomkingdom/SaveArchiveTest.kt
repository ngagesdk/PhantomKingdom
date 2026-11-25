/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.CRC32
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class SaveArchiveTest {
    private val identity = "# identity\nimei=490154203237518\n".toByteArray()

    private fun save(): ByteArray {
        val data = ByteArray(0x2958) { (it * 17).toByte() }
        val crc = CRC32().apply { update(data, 0, data.size - 4) }.value
        repeat(4) { data[data.size - 4 + it] = (crc shr (it * 8)).toByte() }
        return data
    }

    private fun zip(vararg entries: Pair<String, ByteArray>): ByteArray {
        val output = ByteArrayOutputStream()
        ZipOutputStream(output).use { zip ->
            entries.forEach { (name, data) -> zip.putNextEntry(ZipEntry(name)); zip.write(data); zip.closeEntry() }
        }
        return output.toByteArray()
    }

    @Test fun roundTripPreservesSaveAndIdentity() {
        val archive = SaveArchive.validate(save(), identity)
        val output = ByteArrayOutputStream()
        archive.write(output)
        val result = SaveArchive.read(ByteArrayInputStream(output.toByteArray()))
        assertArrayEquals(archive.save, result.save)
        assertArrayEquals(identity, result.identity)
    }

    @Test fun rejectsCorruptSaveAndInvalidIdentity() {
        val broken = save().apply { this[0] = (this[0] + 1).toByte() }
        assertThrows(IOException::class.java) { SaveArchive.validate(broken, identity) }
        assertThrows(IOException::class.java) { SaveArchive.validate(save(), "imei=490154203237519\n".toByteArray()) }
        assertThrows(IOException::class.java) { SaveArchive.validate(save(), "imei=490154203237518 \n".toByteArray()) }
    }

    @Test fun rejectsMissingUnexpectedAndOversizedEntries() {
        val cases = listOf(
            zip("save.dat" to save()),
            zip("save.dat" to save(), "identity.cfg" to identity, "other" to byteArrayOf()),
            zip("save.dat" to ByteArray(0x295d), "identity.cfg" to identity),
            byteArrayOf(1, 2, 3),
        )
        cases.forEach { bytes -> assertThrows(IOException::class.java) { SaveArchive.read(ByteArrayInputStream(bytes)) } }
    }

    @Test fun rejectsDuplicateEntriesAndOversizedIdentity() {
        val duplicate = zip("save.dat" to save(), "save.dax" to save(), "identity.cfg" to identity)
        val otherName = "save.dax".toByteArray()
        for (i in 0..duplicate.size - otherName.size) {
            if (otherName.indices.all { duplicate[i + it] == otherName[it] }) duplicate[i + 7] = 't'.code.toByte()
        }
        assertThrows(IOException::class.java) { SaveArchive.read(ByteArrayInputStream(duplicate)) }
        val largeIdentity = zip("save.dat" to save(), "identity.cfg" to ByteArray(4097))
        assertThrows(IOException::class.java) { SaveArchive.read(ByteArrayInputStream(largeIdentity)) }
    }

    @Test fun preservesEncryptedPhoneSaveWithoutClaimingToValidateIt() {
        val encrypted = ByteArray(0x295c) { it.toByte() }
        val result = SaveArchive.read(ByteArrayInputStream(zip("save.dat" to encrypted, "identity.cfg" to identity)))
        assertArrayEquals(encrypted, result.save)
    }
}
