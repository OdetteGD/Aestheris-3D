package com.aetheris.engine

import android.app.Activity
import android.os.Bundle

class MainActivity : Activity() {
    private lateinit var viewport: AetherisEditorViewport

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val projectRoot = getExternalFilesDir(null)?.absolutePath ?: filesDir.absolutePath
        AetherisNative.nativeSetProjectRoot(projectRoot)
        viewport = AetherisEditorViewport(this)
        setContentView(viewport)
    }

    override fun onDestroy() {
        if (::viewport.isInitialized) AetherisNative.nativeShutdown()
        super.onDestroy()
    }
}
