/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import android.content.SharedPreferences
import java.lang.reflect.Proxy
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsStoreTest {
    private fun preferences(): SharedPreferences {
        val values = mutableMapOf<String, Any>("haptics" to false)
        val editor = Proxy.newProxyInstance(
            SharedPreferences.Editor::class.java.classLoader,
            arrayOf(SharedPreferences.Editor::class.java),
        ) { proxy, method, args ->
            when (method.name) {
                "putString", "putBoolean" -> {
                    values[args[0] as String] = args[1]
                    proxy
                }
                "apply" -> null
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SharedPreferences.Editor
        return Proxy.newProxyInstance(
            SharedPreferences::class.java.classLoader,
            arrayOf(SharedPreferences::class.java),
        ) { _, method, args ->
            when (method.name) {
                "getString", "getBoolean" -> values[args[0] as String] ?: args[1]
                "edit" -> editor
                else -> throw UnsupportedOperationException(method.name)
            }
        } as SharedPreferences
    }

    @Test fun existingSettingsDefaultToSmoothAndPersistBothChoices() {
        val prefs = preferences()
        val store = SettingsStore(prefs)
        assertTrue(store.settings.value.smooth)
        assertFalse(store.settings.value.haptics)
        store.update { it.copy(smooth = false) }
        assertFalse(SettingsStore(prefs).settings.value.smooth)
        store.update { it.copy(smooth = true) }
        assertTrue(SettingsStore(prefs).settings.value.smooth)
        assertFalse(SettingsStore(prefs).settings.value.haptics)
    }
}
