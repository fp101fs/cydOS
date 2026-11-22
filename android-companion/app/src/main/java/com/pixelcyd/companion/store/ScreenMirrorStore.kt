package com.pixelcyd.companion.store

import android.graphics.Bitmap
import com.pixelcyd.companion.protocol.CompanionProtocol.CydUiTree
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

data class ScreenMirrorUiState(
    val streaming: Boolean = false,
    val remoteEnabled: Boolean = true,
    val frameBitmap: Bitmap? = null,
    val frameGen: Long = 0L,
    val bandsReceived: Long = 0L,
    val uiTree: CydUiTree = CydUiTree()
)

object ScreenMirrorStore {
    private var mirrorW = 80
    private var mirrorH = 108

    private val lock = Any()
    private var pixels = IntArray(mirrorW * mirrorH) { 0xFF0F1117.toInt() }
    private var frameGen = 0L
    private var bandsReceived = 0L

    private val _state = MutableStateFlow(ScreenMirrorUiState())
    val state: StateFlow<ScreenMirrorUiState> = _state.asStateFlow()

    // Decodes a binary 'CY' PackBits 16-bit Big-Endian RGB565 band frame
    fun decodeBandFrame(data: ByteArray) {
        if (data.size < 10) return
        if (data[0].toInt() != 'C'.code || data[1].toInt() != 'Y'.code) return
        if (data[2].toInt() != 0x01) return

        val width = (data[4].toInt() and 0xFF) or ((data[5].toInt() and 0xFF) shl 8)
        val yOffset = (data[6].toInt() and 0xFF) or ((data[7].toInt() and 0xFF) shl 8)
        val bandH = (data[8].toInt() and 0xFF) or ((data[9].toInt() and 0xFF) shl 8)

        if (width <= 0 || width > 240 || yOffset < 0 || bandH <= 0 || yOffset + bandH > 320) return

        val totalPx = width * bandH

        val snapshot: Bitmap
        val nextGen: Long
        val nextBands: Long

        synchronized(lock) {
            val expectedH = if (width == 80) 108 else if (width == 120) 160 else maxOf(mirrorH, yOffset + bandH)
            if (width != mirrorW || expectedH != mirrorH) {
                mirrorW = width
                mirrorH = expectedH
                pixels = IntArray(mirrorW * mirrorH) { 0xFF0F1117.toInt() }
            }
            if (yOffset + bandH > mirrorH) return
            val startOffset = yOffset * mirrorW

            var pos = 10
            var pxOut = 0
            while (pxOut < totalPx && pos < data.size) {
                val ctrl = data[pos++].toInt() and 0xFF
                if ((ctrl and 0x80) != 0) {
                    val runLen = (ctrl and 0x7F) + 1
                    if (pos + 1 >= data.size) break
                    val argb = rgb565BeToArgb(data[pos], data[pos + 1])
                    pos += 2
                    val end = minOf(pxOut + runLen, totalPx)
                    while (pxOut < end) {
                        pixels[startOffset + pxOut] = argb
                        pxOut++
                    }
                } else {
                    val litLen = ctrl + 1
                    val end = minOf(pxOut + litLen, totalPx)
                    while (pxOut < end && pos + 1 < data.size) {
                        pixels[startOffset + pxOut] = rgb565BeToArgb(data[pos], data[pos + 1])
                        pos += 2
                        pxOut++
                    }
                }
            }
            frameGen++
            bandsReceived++
            nextGen = frameGen
            nextBands = bandsReceived
            snapshot = Bitmap.createBitmap(pixels, mirrorW, mirrorH, Bitmap.Config.ARGB_8888)
        }

        _state.value = _state.value.copy(
            streaming = true,
            frameBitmap = snapshot,
            frameGen = nextGen,
            bandsReceived = nextBands
        )
    }

    fun updateUiTree(tree: CydUiTree) {
        _state.value = _state.value.copy(
            streaming = tree.streaming,
            remoteEnabled = tree.remoteEnabled,
            uiTree = tree
        )
    }

    fun updateMirrorStatus(streaming: Boolean, remoteEnabled: Boolean, activeApp: String) {
        val cur = _state.value
        _state.value = cur.copy(
            streaming = streaming,
            remoteEnabled = remoteEnabled,
            uiTree = cur.uiTree.copy(activeApp = activeApp, streaming = streaming, remoteEnabled = remoteEnabled)
        )
    }

    fun onDisconnected() {
        _state.value = _state.value.copy(streaming = false)
    }

    private fun rgb565BeToArgb(hi: Byte, lo: Byte): Int {
        val v = ((hi.toInt() and 0xFF) shl 8) or (lo.toInt() and 0xFF)
        val r5 = (v ushr 11) and 0x1F
        val g6 = (v ushr 5) and 0x3F
        val b5 = v and 0x1F
        val r = (r5 shl 3) or (r5 ushr 2)
        val g = (g6 shl 2) or (g6 ushr 4)
        val b = (b5 shl 3) or (b5 ushr 2)
        return (0xFF shl 24) or (r shl 16) or (g shl 8) or b
    }
}
