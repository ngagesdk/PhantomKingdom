/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import java.io.IOException
import java.security.GeneralSecurityException
import javax.crypto.Cipher
import javax.crypto.spec.SecretKeySpec

internal object PhoneSave {
    const val ENCRYPTED_SIZE = 0x295c
    const val PHONE_FILE_SIZE = ENCRYPTED_SIZE - 4
    private const val PLAIN_SIZE = PHONE_FILE_SIZE

    fun isPhoneSave(data: ByteArray) = data.size == ENCRYPTED_SIZE

    fun joinPhoneFiles(save: ByteArray, salt: ByteArray): ByteArray {
        if (save.size != PHONE_FILE_SIZE) throw IOException("Invalid N-Gage save size.")
        if (salt.size != 4) throw IOException("The N-Gage salt file must be exactly 4 bytes.")
        return save + salt
    }

    fun requirePhoneSave(data: ByteArray) {
        if (!isPhoneSave(data)) {
            throw IOException("Unsupported save size: ${data.size} bytes. Expected $ENCRYPTED_SIZE bytes for an encrypted N-Gage save. The save may use a different format or be incomplete.")
        }
    }

    private fun reverseWords(data: ByteArray) {
        for (i in data.indices step 4) {
            val a = data[i]
            val b = data[i + 1]
            data[i] = data[i + 3]
            data[i + 1] = data[i + 2]
            data[i + 2] = b
            data[i + 3] = a
        }
    }

    private fun crypt(data: ByteArray, imei: String, salt: ByteArray, mode: Int): ByteArray {
        val words = data.copyOf()
        reverseWords(words)
        val result = try {
            val cipher = Cipher.getInstance("Blowfish/ECB/NoPadding")
            val key = imei.toByteArray(Charsets.US_ASCII) + salt
            cipher.init(mode, SecretKeySpec(key, "Blowfish"))
            cipher.doFinal(words)
        } catch (e: GeneralSecurityException) {
            throw IOException("N-Gage save encryption is unavailable on this device.", e)
        }
        reverseWords(result)
        return result
    }

    fun decrypt(data: ByteArray, imei: String): ByteArray {
        requirePhoneSave(data)
        val identity = "imei=$imei\n".toByteArray(Charsets.UTF_8)
        SaveArchive.validate(data, identity)
        val plain = crypt(data.copyOfRange(0, PLAIN_SIZE), imei, data.copyOfRange(PLAIN_SIZE, ENCRYPTED_SIZE), Cipher.DECRYPT_MODE)
        try {
            SaveArchive.validate(plain, identity)
        } catch (e: IOException) {
            throw IOException("The IMEI does not match the save, or the save is damaged.", e)
        }
        return plain
    }

    fun validate(data: ByteArray, imei: String): SaveArchive {
        decrypt(data, imei)
        return SaveArchive(data, "imei=$imei\n".toByteArray(Charsets.UTF_8))
    }

    fun encrypt(plain: ByteArray, imei: String, salt: ByteArray): ByteArray {
        if (salt.size != 4) throw IOException("Invalid N-Gage save salt.")
        val identity = "imei=$imei\n".toByteArray(Charsets.UTF_8)
        if (plain.size != PLAIN_SIZE) throw IOException("Invalid unencrypted save size.")
        SaveArchive.validate(plain, identity)
        return crypt(plain, imei, salt, Cipher.ENCRYPT_MODE) + salt
    }
}
