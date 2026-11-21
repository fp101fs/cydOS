package com.pixelcyd.companion.notifications

import android.app.Notification
import android.app.Person
import android.app.RemoteInput
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Icon
import android.os.Build
import android.os.Bundle
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import android.util.Base64
import com.pixelcyd.companion.protocol.CompanionProtocol
import com.pixelcyd.companion.protocol.CompanionProtocol.ChatBubble
import com.pixelcyd.companion.protocol.CompanionProtocol.WhatsAppMessage
import com.pixelcyd.companion.store.CompanionRepository
import java.security.MessageDigest
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class WhatsAppNotificationListener : NotificationListenerService() {

    companion object {
        private val WHATSAPP_PACKAGES = setOf(
            "com.whatsapp",
            "com.whatsapp.w4b"
        )

        @Volatile
        private var instance: WhatsAppNotificationListener? = null

        fun isNotificationAccessGranted(context: Context): Boolean {
            val enabled = Settings.Secure.getString(
                context.contentResolver,
                "enabled_notification_listeners"
            ) ?: return false
            val target = ComponentName(context, WhatsAppNotificationListener::class.java).flattenToString()
            return enabled.split(':').any { it.equals(target, ignoreCase = true) }
        }

        fun sendReply(context: Context, notifId: String, replyText: String): Pair<String, String> {
            val entry = CompanionRepository.getReplyAction(notifId)
                ?: return "unsupported" to "Notification no longer active"

            val action = entry.action
                ?: return "unsupported" to "No reply action on notification"

            val remoteInputs = action.remoteInputs
            if (remoteInputs.isNullOrEmpty()) {
                return "unsupported" to "Notification has no RemoteInput"
            }

            val pendingIntent = action.actionIntent
                ?: return "unsupported" to "Reply PendingIntent missing"

            return try {
                val fillIn = Intent()
                val results = Bundle()
                for (ri in remoteInputs) {
                    results.putCharSequence(ri.resultKey, replyText)
                }
                RemoteInput.addResultsToIntent(remoteInputs, fillIn, results)
                pendingIntent.send(context, 0, fillIn)
                "sent" to ""
            } catch (e: Exception) {
                "failed" to (e.message?.take(48) ?: "Reply send failed")
            }
        }

        fun dismissNotification(notifId: String): Pair<Boolean, String> {
            val svc = instance ?: return false to "Notification listener not active"
            val entry = CompanionRepository.getReplyAction(notifId)
                ?: return false to "Notification already dismissed"
            return try {
                svc.cancelNotification(entry.sbnKey)
                CompanionRepository.removeFromHistory(notifId)
                true to ""
            } catch (e: Exception) {
                false to (e.message?.take(48) ?: "Dismiss failed")
            }
        }
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        instance = this
        CompanionRepository.updateListenerStatus(
            accessGranted = isNotificationAccessGranted(this),
            bound = true
        )
        // Seed chat list with any already-active WhatsApp notifications
        try {
            val existing = activeNotifications ?: return
            for (sbn in existing) {
                processNotification(sbn)
            }
        } catch (_: Exception) {
        }
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        instance = null
        CompanionRepository.updateListenerStatus(
            accessGranted = isNotificationAccessGranted(this),
            bound = false
        )
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        processNotification(sbn)
    }

    private fun processNotification(sbn: StatusBarNotification?) {
        if (sbn == null) return
        if (sbn.packageName !in WHATSAPP_PACKAGES) return
        if (!CompanionRepository.isForwardingEnabled()) return

        val notif = sbn.notification ?: return

        // Skip group summary headers ("3 messages from 2 chats") to avoid duplicates
        if ((notif.flags and Notification.FLAG_GROUP_SUMMARY) != 0) return
        // Skip ongoing call/backup notifications
        if ((notif.flags and Notification.FLAG_ONGOING_EVENT) != 0) return

        val extras = notif.extras ?: return
        val convTitle = extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE)?.toString()?.trim().orEmpty()
        val rawTitle = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim().orEmpty()
        val rawText = extractLatestMessageText(extras)

        if (rawTitle.isBlank() && rawText.isBlank()) return

        val sender = rawTitle.ifBlank { convTitle.ifBlank { "WhatsApp" } }
        val conversation = convTitle.ifBlank { sender }
        val replyAction = findReplyAction(notif)
        val stableId = stableIdForKey(sbn.key)
        val bubbles = extractMessagesList(extras, sender, sbn.postTime)
        val avatarB64 = extractAvatarRgb565B64(notif, extras)

        val msg = WhatsAppMessage(
            id = stableId,
            sbnKey = sbn.key,
            sender = sender.take(44),
            conversation = conversation.take(44),
            text = rawText.take(220),
            timestampMs = sbn.postTime,
            canReply = replyAction != null,
            messages = bubbles,
            avatarRgb565B64 = avatarB64
        )

        // Note: Intentionally do NOT log message body to Logcat for privacy.
        CompanionRepository.recordNotification(msg, replyAction)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        if (sbn == null) return
        if (sbn.packageName !in WHATSAPP_PACKAGES) return
        CompanionRepository.markNotificationRemoved(sbn.key)
    }

    private fun extractMessagesList(extras: Bundle, fallbackSender: String, fallbackTimeMs: Long): List<ChatBubble> {
        @Suppress("DEPRECATION")
        val parcelables = extras.getParcelableArray(Notification.EXTRA_MESSAGES) ?: return emptyList()
        val fmt = SimpleDateFormat("HH:mm", Locale.getDefault())
        val out = ArrayList<ChatBubble>()
        val recent = if (parcelables.size > 6) parcelables.takeLast(6) else parcelables.toList()
        for (p in recent) {
            val b = p as? Bundle ?: continue
            val text = b.getCharSequence("text")?.toString()?.trim().orEmpty()
            if (text.isBlank()) continue
            val msgSender = extractBundleSender(b).ifBlank { fallbackSender }
            val ts = b.getLong("time", fallbackTimeMs).let { if (it > 0L) it else fallbackTimeMs }
            val isOutgoing = msgSender.equals("You", ignoreCase = true)
            out.add(
                ChatBubble(
                    sender = msgSender.take(26),
                    text = text.take(156),
                    time = fmt.format(Date(ts)),
                    outgoing = isOutgoing
                )
            )
        }
        return out
    }

    private fun extractBundleSender(b: Bundle): String {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            @Suppress("DEPRECATION")
            val person = b.getParcelable<Person>("sender_person")
            val name = person?.name?.toString()?.trim()
            if (!name.isNullOrBlank()) return name
        }
        return b.getCharSequence("sender")?.toString()?.trim().orEmpty()
    }

    private fun extractAvatarRgb565B64(notif: Notification, extras: Bundle): String? {
        return try {
            var bmp: Bitmap? = null
            // 1) Try MessagingStyle Person icon first
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                @Suppress("DEPRECATION")
                val parcelables = extras.getParcelableArray(Notification.EXTRA_MESSAGES)
                val last = parcelables?.lastOrNull() as? Bundle
                @Suppress("DEPRECATION")
                val person = last?.getParcelable<Person>("sender_person")
                val pIcon = person?.icon
                if (pIcon != null) {
                    bmp = iconToBitmap(pIcon)
                }
            }
            // 2) Fallback to notification largeIcon (WhatsApp sets contact/group avatar here)
            if (bmp == null) {
                val largeIcon = notif.getLargeIcon()
                if (largeIcon != null) {
                    bmp = iconToBitmap(largeIcon)
                }
            }
            if (bmp == null) return null

            val sz = CompanionProtocol.AVATAR_SIZE
            val scaled = Bitmap.createScaledBitmap(bmp, sz, sz, true)
            val bytes = ByteArray(sz * sz * 2)
            var idx = 0
            for (y in 0 until sz) {
                for (x in 0 until sz) {
                    val argb = scaled.getPixel(x, y)
                    val r = (argb ushr 16) and 0xFF
                    val g = (argb ushr 8) and 0xFF
                    val b = argb and 0xFF
                    val rgb565 = ((r and 0xF8) shl 8) or ((g and 0xFC) shl 3) or (b ushr 3)
                    // LV_COLOR_16_SWAP == 1 expects big-endian RGB565
                    bytes[idx++] = ((rgb565 ushr 8) and 0xFF).toByte()
                    bytes[idx++] = (rgb565 and 0xFF).toByte()
                }
            }
            if (scaled != bmp) scaled.recycle()
            Base64.encodeToString(bytes, Base64.NO_WRAP)
        } catch (_: Exception) {
            null
        }
    }

    private fun iconToBitmap(icon: Icon): Bitmap? {
        val drawable = icon.loadDrawable(this) ?: return null
        if (drawable is BitmapDrawable && drawable.bitmap != null) {
            return drawable.bitmap
        }
        val w = if (drawable.intrinsicWidth > 0) drawable.intrinsicWidth else 64
        val h = if (drawable.intrinsicHeight > 0) drawable.intrinsicHeight else 64
        val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        drawable.setBounds(0, 0, canvas.width, canvas.height)
        drawable.draw(canvas)
        return bmp
    }

    private fun extractLatestMessageText(extras: Bundle): String {
        // Prefer MessagingStyle EXTRA_MESSAGES if present to get the latest message cleanly
        @Suppress("DEPRECATION")
        val parcelables = extras.getParcelableArray(Notification.EXTRA_MESSAGES)
        if (!parcelables.isNullOrEmpty()) {
            val last = parcelables.lastOrNull() as? Bundle
            val text = last?.getCharSequence("text")?.toString()?.trim()
            if (!text.isNullOrBlank()) return text
        }
        val bigText = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()?.trim()
        if (!bigText.isNullOrBlank()) return bigText
        return extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()?.trim().orEmpty()
    }

    private fun findReplyAction(notif: Notification): Notification.Action? {
        val actions = notif.actions ?: return null
        return actions.firstOrNull { action ->
            val inputs = action.remoteInputs
            !inputs.isNullOrEmpty() && action.actionIntent != null
        }
    }

    private fun stableIdForKey(key: String): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(key.toByteArray())
        return digest.take(10).joinToString("") { "%02x".format(it) }
    }
}
