package com.pixelcyd.companion.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import android.net.NetworkRequest
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Build
import android.os.IBinder
import androidx.core.app.NotificationCompat
import com.pixelcyd.companion.protocol.CompanionProtocol
import com.pixelcyd.companion.server.CompanionWebSocketServer
import com.pixelcyd.companion.store.CompanionRepository
import com.pixelcyd.companion.ui.MainActivity
import java.net.Inet4Address
import java.net.NetworkInterface

class CompanionService : Service() {

    companion object {
        private const val CHANNEL_ID = "pixel_cyd_companion_channel"
        private const val NOTIF_ID = 1001
        private const val NSD_TYPE = "_pixelcyd._tcp."
        private const val NSD_NAME = "PixelCYDCompanion"

        fun start(context: Context) {
            val intent = Intent(context, CompanionService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                context.startForegroundService(intent)
            } else {
                context.startService(intent)
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, CompanionService::class.java))
        }
    }

    private var wsServer: CompanionWebSocketServer? = null
    private var nsdManager: NsdManager? = null
    private var nsdRegListener: NsdManager.RegistrationListener? = null
    private var connectivityManager: ConnectivityManager? = null
    private var networkCallback: ConnectivityManager.NetworkCallback? = null

    override fun onCreate() {
        super.onCreate()
        CompanionRepository.init(applicationContext)
        createNotificationChannel()
        startForeground(NOTIF_ID, buildStatusNotification())
        startServer()
        registerNetworkMonitor()
        registerNsd()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        refreshLocalIp()
        updateForegroundNotification()
        return START_STICKY
    }

    override fun onDestroy() {
        unregisterNsd()
        unregisterNetworkMonitor()
        stopServer()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun startServer() {
        if (wsServer != null) return
        val port = CompanionProtocol.DEFAULT_PORT
        val server = CompanionWebSocketServer(applicationContext, port)
        wsServer = server

        CompanionRepository.onNewNotification = { msg ->
            server.broadcastNotification(msg)
        }
        CompanionRepository.onStatusChanged = {
            server.broadcastStatus()
            updateForegroundNotification()
        }
        CompanionRepository.onSendToCyd = { payload ->
            server.sendToAuthenticated(payload)
        }

        try {
            server.start()
            CompanionRepository.updateServerState(
                running = true,
                ip = findLocalIpv4(),
                port = port,
                event = "Companion server running"
            )
        } catch (e: Exception) {
            CompanionRepository.updateServerState(
                running = false,
                ip = findLocalIpv4(),
                port = port,
                event = "Server failed: ${e.message ?: "unknown"}"
            )
        }
    }

    private fun stopServer() {
        CompanionRepository.onNewNotification = null
        CompanionRepository.onStatusChanged = null
        CompanionRepository.onSendToCyd = null
        try {
            wsServer?.stop(1000)
        } catch (_: Exception) {
        }
        wsServer = null
        CompanionRepository.updateServerState(
            running = false,
            ip = findLocalIpv4(),
            port = CompanionProtocol.DEFAULT_PORT,
            event = "Companion server stopped"
        )
        CompanionRepository.updateClientCount(0)
    }

    private fun registerNetworkMonitor() {
        val cm = getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return
        connectivityManager = cm
        val cb = object : ConnectivityManager.NetworkCallback() {
            override fun onAvailable(network: Network) = refreshLocalIp()
            override fun onLost(network: Network) = refreshLocalIp()
        }
        networkCallback = cb
        val req = NetworkRequest.Builder()
            .addTransportType(NetworkCapabilities.TRANSPORT_WIFI)
            .build()
        try {
            cm.registerNetworkCallback(req, cb)
        } catch (_: Exception) {
        }
    }

    private fun unregisterNetworkMonitor() {
        val cb = networkCallback ?: return
        try {
            connectivityManager?.unregisterNetworkCallback(cb)
        } catch (_: Exception) {
        }
        networkCallback = null
    }

    private fun registerNsd() {
        val mgr = getSystemService(Context.NSD_SERVICE) as? NsdManager ?: return
        nsdManager = mgr
        val info = NsdServiceInfo().apply {
            serviceName = NSD_NAME
            serviceType = NSD_TYPE
            port = CompanionProtocol.DEFAULT_PORT
        }
        val listener = object : NsdManager.RegistrationListener {
            override fun onServiceRegistered(serviceInfo: NsdServiceInfo) {}
            override fun onRegistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {}
            override fun onServiceUnregistered(serviceInfo: NsdServiceInfo) {}
            override fun onUnregistrationFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {}
        }
        nsdRegListener = listener
        try {
            mgr.registerService(info, NsdManager.PROTOCOL_DNS_SD, listener)
        } catch (_: Exception) {
        }
    }

    private fun unregisterNsd() {
        val listener = nsdRegListener ?: return
        try {
            nsdManager?.unregisterService(listener)
        } catch (_: Exception) {
        }
        nsdRegListener = null
    }

    private fun refreshLocalIp() {
        val ui = CompanionRepository.uiState.value
        CompanionRepository.updateServerState(
            running = ui.serverRunning,
            ip = findLocalIpv4(),
            port = ui.port
        )
    }

    private fun findLocalIpv4(): String {
        return try {
            val ifaces = NetworkInterface.getNetworkInterfaces() ?: return ""
            for (iface in ifaces) {
                if (!iface.isUp || iface.isLoopback) continue
                val addrs = iface.inetAddresses
                for (addr in addrs) {
                    if (addr is Inet4Address && !addr.isLoopbackAddress && addr.isSiteLocalAddress) {
                        return addr.hostAddress ?: ""
                    }
                }
            }
            ""
        } catch (_: Exception) {
            ""
        }
    }

    private fun createNotificationChannel() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            val ch = NotificationChannel(
                CHANNEL_ID,
                "CYD Companion Bridge",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Shows when local WhatsApp forwarding to Pixel CYD is active"
            }
            val nm = getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(ch)
        }
    }

    private fun buildStatusNotification(): Notification {
        val ui = CompanionRepository.uiState.value
        val openIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val statusText = buildString {
            append(if (ui.forwardingEnabled) "WhatsApp forwarding active" else "Forwarding paused")
            if (ui.connectedDevices > 0) append(" • CYD connected")
            if (ui.localIp.isNotBlank()) append(" • ${ui.localIp}:${ui.port}")
        }
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setContentTitle("Pixel CYD Companion")
            .setContentText(statusText)
            .setSmallIcon(android.R.drawable.stat_notify_chat)
            .setOngoing(true)
            .setContentIntent(openIntent)
            .build()
    }

    private fun updateForegroundNotification() {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as? NotificationManager ?: return
        nm.notify(NOTIF_ID, buildStatusNotification())
    }
}
