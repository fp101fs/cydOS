package com.pixelcyd.companion.ui

import android.Manifest
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.widget.Toast
import com.journeyapps.barcodescanner.ScanContract
import com.journeyapps.barcodescanner.ScanOptions
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import java.util.concurrent.Executors
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
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
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.pixelcyd.companion.notifications.WhatsAppNotificationListener
import com.pixelcyd.companion.service.CompanionService
import com.pixelcyd.companion.store.CompanionRepository
import com.pixelcyd.companion.store.CompanionUiState

class MainActivity : ComponentActivity() {

    private val pairExecutor = Executors.newSingleThreadExecutor()

    private val notifPermLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { /* Service still runs; user can allow status notification anytime */ }

    private val qrScanLauncher = registerForActivityResult(ScanContract()) { result ->
        val raw = result.contents ?: return@registerForActivityResult
        handleScannedPairingQr(raw)
    }

    private fun launchQrScanner() {
        val opts = ScanOptions().apply {
            setDesiredBarcodeFormats(ScanOptions.QR_CODE)
            setPrompt("Scan the Pairing QR on your Pixel CYD")
            setBeepEnabled(false)
            setOrientationLocked(true)
        }
        qrScanLauncher.launch(opts)
    }

    private fun handleScannedPairingQr(raw: String) {
        val uri = try { Uri.parse(raw) } catch (_: Exception) { null }
        if (uri == null || uri.scheme != "pixelcyd" || uri.host != "pair") {
            Toast.makeText(this, "Not a Pixel CYD pairing QR code", Toast.LENGTH_SHORT).show()
            return
        }
        val cydIp = uri.getQueryParameter("ip").orEmpty()
        val cydPort = uri.getQueryParameter("port")?.toIntOrNull() ?: 8766
        val pairKey = uri.getQueryParameter("key").orEmpty()
        if (cydIp.isBlank() || pairKey.isBlank()) {
            Toast.makeText(this, "Invalid pairing QR data", Toast.LENGTH_SHORT).show()
            return
        }

        val st = CompanionRepository.uiState.value
        val phoneIp = st.localIp
        if (phoneIp.isBlank()) {
            Toast.makeText(this, "Connect phone to Wi-Fi first", Toast.LENGTH_SHORT).show()
            return
        }

        Toast.makeText(this, "Pairing with $cydIp...", Toast.LENGTH_SHORT).show()
        pairExecutor.execute {
            try {
                val payload = JSONObject()
                    .put("key", pairKey)
                    .put("host", phoneIp)
                    .put("port", st.port)
                    .put("token", st.pairingToken)
                    .toString()

                val conn = (URL("http://$cydIp:$cydPort/pair").openConnection() as HttpURLConnection).apply {
                    requestMethod = "POST"
                    connectTimeout = 4500
                    readTimeout = 4500
                    doOutput = true
                    setRequestProperty("Content-Type", "application/json")
                }
                conn.outputStream.use { it.write(payload.toByteArray(Charsets.UTF_8)) }
                val code = conn.responseCode
                conn.disconnect()

                runOnUiThread {
                    if (code in 200..299) {
                        Toast.makeText(this, "Paired with Pixel CYD!", Toast.LENGTH_LONG).show()
                    } else {
                        Toast.makeText(this, "CYD rejected pairing ($code)", Toast.LENGTH_LONG).show()
                    }
                }
            } catch (e: Exception) {
                runOnUiThread {
                    Toast.makeText(this, "Could not reach CYD on Wi-Fi", Toast.LENGTH_LONG).show()
                }
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        CompanionRepository.init(applicationContext)
        requestPostNotificationsIfNeeded()
        CompanionService.start(applicationContext)

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    primary = Color(0xFF25D366),
                    onPrimary = Color(0xFF062A14),
                    background = Color(0xFF0F1117),
                    surface = Color(0xFF191C24),
                    surfaceVariant = Color(0xFF222631)
                )
            ) {
                Surface(modifier = Modifier.fillMaxSize(), color = MaterialTheme.colorScheme.background) {
                    val state by CompanionRepository.uiState.collectAsState()
                    CompanionScreen(
                        state = state,
                        onScanCydQr = { launchQrScanner() },
                        onToggleForwarding = { enabled ->
                            CompanionRepository.setForwardingEnabled(applicationContext, enabled)
                        },
                        onOpenNotifAccess = {
                            startActivity(Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS))
                        },
                        onCopyToken = {
                            val cm = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
                            cm.setPrimaryClip(ClipData.newPlainText("Pairing Token", state.pairingToken))
                            Toast.makeText(this, "Pairing token copied", Toast.LENGTH_SHORT).show()
                        },
                        onRegenerateToken = {
                            CompanionRepository.regenerateToken(applicationContext)
                        },
                        onToggleServer = {
                            if (state.serverRunning) {
                                CompanionService.stop(applicationContext)
                            } else {
                                CompanionService.start(applicationContext)
                            }
                        },
                        onClearHistory = {
                            CompanionRepository.clearHistory()
                        }
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        val granted = WhatsAppNotificationListener.isNotificationAccessGranted(this)
        val prev = CompanionRepository.uiState.value
        CompanionRepository.updateListenerStatus(granted, prev.listenerBound)
    }

    private fun requestPostNotificationsIfNeeded() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val granted = ContextCompat.checkSelfPermission(
                this,
                Manifest.permission.POST_NOTIFICATIONS
            ) == PackageManager.PERMISSION_GRANTED
            if (!granted) {
                notifPermLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
            }
        }
    }
}

@Composable
private fun CompanionScreen(
    state: CompanionUiState,
    onScanCydQr: () -> Unit,
    onToggleForwarding: (Boolean) -> Unit,
    onOpenNotifAccess: () -> Unit,
    onCopyToken: () -> Unit,
    onRegenerateToken: () -> Unit,
    onToggleServer: () -> Unit,
    onClearHistory: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        Text(
            text = "Pixel CYD Companion",
            fontSize = 24.sp,
            fontWeight = FontWeight.Bold,
            color = Color(0xFFE8EAED)
        )
        Text(
            text = "Local Wi-Fi WhatsApp bridge for ESP32 CYD (no cloud, no account)",
            fontSize = 13.sp,
            color = Color(0xFF9AA0A6)
        )

        // 1. Connection & Pairing Card
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    val dotColor = when {
                        state.connectedDevices > 0 -> Color(0xFF25D366)
                        state.serverRunning -> Color(0xFFFDD663)
                        else -> Color(0xFFF28B82)
                    }
                    Box(
                        modifier = Modifier
                            .size(10.dp)
                            .clip(CircleShape)
                            .background(dotColor)
                    )
                    Spacer(Modifier.width(8.dp))
                    Text(
                        text = when {
                            state.connectedDevices > 0 -> "CYD Connected (${state.lastDeviceName.ifBlank { "ESP32" }})"
                            state.serverRunning -> "Waiting for CYD on Wi-Fi"
                            else -> "Companion Server Stopped"
                        },
                        fontWeight = FontWeight.SemiBold,
                        color = Color(0xFFE8EAED)
                    )
                }

                InfoLine("Phone IP", state.localIp.ifBlank { "Connect to Wi-Fi" })
                InfoLine("Port", state.port.toString())
                InfoLine("Pairing Token", state.pairingToken, monospace = true)

                Button(
                    onClick = onScanCydQr,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = Color(0xFF25D366),
                        contentColor = Color(0xFF062A14)
                    )
                ) {
                    Text("Scan CYD QR Code", fontWeight = FontWeight.SemiBold)
                }

                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(onClick = onCopyToken, modifier = Modifier.weight(1f)) {
                        Text("Copy Token")
                    }
                    OutlinedButton(onClick = onRegenerateToken, modifier = Modifier.weight(1f)) {
                        Text("New Token")
                    }
                }

                Button(
                    onClick = onToggleServer,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(
                        containerColor = if (state.serverRunning) Color(0xFF2C313F) else Color(0xFF25D366),
                        contentColor = if (state.serverRunning) Color(0xFFE8EAED) else Color(0xFF062A14)
                    )
                ) {
                    Text(if (state.serverRunning) "Stop Server" else "Start Server")
                }
            }
        }

        // 2. WhatsApp Notification Forwarding Card
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text("WhatsApp Forwarding", fontWeight = FontWeight.SemiBold, color = Color(0xFFE8EAED))
                        Text(
                            text = if (state.forwardingEnabled) "Active over local Wi-Fi" else "Paused",
                            fontSize = 12.sp,
                            color = if (state.forwardingEnabled) Color(0xFF25D366) else Color(0xFF9AA0A6)
                        )
                    }
                    Switch(
                        checked = state.forwardingEnabled,
                        onCheckedChange = onToggleForwarding
                    )
                }

                InfoLine(
                    "Notification Access",
                    if (state.notificationAccessGranted) "Granted" else "Required"
                )
                InfoLine(
                    "Listener Service",
                    if (state.listenerBound) "Running" else "Not bound"
                )

                if (!state.notificationAccessGranted) {
                    Text(
                        text = "Android requires Notification Access to read WhatsApp alerts and send quick replies via Android's official RemoteInput API. Message content never leaves your local Wi-Fi.",
                        fontSize = 12.sp,
                        color = Color(0xFF9AA0A6)
                    )
                }

                Button(
                    onClick = onOpenNotifAccess,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(
                        if (state.notificationAccessGranted) "Notification Access Settings"
                        else "Grant Notification Access"
                    )
                }
            }
        }

        // 3. Status & Troubleshooting Card
        Card(
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            shape = RoundedCornerShape(16.dp),
            modifier = Modifier.fillMaxWidth()
        ) {
            Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text("Troubleshooting & Cache", fontWeight = FontWeight.SemiBold, color = Color(0xFFE8EAED))
                InfoLine("Cached Messages", "${state.recentCount} (in-memory only)")
                InfoLine("Last Event", state.lastEvent)
                Spacer(Modifier.height(2.dp))
                Text(
                    text = "Tip: Keep both devices on the same 2.4 GHz Wi-Fi network. On the CYD, open WhatsApp -> gear icon -> enter Phone IP, Port 8765, and the 6-digit Pairing Token.",
                    fontSize = 12.sp,
                    color = Color(0xFF9AA0A6)
                )
                OutlinedButton(
                    onClick = onClearHistory,
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text("Clear Cached History")
                }
            }
        }
    }
}

@Composable
private fun InfoLine(label: String, value: String, monospace: Boolean = false) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, fontSize = 13.sp, color = Color(0xFF9AA0A6))
        Text(
            text = value,
            fontSize = 13.sp,
            fontWeight = FontWeight.Medium,
            fontFamily = if (monospace) FontFamily.Monospace else FontFamily.Default,
            color = Color(0xFFE8EAED)
        )
    }
}
