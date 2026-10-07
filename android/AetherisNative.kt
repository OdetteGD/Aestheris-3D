package com.aetheris.engine

import android.view.Surface

object AetherisNative {
    init { System.loadLibrary("aetheris") }

    @JvmStatic external fun nativeSetProjectRoot(root: String)
    @JvmStatic external fun nativeInitialize(surface: Surface, api: Int): Boolean
    @JvmStatic external fun nativeSwitchApi(surface: Surface, api: Int): Boolean
    @JvmStatic external fun nativeSurfaceChanged(surface: Surface)
    @JvmStatic external fun nativeShutdown()

    const val API_VULKAN = 0
    const val API_OPENGL_ES3 = 1
}
