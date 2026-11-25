import java.net.URI
import java.util.Properties
import java.util.zip.ZipInputStream

plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
}

// The SDL3 version comes from the top-level CMakeLists.txt (PK_SDL3_TAG), so
// the Java half of SDL always matches the native libSDL3.so.
val sdlTag: String = Regex("""set\(PK_SDL3_TAG\s+"([^"]+)"""")
    .find(rootProject.file("../CMakeLists.txt").readText())
    ?.groupValues?.get(1)
    ?: throw GradleException("PK_SDL3_TAG not found in CMakeLists.txt")

/* SDL's Java activity (org.libsdl.app) is not part of this repository: it
 * is taken from the SDL source of the same tag, either a local checkout
 * (-Ppk.sdlSource=/path/to/SDL) or the release archive on GitHub. */
abstract class FetchSdlJava : DefaultTask() {
    @get:Input abstract val tag: Property<String>
    @get:Optional @get:Input abstract val localSource: Property<String>
    /* the downloaded archive, shared by the variants' tasks */
    @get:Internal abstract val archive: RegularFileProperty
    @get:OutputDirectory abstract val outputDir: DirectoryProperty

    @TaskAction
    fun fetch() {
        val out = outputDir.get().asFile
        out.deleteRecursively()
        out.mkdirs()
        val marker = "android-project/app/src/main/java/"
        val local = localSource.orNull
        if (local != null) {
            val src = File(local, marker)
            if (!src.isDirectory) throw GradleException("$src does not exist")
            src.copyRecursively(out, overwrite = true)
        } else {
            val zipFile = archive.get().asFile
            if (!zipFile.isFile) {
                val url = URI("https://github.com/libsdl-org/SDL/archive/refs/tags/${tag.get()}.zip").toURL()
                val part = File(zipFile.path + ".part")
                zipFile.parentFile.mkdirs()
                url.openStream().use { input -> part.outputStream().use { input.copyTo(it) } }
                if (!part.renameTo(zipFile)) throw GradleException("cannot write $zipFile")
            }
            ZipInputStream(zipFile.inputStream().buffered()).use { zip ->
                while (true) {
                    val e = zip.nextEntry ?: break
                    val i = e.name.indexOf("/$marker")
                    if (i < 0 || e.isDirectory || !e.name.endsWith(".java")) continue
                    val f = File(out, e.name.substring(i + marker.length + 1))
                    f.parentFile.mkdirs()
                    f.outputStream().use { zip.copyTo(it) }
                }
            }
        }
        if (out.walkTopDown().none { it.name == "SDLActivity.java" }) {
            throw GradleException("SDLActivity.java not found in SDL ${tag.get()}")
        }
    }
}

android {
    namespace = "de.ngagesdk.phantomkingdom"
    compileSdk = 36

    defaultConfig {
        applicationId = "de.ngagesdk.phantomkingdom"
        minSdk = 24
        targetSdk = 36
        // versionCode: whole number, +1 for every published build.
        versionCode = 1
        versionName = "1.0.0"
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        externalNativeBuild {
            cmake {
                arguments += listOf("-DPK_BUILD_TESTS=OFF", "-DANDROID_STL=c++_static")
            }
        }
    }

    // Release signing: create android/keystore.properties (see android/README.md).
    val keystoreFile = rootProject.file("keystore.properties")
    val keystore = Properties().apply { if (keystoreFile.exists()) keystoreFile.inputStream().use { load(it) } }
    signingConfigs {
        if (keystoreFile.exists()) {
            create("release") {
                storeFile = rootProject.file(keystore.getProperty("storeFile"))
                storePassword = keystore.getProperty("storePassword")
                keyAlias = keystore.getProperty("keyAlias")
                keyPassword = keystore.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        release {
            signingConfig = signingConfigs.findByName("release")
            // SDL calls its Java methods by name over JNI: no shrinking
            isMinifyEnabled = false
        }
    }

    // The whole game is the top-level CMake project; on Android it builds
    // libSDL3.so and libmain.so (see CMakeLists.txt). CMake >= 3.24 is
    // needed; CI passes the SDK CMake it installed as -Ppk.cmakeVersion.
    externalNativeBuild {
        cmake {
            path = file("../../CMakeLists.txt")
            version = providers.gradleProperty("pk.cmakeVersion").getOrElse("3.31.6")
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    // The start screen (game data import, server settings) is Jetpack
    // Compose; the game itself stays SDL (PocketKingdomActivity).
    buildFeatures {
        compose = true
    }

    lint {
        abortOnError = false
    }
}

dependencies {
    // StateFlow and coroutines are used directly by the start screen
    // (GameData, ServerCheck, AppLog), so they are declared, not taken
    // over from the lifecycle libraries.
    implementation(libs.kotlinx.coroutines.android)
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.core)
    debugImplementation(libs.androidx.compose.ui.tooling)
    testImplementation("junit:junit:4.13.2")
}

androidComponents {
    onVariants { variant ->
        val name = variant.name.replaceFirstChar { it.uppercase() }
        val fetch = tasks.register<FetchSdlJava>("fetchSdlJava$name") {
            tag.set(sdlTag)
            localSource.set(providers.gradleProperty("pk.sdlSource"))
            archive.set(layout.buildDirectory.file("sdl/SDL-$sdlTag.zip"))
        }
        variant.sources.java?.addGeneratedSourceDirectory(fetch, FetchSdlJava::outputDir)
        // the font files shipped with the program (assets/fonts); no game data
        variant.sources.assets?.addStaticSourceDirectory(rootProject.file("../assets").absolutePath)
    }
}
