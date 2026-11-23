package com.anari.orbit

import android.Manifest
import android.content.BroadcastReceiver
import android.content.ContentUris
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.database.ContentObserver
import android.location.Location
import android.location.LocationManager
import android.os.BatteryManager
import android.os.Handler
import android.provider.CalendarContract
import android.util.Log
import com.anari.orbit.link.Agenda
import com.anari.orbit.link.AgendaEvent
import com.anari.orbit.link.Messages
import com.anari.orbit.link.Weather
import com.anari.orbit.link.WatchLink
import com.anari.orbit.link.WeatherReport
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.net.HttpURLConnection
import java.net.URL
import java.time.Instant
import java.time.LocalDate
import java.time.YearMonth
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import kotlin.math.abs

/**
 * The things the phone looks up so the watch does not have to: weather, the
 * next calendar entries, roughly where it is, and the phone's own battery.
 *
 * Sent when the watch connects and about once an hour after that, so the
 * phone's radios are barely used. Runs on the link thread; the one internet
 * call runs on [scope].
 */
class DailyRelay(
    private val context: Context,
    private val settings: Settings,
    private val scope: CoroutineScope,
    private val handler: Handler,
    private val link: WatchLink,
) {
    /** The extras the watch's software knows. Weather needs none: the watch already reads it. */
    var caps: Set<String> = emptySet()

    /** How many calendar entries the watch wants and how far ahead, from its hello. */
    var agendaMax = 8
    var agendaDays = 2
    private var sentAgenda = ""

    private var watching = false
    private var watchingCalendar = false
    private var sentBattery = -100
    private var sentCharging = false

    private val hourly = Runnable { refresh() }
    private val calendarSettled = Runnable { sendAgenda() }

    private val calendarChanged = object : ContentObserver(handler) {
        override fun onChange(selfChange: Boolean) {
            // Calendar apps save in bursts; wait for the burst to end.
            handler.removeCallbacks(calendarSettled)
            handler.postDelayed(calendarSettled, 5000)
        }
    }

    private val batteryChanged = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) = sendBattery(intent, force = false)
    }

    fun start() {
        if (!watching) {
            watching = true
            context.registerReceiver(batteryChanged, IntentFilter(Intent.ACTION_BATTERY_CHANGED), null, handler, Context.RECEIVER_NOT_EXPORTED)
        }
        // A moment's grace so the watch's hello, which says what it understands, arrives first.
        handler.removeCallbacks(hourly)
        handler.postDelayed(hourly, 3000)
    }

    fun stop() {
        handler.removeCallbacks(hourly)
        handler.removeCallbacks(calendarSettled)
        if (watching) {
            watching = false
            context.unregisterReceiver(batteryChanged)
        }
        if (watchingCalendar) {
            watchingCalendar = false
            context.contentResolver.unregisterContentObserver(calendarChanged)
        }
        caps = emptySet()
        agendaMax = 8
        agendaDays = 2
        sentAgenda = ""
        sentBattery = -100
    }

    /** Sends everything afresh. Called on connect, hourly, and when a setting on the phone changes. */
    fun refresh() {
        handler.removeCallbacks(hourly)
        if (!link.up) return
        handler.postDelayed(hourly, 60 * 60 * 1000L)
        // Calendar access may have been granted since the watch connected.
        if (!watchingCalendar && has(Manifest.permission.READ_CALENDAR)) {
            watchingCalendar = true
            context.contentResolver.registerContentObserver(CalendarContract.Events.CONTENT_URI, true, calendarChanged)
        }
        sendReplies()
        sendAgenda()
        sendBattery(context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED)), force = true)
        sendWeatherAndPlace()
    }

    // --------------------------------------------------------------- weather

    private fun sendWeatherAndPlace() {
        if (!settings.weatherOn) {
            Hub.state.update { it.copy(weather = "") }
            return
        }
        val here = lastPosition() ?: return Hub.state.update { it.copy(weather = "Waiting for the phone's location") }
        val latitude = Messages.coarse(here.latitude)
        val longitude = Messages.coarse(here.longitude)
        if ("place" in caps) link.send(Messages.place(latitude, longitude))
        scope.launch {
            val report = fetchWeather(latitude, longitude)
            handler.post {
                if (report != null && link.up) link.send(Messages.weather(report))
                Hub.state.update { it.copy(weather = report?.summary ?: "Weather is not available right now") }
            }
        }
    }

    /** The most recent position any app has already found; this never switches GPS on. */
    private fun lastPosition(): Location? {
        if (!has(Manifest.permission.ACCESS_COARSE_LOCATION)) return null
        val manager = context.getSystemService(LocationManager::class.java) ?: return null
        return manager.allProviders.mapNotNull {
            try {
                manager.getLastKnownLocation(it)
            } catch (e: SecurityException) {
                null
            }
        }.maxByOrNull { it.time }
    }

    /** One small request to Open-Meteo, which needs no account. The position is already blurred. */
    private fun fetchWeather(latitude: Double, longitude: Double): WeatherReport? {
        val url = "https://api.open-meteo.com/v1/forecast?latitude=$latitude&longitude=$longitude" +
            "&current=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1"
        val connection = URL(url).openConnection() as HttpURLConnection
        return try {
            connection.connectTimeout = 15_000
            connection.readTimeout = 15_000
            if (connection.responseCode != 200) null
            else Weather.parse(connection.inputStream.use { it.readBytes() }.toString(Charsets.UTF_8))
        } catch (e: Exception) {
            Log.i(TAG, "weather: $e")
            null
        } finally {
            connection.disconnect()
        }
    }

    // -------------------------------------------------------------- calendar

    private fun sendAgenda() {
        if (!link.up) return
        val allowed = settings.calendarOn && has(Manifest.permission.READ_CALENDAR)
        val now = System.currentTimeMillis()
        val events = if (allowed) {
            Agenda.upcoming(calendar(now - DAY, now + (agendaDays + 1) * DAY), now, ZoneId.systemDefault(), agendaMax, agendaDays)
        } else {
            emptyList()
        }
        if ("agenda" in caps) {
            // A long list is a lot of radio; say it again only when it has changed.
            val lines = Messages.agenda(events)
            val text = lines.joinToString("\n")
            if (text != sentAgenda) {
                sentAgenda = text
                lines.forEach(link::send)
            }
        }
        Hub.state.update { it.copy(nextEvent = events.firstOrNull()?.let(::describe) ?: "") }
        sendBusy(YearMonth.now())
    }

    /** One date's entries, when the watch's month view is tapped on a day. Nothing is sent while the calendar is off. */
    fun sendDay(date: LocalDate) {
        if ("day" !in caps || !link.up || !settings.calendarOn || !has(Manifest.permission.READ_CALENDAR)) return
        val zone = ZoneId.systemDefault()
        val from = date.atStartOfDay(zone).toInstant().toEpochMilli()
        Messages.day(date, Agenda.onDate(calendar(from - DAY, from + 2 * DAY), date, zone)).forEach(link::send)
    }

    /**
     * Which days of a month have something on, for the dots in the watch's
     * month view. Sent for this month alongside the agenda, and for any month
     * the watch asks about. Nothing is sent while the calendar is off.
     */
    fun sendBusy(month: YearMonth) {
        if ("busy" !in caps || !link.up || !settings.calendarOn || !has(Manifest.permission.READ_CALENDAR)) return
        val zone = ZoneId.systemDefault()
        val from = month.atDay(1).atStartOfDay(zone).toInstant().toEpochMilli()
        val to = month.plusMonths(1).atDay(1).atStartOfDay(zone).toInstant().toEpochMilli()
        link.send(Messages.busy(month, Agenda.busyDays(calendar(from - DAY, to + DAY), month, zone)))
    }

    /**
     * Calendar entries between two moments, leaving out anything declined.
     * Callers ask for a day either side of what they want: all-day entries are
     * filed under UTC midnights, which can sit most of a day away from the
     * phone's own midnight.
     */
    private fun calendar(fromMs: Long, toMs: Long): List<Agenda.Row> {
        val window = CalendarContract.Instances.CONTENT_URI.buildUpon().also {
            ContentUris.appendId(it, fromMs)
            ContentUris.appendId(it, toMs)
        }.build()
        val columns = arrayOf(
            CalendarContract.Instances.BEGIN,
            CalendarContract.Instances.END,
            CalendarContract.Instances.TITLE,
            CalendarContract.Instances.EVENT_LOCATION,
            CalendarContract.Instances.ALL_DAY,
            CalendarContract.Instances.SELF_ATTENDEE_STATUS,
        )
        val found = ArrayList<Agenda.Row>()
        try {
            context.contentResolver.query(
                window, columns, "${CalendarContract.Instances.VISIBLE} = 1", null, "${CalendarContract.Instances.BEGIN} ASC",
            )?.use { rows ->
                while (rows.moveToNext() && found.size < 2000) {
                    if (rows.getInt(5) == CalendarContract.Attendees.ATTENDEE_STATUS_DECLINED) continue
                    found += Agenda.Row(
                        beginMs = rows.getLong(0),
                        endMs = rows.getLong(1),
                        title = rows.getString(2).orEmpty(),
                        place = rows.getString(3).orEmpty(),
                        allDay = rows.getInt(4) != 0,
                    )
                }
            }
        } catch (e: Exception) {
            Log.w(TAG, "calendar: $e")
        }
        return found
    }

    private fun describe(event: AgendaEvent): String {
        val title = event.title.ifBlank { "Busy" }
        if (event.allDay) return "$title, all day"
        val time = DateTimeFormatter.ofPattern("EEE HH:mm").format(Instant.ofEpochSecond(event.start).atZone(ZoneId.systemDefault()))
        return "$title, $time"
    }

    // ------------------------------------------------ battery, quick replies

    private fun sendBattery(intent: Intent?, force: Boolean) {
        if (intent == null || "phone" !in caps || !link.up) return
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, 100).coerceAtLeast(1)
        val percent = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, 0) * 100 / scale
        val charging = intent.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0
        // The system reports every percent; the watch only needs the broad picture.
        if (!force && charging == sentCharging && abs(percent - sentBattery) < 5) return
        sentBattery = percent
        sentCharging = charging
        link.send(Messages.phone(percent, charging))
    }

    fun sendReplies() {
        if ("replies" in caps && link.up) link.send(Messages.replies(settings.quickReplies))
    }

    private fun has(permission: String) = context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private companion object {
        const val TAG = "OrbitDaily"
        const val MAX_EVENTS = 8
        const val DAY = 24 * 60 * 60 * 1000L
    }
}
