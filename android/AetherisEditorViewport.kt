package com.aetheris.engine

import android.content.Context
import android.util.AttributeSet
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

class AetherisEditorViewport @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : SurfaceView(context, attrs), SurfaceHolder.Callback {

    init { holder.addCallback(this) }

    override fun surfaceCreated(holder: SurfaceHolder) {
        AetherisNative.nativeRegisterEditorSurface(holder.surface)
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        AetherisNative.nativeResizeViewport(holder.surface, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        AetherisNative.nativeReleaseSurface()
    }
}
