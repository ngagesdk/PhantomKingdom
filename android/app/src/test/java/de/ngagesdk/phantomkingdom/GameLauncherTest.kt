/* SPDX-License-Identifier: MIT */
package de.ngagesdk.phantomkingdom

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class GameLauncherTest {
    private val game = GameInfo(files = 1, bytes = 1, source = "test", importedAt = 0, dataDir = null)

    @Test fun smoothRenderingIsEnabledByDefault() {
        val settings = AppSettings()
        assertTrue(settings.smooth)
        assertTrue("--smooth" in GameLauncher.arguments(settings, game))
    }

    @Test fun disablingSmoothRenderingOmitsOnlyItsFlag() {
        val settings = AppSettings(haptics = false, smooth = false)
        val args = GameLauncher.arguments(settings, game)
        assertFalse("--smooth" in args)
        assertTrue("--no-haptics" in args)
        assertTrue("--arena-host" in args)
        assertTrue("--smooth" in GameLauncher.arguments(settings.copy(smooth = true), game))
    }
}
