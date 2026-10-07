package com.aetheris.engine

import android.app.Activity
import android.graphics.Color
import android.os.Bundle
import android.view.Gravity
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
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
        copyAssetTree("shaders", File(projectRoot, "assets/shaders"))
        copyAssetTree("models/kenney", File(projectRoot, "assets/models/kenney"))
        copyAssetTree("textures", File(projectRoot, "assets/textures"))
        copyAssetTree("fallback_shaders", File(projectRoot, "assets/fallback_shaders"))
        copyAssetTree("csharp", File(projectRoot, "assets/csharp"))

        AetherisNative.nativeSetProjectRoot(projectRoot)

        viewport = AetherisEditorViewport(this)

        val root = FrameLayout(this)
        root.addView(
            viewport,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        val overlay = TextView(this).apply {
            text = "AETHERIS  •  GLES3 mobile viewport"
            setTextColor(Color.WHITE)
            setBackgroundColor(0x990B1420.toInt())
            setPadding(18, 10, 18, 10)
            gravity = Gravity.CENTER_VERTICAL
            isClickable = false
            isFocusable = false
            importantForAccessibility = View.IMPORTANT_FOR_ACCESSIBILITY_NO
        }

        val overlayParams = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.WRAP_CONTENT,
            FrameLayout.LayoutParams.WRAP_CONTENT,
            Gravity.TOP or Gravity.START
        )
        overlayParams.topMargin = 12
        overlayParams.leftMargin = 12
        root.addView(overlay, overlayParams)
        setContentView(root)
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
