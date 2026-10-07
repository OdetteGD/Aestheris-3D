package com.aetheris.engine

import android.app.Activity
import android.os.Bundle
import java.io.File

class MainActivity : Activity() {
    private lateinit var viewport: AetherisEditorViewport

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val projectRoot =
            getExternalFilesDir(null)?.absolutePath
                ?: filesDir.absolutePath

        // Packaged SPIR-V is copied once into the persistent project sandbox.
        // The native renderer only reads these immutable files during boot.
        copyAssetTree("shaders/spirv", File(projectRoot, "assets/shaders/spirv"))

        AetherisNative.nativeSetProjectRoot(projectRoot)

        viewport = AetherisEditorViewport(this)
        setContentView(viewport)
    }

    private fun copyAssetTree(assetPath: String, destination: File) {
        val entries = assets.list(assetPath) ?: return

        destination.mkdirs()

        for (entry in entries) {
            val childAsset = "$assetPath/$entry"
            val childFile = File(destination, entry)
            val nested = assets.list(childAsset)

            if (!nested.isNullOrEmpty()) {
                copyAssetTree(childAsset, childFile)
            } else {
                try {
                    assets.open(childAsset).use { input ->
                        childFile.outputStream().use { output ->
                            input.copyTo(output)
                        }
                    }
                } catch (_: Exception) {
                    // Native startup has a deterministic procedural fallback.
                }
            }
        }
    }

    override fun onDestroy() {
        if (::viewport.isInitialized) {
            AetherisNative.nativeShutdown()
        }
        super.onDestroy()
    }
}
