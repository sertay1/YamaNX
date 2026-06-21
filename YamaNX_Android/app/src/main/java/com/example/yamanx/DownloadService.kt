package com.example.yamanx

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.IBinder
import androidx.core.app.NotificationCompat

class DownloadService : Service() {

    companion object {
        const val CHANNEL_ID = "yamanx_download_channel"
        const val NOTIFICATION_ID = 1001
        const val ACTION_UPDATE = "com.example.yamanx.UPDATE_PROGRESS"
        const val EXTRA_TITLE = "extra_title"
        const val EXTRA_PROGRESS = "extra_progress"
        const val EXTRA_STATUS = "extra_status"
        const val EXTRA_FINISHED = "extra_finished"

        fun createChannel(context: Context) {
            val channel = NotificationChannel(
                CHANNEL_ID,
                "YamaNX İndirme",
                NotificationManager.IMPORTANCE_LOW
            ).apply {
                description = "Yama indirme bildirimleri"
                setSound(null, null)
            }
            val nm = context.getSystemService(NotificationManager::class.java)
            nm.createNotificationChannel(channel)
        }
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        createChannel(this)
        startForeground(NOTIFICATION_ID, buildNotification("Hazırlanıyor...", 0, false))
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_UPDATE) {
            val title = intent.getStringExtra(EXTRA_TITLE) ?: "Yükleniyor..."
            val progress = intent.getIntExtra(EXTRA_PROGRESS, 0)
            val finished = intent.getBooleanExtra(EXTRA_FINISHED, false)
            val status = intent.getStringExtra(EXTRA_STATUS) ?: ""

            val nm = getSystemService(NotificationManager::class.java)
            nm.notify(NOTIFICATION_ID, buildNotification(title, progress, finished, status))

            if (finished) {
                stopSelf()
            }
        }
        return START_NOT_STICKY
    }

    private fun buildNotification(title: String, progress: Int, finished: Boolean, status: String = ""): Notification {
        val openIntent = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )

        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.logo)
            .setContentTitle(title)
            .setContentText(if (finished) status else "İndiriliyor... %$progress")
            .setContentIntent(openIntent)
            .setOngoing(!finished)
            .apply {
                if (!finished) {
                    setProgress(100, progress, progress == 0)
                } else {
                    setProgress(0, 0, false)
                }
            }
            .build()
    }
}
