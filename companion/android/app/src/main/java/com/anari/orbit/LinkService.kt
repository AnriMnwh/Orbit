package com.anari.orbit

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.LocationListener
import android.location.LocationManager
import android.media.AudioAttributes
import android.media.Ringtone
import android.media.RingtoneManager
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.util.Log
import com.anari.orbit.link.Link
import com.anari.orbit.link.Messages
import com.anari.orbit.link.StepLog
import com.anari.orbit.link.WatchConfig
import com.anari.orbit.link.WatchLink
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.update
import org.json.JSONObject
import java.time.LocalDate
import java.time.YearMonth
import java.util.TimeZone

/**
 * Keeps the watch connected while the app is out of sight, and passes
 * messages between the watch and the parts of the app that answer them.
 *
 * The work costs the phone very little: the service sleeps until the watch
 * or one of Android's own callbacks has something to say.
 */
class LinkService : Service(), WatchLink.Events {
    private lateinit var settings: Settings
    private lateinit var thread: HandlerThread
    private lateinit var handler: Handler
    private lateinit var link: WatchLink
    private lateinit var media: MediaRelay
    private lateinit var agent: AgentRelay
    private lateinit var daily: DailyRelay
    private lateinit var photos: PhotoRelay
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var ringtone: Ringtone? = null

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        settings = Settings(this)
        thread = HandlerThread("orbit-link").apply { start() }
        handler = Handler(thread.looper)
        link = WatchLink(this, handler, this)
        media = MediaRelay(this, handler, link, scope)
        agent = AgentRelay(settings, scope, handler, link)
        daily = DailyRelay(this, settings, scope, handler, link)
        photos = PhotoRelay(PhotoStore.get(this), handler, link)

        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL, getString(R.string.channel_link), NotificationManager.IMPORTANCE_LOW),
        )
        val filter = IntentFilter().apply {
            addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            addAction(Intent.ACTION_TIME_CHANGED)
            addAction(Intent.ACTION_TIMEZONE_CHANGED)
        }
        registerReceiver(systemEvents, filter, null, handler, Context.RECEIVER_NOT_EXPORTED)
        Hub.service = this
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        val address = settings.watchAddress
        val allowed = checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
        try {
            startForeground(1, notification(R.string.link_waiting), ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        } catch (e: Exception) {
            // Android refuses when Bluetooth permission is missing; the setup screen asks for it.
            Log.w(TAG, "cannot run in the background yet: $e")
            stopSelf()
            return START_NOT_STICKY
        }
        if (address == null || !allowed) {
            stopSelf()
            return START_NOT_STICKY
        }
        val quick = intent?.getBooleanExtra(EXTRA_QUICK, false) == true
        Hub.state.update { it.copy(address = address) }
        // Opening the app restarts the service; a link that is already up to this watch is left alone.
        handler.post { if (!link.isUpTo(address)) link.connect(address, quick) }
        return START_STICKY
    }

    override fun onDestroy() {
        Hub.service = null
        unregisterReceiver(systemEvents)
        scope.cancel()
        handler.post {
            ring(false)
            locate(false)
            media.stop()
            daily.stop()
            photos.stop()
            link.close()
            thread.quitSafely()
        }
        Hub.state.update { WatchState(address = settings.watchAddress) }
        super.onDestroy()
    }

    // ------------------------------------------------- called from other parts

    /** Sends one message to the watch. Safe from any thread; dropped while the watch is away. */
    fun send(json: JSONObject) {
        handler.post { link.send(json) }
    }

    /** Notification access was just granted, so media sessions can be read now. */
    fun onNotificationAccess() {
        handler.post { if (link.up) media.start() }
    }

    /** Makes the watch ring and say where it is. */
    fun findWatch() = send(Messages.find(true))

    /**
     * Says hello again, which makes the watch ask to pair again. For when the
     * pairing request was declined or missed: the watch never answers a hello
     * on an unpaired link, and the app stays in basic mode until it does.
     */
    fun helloAgain() = send(Link.hello(BuildConfig.VERSION_NAME))

    /** Changes one of the watch's own settings, and shows the change at once on the phone. */
    fun configure(key: String, value: Any) {
        send(Messages.configure(key, value))
        val jsonVal = if (value is Collection<*>) org.json.JSONArray(value) else value
        Hub.state.update { it.copy(config = (it.config ?: WatchConfig()).merge(JSONObject().put(key, jsonVal))) }
    }

    /** A photo was added or removed on the phone; bring the watch up to date. */
    fun photosChanged() {
        handler.post { photos.pump(fresh = true) }
    }

    /** Weather, calendar or quick replies were changed on the phone; send them again. */
    fun settingsChanged() {
        handler.post { daily.refresh() }
    }

    // ------------------------------------------------------------ link events

    override fun onUp() {
        sendTime()
        link.send(Link.hello(BuildConfig.VERSION_NAME))
        Hub.state.update { it.copy(connected = true) }
        showStatus(R.string.link_connected)
        media.start()
        daily.start()
    }

    override fun onDown() {
        media.stop()
        agent.reset()
        daily.stop()
        photos.stop()
        ring(false)
        locate(false)
        Hub.state.update { it.copy(connected = false, orbitLink = false, assistant = "", caps = emptySet(), ota = null) }
        showStatus(R.string.link_waiting)
    }

    override fun onLine(json: JSONObject) {
        when (json.optString("t")) {
            "orbit" -> {
                // The watch knows this app: the extras both sides list can be used. It answers
                // only once the link is encrypted, so pictures and speech are safe to send from here on.
                link.bulkReady = link.hasBulk
                val caps = json.optJSONArray("caps")
                val both = (0 until (caps?.length() ?: 0)).map { caps!!.optString(it) }.filter { it in Link.CAPS }.toSet()
                val art = json.optJSONObject("art")
                media.artSpec = if ("art" in both && art != null && link.hasBulk) {
                    MediaRelay.ArtSpec(art.optInt("w", 240), art.optInt("h", 240), art.optInt("max", 20000))
                } else {
                    null
                }
                agent.enabled = "agent" in both && link.hasBulk
                agent.linkUp()
                Hub.state.update { it.copy(orbitLink = true, caps = both, firmware = json.optString("fw", it.firmware)) }
                media.refresh()
                daily.caps = both
                // A watch with a full calendar app says how much it wants; otherwise eight within two days.
                json.optJSONObject("agenda")?.let {
                    daily.agendaMax = it.optInt("max", 8).coerceIn(1, 200)
                    daily.agendaDays = it.optInt("days", 2).coerceIn(1, 90)
                }
                daily.refresh()
                photos.enabled = "photos" in both
                photos.pump()
            }
            // The watch's step count for a day; the phone keeps the history.
            "act" -> {
                val log = StepLog(settings.stepLog)
                log.record(json.optString("day").ifEmpty { LocalDate.now().toString() }, json.optInt("steps", -1))
                settings.stepLog = log.save()
                Hub.state.update { it.copy(steps = log.recent(LocalDate.now(), 7)) }
            }
            "cfg" -> {
                Hub.state.update { it.copy(config = (it.config ?: WatchConfig()).merge(json)) }
                Hub.state.value.config?.let { if (settings.accent != it.accent) settings.accent = it.accent }
            }
            "ota" -> {
                val windowMs = json.optInt("window", 300).coerceIn(30, 1800) * 1000L
                val ready = OtaReady(
                    ip = json.optString("ip"),
                    pin = json.optString("pin"),
                    expiresAtMs = System.currentTimeMillis() + windowMs,
                )
                Hub.state.update { it.copy(ota = ready) }
            }
            // The watch's calendar was paged to a month it holds no dots for.
            "busy" -> runCatching { YearMonth.parse(json.optString("month")) }.getOrNull()?.let(daily::sendBusy)
            // A day in the watch's month view was tapped.
            "day" -> runCatching { LocalDate.parse(json.optString("date")) }.getOrNull()?.let(daily::sendDay)
            "ver" -> Hub.state.update { it.copy(firmware = json.optString("fw")) }
            "status" -> Hub.state.update {
                it.copy(battery = json.optInt("bat", -1), charging = json.optInt("chg", 0) != 0)
            }
            "music" -> media.command(json)
            "notify" -> Hub.notifications?.fromWatch(json)
            "call" -> Hub.notifications?.answerCall(json.optString("n") == "ACCEPT")
            "findPhone" -> ring(json.optBoolean("n"))
            "gps_power" -> locate(json.optBoolean("status"))
            "blob" -> if (json.optString("kind") == "photo") photos.onBlobReply(json) else media.onBlobReply(json)
            "agent" -> agent.onLine(json)
        }
    }

    override fun onBulk(frame: ByteArray) = agent.onBulk(frame)

    // ---------------------------------------------------------- small duties

    private fun sendTime() {
        val now = System.currentTimeMillis()
        link.sendRaw(Link.timeLine(now / 1000, TimeZone.getDefault().getOffset(now) / 60_000))
    }

    private val systemEvents = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (intent.action) {
                BluetoothAdapter.ACTION_STATE_CHANGED ->
                    if (intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, 0) == BluetoothAdapter.STATE_ON) {
                        settings.watchAddress?.let { link.connect(it, false) }
                    }
                else -> if (link.up) sendTime()
            }
        }
    }

    /** "Find my phone" from the watch: ring on the alarm channel so silent mode does not hide it. */
    private fun ring(on: Boolean) {
        handler.removeCallbacksAndMessages(RING)
        ringtone?.stop()
        ringtone = null
        if (!on) return
        val uri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_RINGTONE)
            ?: RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM) ?: return
        ringtone = RingtoneManager.getRingtone(this, uri)?.apply {
            audioAttributes = AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_ALARM)
                .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                .build()
            isLooping = true
            play()
        }
        handler.postDelayed({ ring(false) }, RING, 60_000)
    }

    private val locationListener = LocationListener { fix ->
        link.send(
            JSONObject().put("t", "gps")
                .put("lat", fix.latitude).put("lon", fix.longitude).put("alt", fix.altitude)
                .put("speed", fix.speed * 3.6).put("course", fix.bearing.toDouble())
                .put("time", fix.time).put("satellites", 0).put("fix", 1).put("hdop", fix.accuracy.toDouble()),
        )
    }

    /** The watch's Maps app asks for the phone's position only while it is open. */
    private fun locate(on: Boolean) {
        val manager = getSystemService(LocationManager::class.java) ?: return
        manager.removeUpdates(locationListener)
        if (!on || checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED) return
        val provider = listOf(LocationManager.FUSED_PROVIDER, LocationManager.GPS_PROVIDER)
            .firstOrNull { it in manager.allProviders } ?: return
        try {
            manager.requestLocationUpdates(provider, 2000L, 0f, locationListener, handler.looper)
        } catch (e: SecurityException) {
            Log.w(TAG, "location refused: $e")
        }
    }

    private fun showStatus(text: Int) {
        getSystemService(NotificationManager::class.java).notify(1, notification(text))
    }

    private fun notification(text: Int): Notification = Notification.Builder(this, CHANNEL)
        .setSmallIcon(R.drawable.ic_stat_orbit)
        .setContentTitle(getString(R.string.app_name))
        .setContentText(getString(text))
        .setOngoing(true)
        .setContentIntent(
            PendingIntent.getActivity(this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE),
        )
        .build()

    companion object {
        private const val TAG = "OrbitService"
        private const val CHANNEL = "link"
        private const val EXTRA_QUICK = "quick"
        private val RING = Any()

        /** Starts (or restarts) the connection. [quick] is for when the user is watching the screen. */
        fun start(context: Context, quick: Boolean = false) {
            if (Settings(context).watchAddress == null) return
            try {
                context.startForegroundService(Intent(context, LinkService::class.java).putExtra(EXTRA_QUICK, quick))
            } catch (e: Exception) {
                Log.w(TAG, "could not start: $e")
            }
        }

        fun stop(context: Context) {
            context.stopService(Intent(context, LinkService::class.java))
        }
    }
}
