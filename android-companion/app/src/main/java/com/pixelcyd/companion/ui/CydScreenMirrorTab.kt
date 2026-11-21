package com.pixelcyd.companion.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.pixelcyd.companion.protocol.CompanionProtocol
import com.pixelcyd.companion.store.CompanionRepository
import com.pixelcyd.companion.store.CompanionUiState
import com.pixelcyd.companion.store.ScreenMirrorStore
import kotlin.math.hypot
import kotlin.math.roundToInt

@Composable
fun CydScreenMirrorTab(
    companionState: CompanionUiState
) {
    val mirrorState by ScreenMirrorStore.state.collectAsState()
    val connected = companionState.connectedDevices > 0
    var showHitboxes by remember { mutableStateOf(false) }
    var lastTapPoint by remember { mutableStateOf<Offset?>(null) }

    // Auto-start stream when opening this tab while connected; stop when leaving tab
    DisposableEffect(connected) {
        if (connected) {
            CompanionRepository.sendCommandToCyd(CompanionProtocol.mirrorControl(true))
            CompanionRepository.sendCommandToCyd(CompanionProtocol.uiTreeRequest())
        }
        onDispose {
            if (connected) {
                CompanionRepository.sendCommandToCyd(CompanionProtocol.mirrorControl(false))
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(horizontal = 16.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        // 1. Stream Control Header Card
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 14.dp, vertical = 10.dp),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    val dotColor = when {
                        connected && mirrorState.streaming -> Color(0xFF25D366)
                        connected -> Color(0xFF8AB4F8)
                        else -> Color(0xFFF28B82)
                    }
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .clip(CircleShape)
                            .background(dotColor)
                    )
                    Spacer(Modifier.width(8.dp))
                    Column {
                        Text(
                            text = when {
                                !connected -> "CYD Offline"
                                mirrorState.streaming -> "Live • ${mirrorState.uiTree.activeName}"
                                else -> "Connected (Paused)"
                            },
                            fontWeight = FontWeight.SemiBold,
                            fontSize = 14.sp,
                            color = Color(0xFFE8EAED)
                        )
                        Text(
                            text = if (connected) {
                                "${mirrorState.uiTree.elements.size} UI controls mapped"
                            } else {
                                "Link your CYD on the Dashboard tab"
                            },
                            fontSize = 11.sp,
                            color = Color(0xFF9AA0A6)
                        )
                    }
                }

                Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    FilterChip(
                        selected = showHitboxes,
                        onClick = { showHitboxes = !showHitboxes },
                        label = { Text("UI Map", fontSize = 11.sp) }
                    )
                    Button(
                        enabled = connected,
                        onClick = {
                            val next = !mirrorState.streaming
                            CompanionRepository.sendCommandToCyd(CompanionProtocol.mirrorControl(next))
                            if (next) {
                                CompanionRepository.sendCommandToCyd(CompanionProtocol.uiTreeRequest())
                            }
                        },
                        colors = ButtonDefaults.buttonColors(
                            containerColor = if (mirrorState.streaming) Color(0xFF2C313F) else Color(0xFF25D366),
                            contentColor = if (mirrorState.streaming) Color(0xFFE8EAED) else Color(0xFF062A14)
                        ),
                        shape = RoundedCornerShape(10.dp)
                    ) {
                        Text(if (mirrorState.streaming) "Stop" else "Start", fontSize = 12.sp)
                    }
                }
            }
        }

        // 2. Interactive Clickable & Draggable CYD Screen Bezel (exact 240x320 / 3:4 portrait)
        Box(
            modifier = Modifier
                .fillMaxWidth(0.84f)
                .aspectRatio(240f / 320f)
                .clip(RoundedCornerShape(22.dp))
                .background(Color(0xFF090B10))
                .border(2.dp, Color(0xFF2C313F), RoundedCornerShape(22.dp))
                .pointerInput(connected) {
                    if (!connected) return@pointerInput
                    awaitEachGesture {
                        val down = awaitFirstDown(requireUnconsumed = false)
                        val startPos = down.position
                        var lastPos = startPos
                        var movedFar = false
                        var lastSendMs = 0L

                        fun toCydX(px: Float): Int =
                            ((px / size.width.coerceAtLeast(1)) * 239f).roundToInt().coerceIn(0, 239)

                        fun toCydY(py: Float): Int =
                            ((py / size.height.coerceAtLeast(1)) * 319f).roundToInt().coerceIn(0, 319)

                        lastTapPoint = startPos

                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull() ?: break
                            if (!change.pressed) {
                                val cx = toCydX(lastPos.x)
                                val cy = toCydY(lastPos.y)
                                if (!movedFar) {
                                    CompanionRepository.sendCommandToCyd(
                                        CompanionProtocol.remoteInputTap(cx, cy)
                                    )
                                } else {
                                    CompanionRepository.sendCommandToCyd(
                                        CompanionProtocol.remoteInputPointer("up", cx, cy)
                                    )
                                }
                                break
                            }

                            val cur = change.position
                            lastTapPoint = cur
                            if (!movedFar && hypot(cur.x - startPos.x, cur.y - startPos.y) > 12f) {
                                movedFar = true
                                CompanionRepository.sendCommandToCyd(
                                    CompanionProtocol.remoteInputPointer("down", toCydX(startPos.x), toCydY(startPos.y))
                                )
                                lastSendMs = System.currentTimeMillis()
                            }

                            if (movedFar) {
                                val now = System.currentTimeMillis()
                                if (now - lastSendMs >= 35L) {
                                    lastSendMs = now
                                    CompanionRepository.sendCommandToCyd(
                                        CompanionProtocol.remoteInputPointer("move", toCydX(cur.x), toCydY(cur.y))
                                    )
                                }
                            }
                            lastPos = cur
                        }
                    }
                },
            contentAlignment = Alignment.Center
        ) {
            val bmp = mirrorState.frameBitmap
            if (bmp != null) {
                Image(
                    bitmap = bmp.asImageBitmap(),
                    contentDescription = "Live CYD Screen",
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.FillBounds,
                    filterQuality = FilterQuality.Medium
                )
            } else {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(6.dp),
                    modifier = Modifier.padding(20.dp)
                ) {
                    Text(
                        text = if (connected) "Waiting for CYD frame..." else "CYD Not Connected",
                        color = Color(0xFF9AA0A6),
                        fontWeight = FontWeight.Medium,
                        fontSize = 14.sp
                    )
                    Text(
                        text = "Tap Start above to mirror and control your CYD screen live.",
                        color = Color(0xFF6E747E),
                        fontSize = 12.sp
                    )
                }
            }

            // Optional programmatic UI hitbox overlay + live touch indicator
            Canvas(modifier = Modifier.fillMaxSize()) {
                if (showHitboxes) {
                    val sx = size.width / mirrorState.uiTree.width.coerceAtLeast(1)
                    val sy = size.height / mirrorState.uiTree.height.coerceAtLeast(1)
                    for (el in mirrorState.uiTree.elements) {
                        val color = when (el.type) {
                            "switch" -> Color(0xFF25D366)
                            "slider" -> Color(0xFFFDD663)
                            "textarea" -> Color(0xFFC5B4E3)
                            else -> Color(0xFF78D9EC)
                        }
                        drawRect(
                            color = color.copy(alpha = 0.75f),
                            topLeft = Offset(el.x * sx, el.y * sy),
                            size = Size(el.w * sx, el.h * sy),
                            style = Stroke(width = 2.dp.toPx())
                        )
                    }
                }
                lastTapPoint?.let { pt ->
                    drawCircle(
                        color = Color(0xFF78D9EC).copy(alpha = 0.55f),
                        radius = 12.dp.toPx(),
                        center = pt,
                        style = Stroke(width = 2.dp.toPx())
                    )
                }
            }
        }

        // 3. Hardware Navigation & Sync Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedButton(
                enabled = connected,
                onClick = { CompanionRepository.sendCommandToCyd(CompanionProtocol.remoteCmd("back")) },
                modifier = Modifier.weight(1f)
            ) { Text("Back", fontSize = 12.sp) }

            OutlinedButton(
                enabled = connected,
                onClick = { CompanionRepository.sendCommandToCyd(CompanionProtocol.remoteCmd("home")) },
                modifier = Modifier.weight(1f)
            ) { Text("Home", fontSize = 12.sp) }

            OutlinedButton(
                enabled = connected,
                onClick = { CompanionRepository.sendCommandToCyd(CompanionProtocol.remoteCmd("quick_settings")) },
                modifier = Modifier.weight(1f)
            ) { Text("Quick", fontSize = 12.sp) }

            OutlinedButton(
                enabled = connected,
                onClick = {
                    CompanionRepository.sendCommandToCyd(CompanionProtocol.remoteCmd("refresh_ui"))
                    CompanionRepository.sendCommandToCyd(CompanionProtocol.uiTreeRequest())
                },
                modifier = Modifier.weight(1f)
            ) { Text("Sync", fontSize = 12.sp) }
        }

        // 4. Programmatic On-Screen Controls (populated live from CYD ui_tree)
        val labeledElements = mirrorState.uiTree.elements.filter { it.label.isNotBlank() }
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(
                modifier = Modifier.padding(14.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(
                    text = "Detected Screen Controls (${labeledElements.size})",
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 13.sp,
                    color = Color(0xFFE8EAED)
                )
                if (labeledElements.isEmpty()) {
                    Text(
                        text = "Tap Sync to fetch clickable buttons & coordinates from the CYD.",
                        fontSize = 12.sp,
                        color = Color(0xFF9AA0A6)
                    )
                } else {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .horizontalScroll(rememberScrollState()),
                        horizontalArrangement = Arrangement.spacedBy(6.dp)
                    ) {
                        for (el in labeledElements) {
                            OutlinedButton(
                                enabled = connected,
                                onClick = {
                                    CompanionRepository.sendCommandToCyd(
                                        CompanionProtocol.remoteInputTap(el.cx, el.cy)
                                    )
                                },
                                shape = RoundedCornerShape(10.dp)
                            ) {
                                Text(
                                    text = "${el.label} (${el.cx},${el.cy})",
                                    fontSize = 11.sp
                                )
                            }
                        }
                    }
                }

                Spacer(Modifier.height(2.dp))
                Text(
                    text = "Quick Launch App (Programmatic API)",
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 12.sp,
                    color = Color(0xFF9AA0A6)
                )
                val quickApps = listOf(
                    "trackpad" to "Trackpad",
                    "mirror" to "Mirror",
                    "wifi" to "Wi-Fi",
                    "bluetooth" to "Bluetooth",
                    "whatsapp" to "WhatsApp",
                    "settings" to "Settings"
                )
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .horizontalScroll(rememberScrollState()),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    for ((appId, title) in quickApps) {
                        Button(
                            enabled = connected,
                            onClick = {
                                CompanionRepository.sendCommandToCyd(
                                    CompanionProtocol.remoteCmd("launch_app", "app_id", appId)
                                )
                            },
                            colors = ButtonDefaults.buttonColors(
                                containerColor = Color(0xFF222631),
                                contentColor = Color(0xFFE8EAED)
                            ),
                            shape = RoundedCornerShape(10.dp)
                        ) {
                            Text(title, fontSize = 11.sp)
                        }
                    }
                }
            }
        }
    }
}
