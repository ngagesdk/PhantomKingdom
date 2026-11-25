/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.IOException
import java.util.zip.CRC32
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Test

class PhoneSaveTest {
    private val imei = "490154203237518"
    private val identity = "imei=$imei\n".toByteArray()
    private val salt = byteArrayOf(0, 1, 2, 3)

    private fun save(): ByteArray {
        val data = ByteArray(0x2958) { (it * 17).toByte() }
        val crc = CRC32().apply { update(data, 0, data.size - 4) }.value
        repeat(4) { data[data.size - 4 + it] = (crc shr (it * 8)).toByte() }
        return data
    }

    private fun backup(save: ByteArray, id: ByteArray = identity): ByteArray {
        val output = ByteArrayOutputStream()
        SaveArchive(save, id).write(output)
        return output.toByteArray()
    }

    @Test fun encryptsPhoneFormatWithKnownIndependentBlowfishVector() {
        val data = save()
        val encrypted = PhoneSave.encrypt(data, imei, salt)
        assertEquals(0x295c, encrypted.size)
        // OpenSSL Blowfish ECB of 3322110077665544 under ASCII IMEI + 00010203,
        // reversed per 32-bit word as in pk_blowfish_ecb.
        assertArrayEquals(byteArrayOf(0x74, 0xac.toByte(), 0xf9.toByte(), 0xe0.toByte(), 0x10, 0x29, 0x52, 0x9d.toByte()), encrypted.copyOfRange(0, 8))
        assertArrayEquals(salt, encrypted.copyOfRange(0x2958, 0x295c))
        assertArrayEquals(data, PhoneSave.decrypt(encrypted, imei))
    }

    @Test fun rejectsWrongIdentityCorruptionAndPlaintext() {
        val encrypted = PhoneSave.encrypt(save(), imei, salt)
        assertTrue(PhoneSave.isPhoneSave(encrypted))
        assertFalse(PhoneSave.isPhoneSave(save()))
        assertThrows(IOException::class.java) { PhoneSave.decrypt(encrypted, "490154203237526") }
        assertThrows(IOException::class.java) { PhoneSave.decrypt(encrypted.copyOf().apply { this[100] = (this[100] + 1).toByte() }, imei) }
        assertThrows(IOException::class.java) { PhoneSave.decrypt(save(), imei) }
        assertThrows(IOException::class.java) { PhoneSave.encrypt(save(), "490154203237519", salt) }
        assertThrows(IOException::class.java) { PhoneSave.encrypt(save().apply { this[1] = 0 }, imei, salt) }
    }

    @Test fun reportsUnsupportedPhoneSaveSize() {
        val error = assertThrows(IOException::class.java) { PhoneSave.requirePhoneSave(ByteArray(0x2958)) }
        assertTrue(error.message!!.contains("10584 bytes"))
        assertTrue(error.message!!.contains("10588 bytes"))
        PhoneSave.requirePhoneSave(ByteArray(0x295c))
    }

    @Test fun importsSplitPhoneSaveAndSalt() {
        val encrypted = PhoneSave.encrypt(save(), imei, salt)
        val phoneFile = encrypted.copyOfRange(0, PhoneSave.PHONE_FILE_SIZE)
        assertFalse(SaveArchive.isPlainSave(phoneFile))
        assertTrue(SaveArchive.isPlainSave(save()))
        assertArrayEquals(encrypted, PhoneSave.joinPhoneFiles(phoneFile, salt))
        assertArrayEquals(save(), PhoneSave.decrypt(PhoneSave.joinPhoneFiles(phoneFile, salt), imei))
        assertThrows(IOException::class.java) { PhoneSave.joinPhoneFiles(phoneFile, ByteArray(3)) }
        assertThrows(IOException::class.java) { PhoneSave.joinPhoneFiles(phoneFile, byteArrayOf(4, 3, 2, 1)).let { PhoneSave.decrypt(it, imei) } }
    }

    @Test fun importAcceptsEncryptedBackupsOnlyAndChecksMatchingIdentity() {
        val encrypted = PhoneSave.encrypt(save(), imei, salt)
        val zip = backup(encrypted)
        assertTrue(SaveArchive.isZip(zip))
        val archive = SaveArchive.readImport(ByteArrayInputStream(zip))
        assertArrayEquals(encrypted, archive.save)
        assertArrayEquals(identity, archive.identity)
        assertThrows(IOException::class.java) { SaveArchive.readImport(ByteArrayInputStream(backup(save()))) }
        assertThrows(IOException::class.java) {
            SaveArchive.readImport(ByteArrayInputStream(backup(encrypted, "imei=490154203237526\n".toByteArray())))
        }
    }
}
