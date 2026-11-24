package com.anari.orbit.link

import org.json.JSONArray
import org.json.JSONObject
import java.time.Instant
import java.time.LocalDate
import java.time.YearMonth
import java.time.ZoneId
import java.time.ZoneOffset
import java.util.TreeMap
import java.util.TreeSet
import kotlin.math.roundToInt

/** One calendar entry as the watch needs it. Times are seconds since 1970, UTC. */
data class AgendaEvent(val start: Long, val end: Long, val title: String, val place: String, val allDay: Boolean)

/** Chooses which calendar entries the watch should hear about. */
object Agenda {
    /**
     * A calendar entry as Android hands it over, times in milliseconds. A
     * timed entry is a real moment. An all-day entry is not: it runs from
     * midnight UTC on its first date to midnight UTC after its last, whatever
     * the phone's own time zone.
     */
    data class Row(val beginMs: Long, val endMs: Long, val title: String, val place: String, val allDay: Boolean)

    /**
     * What is on from now through the next [days] days, soonest first, at
     * most [limit]. All-day entries are judged by the calendar date where the
     * phone is, so today's stays listed until local midnight rather than
     * vanishing early or late.
     */
    fun upcoming(rows: List<Row>, nowMs: Long, zone: ZoneId, limit: Int = 8, days: Int = 2): List<AgendaEvent> {
        val today = Instant.ofEpochMilli(nowMs).atZone(zone).toLocalDate()
        val horizon = nowMs + days * 24 * 60 * 60 * 1000L
        val lastDate = today.plusDays(days - 1L)

        fun firstDate(row: Row) = utcDate(row.beginMs)
        fun dateAfter(row: Row) = maxOf(utcDate(row.endMs), firstDate(row).plusDays(1))
        fun sortKey(row: Row) =
            if (row.allDay) maxOf(firstDate(row), today).atStartOfDay(zone).toInstant().toEpochMilli() else row.beginMs

        return rows
            .filter { row ->
                if (row.allDay) firstDate(row) <= lastDate && dateAfter(row) > today
                else row.endMs > nowMs && row.beginMs < horizon
            }
            .sortedWith(compareBy<Row>(::sortKey).thenBy { !it.allDay })
            .take(limit)
            .map { AgendaEvent(it.beginMs / 1000, it.endMs / 1000, it.title, it.place, it.allDay) }
    }

    /**
     * The days of [month], by the phone's own calendar, that have something
     * on. An entry that runs over several days marks each of them; one that
     * ends exactly at midnight does not mark the day that follows.
     */
    fun busyDays(rows: List<Row>, month: YearMonth, zone: ZoneId): List<Int> {
        val days = TreeSet<Int>()
        for (row in rows) {
            val (first, last) = dates(row, zone)
            var day = maxOf(first, month.atDay(1))
            val stop = minOf(last, month.atEndOfMonth())
            while (!day.isAfter(stop)) {
                days += day.dayOfMonth
                day = day.plusDays(1)
            }
        }
        return days.toList()
    }

    /** Everything that touches one date, by the same test as [busyDays]: all-day entries first, then by start. */
    fun onDate(rows: List<Row>, date: LocalDate, zone: ZoneId, limit: Int = 12): List<AgendaEvent> = rows
        .filter { row -> dates(row, zone).let { (first, last) -> !date.isBefore(first) && !date.isAfter(last) } }
        .sortedWith(compareBy<Row> { !it.allDay }.thenBy { it.beginMs })
        .take(limit)
        .map { AgendaEvent(it.beginMs / 1000, it.endMs / 1000, it.title, it.place, it.allDay) }

    /** The first and last phone-local dates an entry touches. */
    private fun dates(row: Row, zone: ZoneId): Pair<LocalDate, LocalDate> =
        if (row.allDay) {
            val first = utcDate(row.beginMs)
            first to maxOf(utcDate(row.endMs), first.plusDays(1)).minusDays(1)
        } else {
            Instant.ofEpochMilli(row.beginMs).atZone(zone).toLocalDate() to
                Instant.ofEpochMilli(maxOf(row.endMs - 1, row.beginMs)).atZone(zone).toLocalDate()
        }

    private fun utcDate(ms: Long): LocalDate = Instant.ofEpochMilli(ms).atZone(ZoneOffset.UTC).toLocalDate()
}

/** Today's weather. Temperatures in Celsius; [code] is the World Meteorological Organization's weather code. */
data class WeatherReport(val temp: Double, val high: Double, val low: Double, val code: Int) {
    val text get() = Weather.describe(code)
    val summary get() = "${temp.roundToInt()}°, ${text.lowercase()}"
}

object Weather {
    /** Reads Open-Meteo's forecast reply. Null when it is not what was asked for. */
    fun parse(body: String): WeatherReport? = try {
        val json = JSONObject(body)
        val now = json.getJSONObject("current")
        val day = json.getJSONObject("daily")
        WeatherReport(
            temp = now.getDouble("temperature_2m"),
            high = day.getJSONArray("temperature_2m_max").getDouble(0),
            low = day.getJSONArray("temperature_2m_min").getDouble(0),
            code = now.optInt("weather_code", -1),
        )
    } catch (e: Exception) {
        null
    }

    /** Short enough for the watch's 23-character weather line. */
    fun describe(code: Int): String = when (code) {
        0 -> "Clear"
        1 -> "Mostly clear"
        2 -> "Partly cloudy"
        3 -> "Overcast"
        45, 48 -> "Fog"
        51, 53, 55 -> "Drizzle"
        56, 57 -> "Freezing drizzle"
        61 -> "Light rain"
        63 -> "Rain"
        65 -> "Heavy rain"
        66, 67 -> "Freezing rain"
        71 -> "Light snow"
        73 -> "Snow"
        75 -> "Heavy snow"
        77 -> "Snow grains"
        80, 81 -> "Showers"
        82 -> "Heavy showers"
        85, 86 -> "Snow showers"
        95 -> "Thunderstorm"
        96, 99 -> "Thunder and hail"
        else -> "Weather"
    }
}

/** Messages the phone sends beyond the ones in Protocol.kt. See companion/PROTOCOL.md. */
object Messages {
    /** The watch already reads this one (it is Gadgetbridge's), with temperatures in kelvin. */
    fun weather(report: WeatherReport): JSONObject = JSONObject().put("t", "weather")
        .put("temp", kelvin(report.temp)).put("hi", kelvin(report.high)).put("lo", kelvin(report.low))
        .put("txt", report.text).put("code", report.code)

    private fun kelvin(celsius: Double) = ((celsius + 273.15) * 100).roundToInt() / 100.0

    /** Roughly where the phone is, to the nearest kilometre or so, for sunrise and sunset. */
    fun place(latitude: Double, longitude: Double): JSONObject = JSONObject().put("t", "place")
        .put("lat", coarse(latitude)).put("lon", coarse(longitude))

    fun coarse(degrees: Double) = (degrees * 100).roundToInt() / 100.0

    /**
     * The next few calendar entries, one line each so no line grows too long.
     * Line 0 tells the watch to forget the old list. An empty calendar is one
     * line with "n":0.
     */
    fun agenda(events: List<AgendaEvent>): List<JSONObject> {
        if (events.isEmpty()) return listOf(JSONObject().put("t", "agenda").put("i", 0).put("n", 0))
        return events.mapIndexed { i, event -> entry("agenda", i, events.size, event) }
    }

    /** One date's entries, in answer to the watch asking for that date. Same lines as [agenda], each naming the date. */
    fun day(date: LocalDate, events: List<AgendaEvent>): List<JSONObject> {
        if (events.isEmpty()) return listOf(JSONObject().put("t", "day").put("date", date.toString()).put("n", 0))
        return events.mapIndexed { i, event -> entry("day", i, events.size, event).put("date", date.toString()) }
    }

    private fun entry(kind: String, index: Int, count: Int, event: AgendaEvent): JSONObject =
        JSONObject().put("t", kind).put("i", index).put("n", count)
            .put("s", event.start).put("e", event.end)
            .put("title", Link.clip(event.title.ifBlank { "Busy" }, 40))
            .put("loc", Link.clip(event.place, 30))
            .put("allday", event.allDay)

    /** The quick replies offered under a notification. At most five, each short enough for a chip. */
    fun replies(list: List<String>): JSONObject = JSONObject().put("t", "replies")
        .put("list", JSONArray(list.map { Link.clip(it.trim(), 24) }.filter { it.isNotEmpty() }.take(5)))

    fun phone(battery: Int, charging: Boolean): JSONObject =
        JSONObject().put("t", "phone").put("bat", battery).put("chg", charging)

    /** "Find my watch": the watch already answers this one by ringing. */
    fun find(on: Boolean): JSONObject = JSONObject().put("t", "find").put("n", on)

    /** Changes one of the watch's settings from the phone. */
    fun configure(key: String, value: Any): JSONObject {
        val v = if (value is Collection<*>) JSONArray(value) else value
        return JSONObject().put("t", "cfg").put("set", JSONObject().put(key, v))
    }

    fun photo(key: String, bytes: ByteArray): JSONObject = JSONObject().put("t", "blob").put("kind", "photo")
        .put("key", key).put("len", bytes.size).put("crc", Link.crc32Hex(bytes))

    fun photoDelete(name: String): JSONObject = JSONObject().put("t", "photos").put("n", "delete").put("name", name)

    /** The days of a month that have something on, for the dots in the watch's calendar. */
    fun busy(month: YearMonth, days: List<Int>): JSONObject =
        JSONObject().put("t", "busy").put("month", month.toString()).put("days", JSONArray(days))
}

/** The watch's own settings, as it reports them. Unknown until the watch sends its first "cfg". */
data class WatchConfig(
    val brightness: Int = 70,
    val doNotDisturb: Boolean = false,
    val clock24: Boolean = false,
    val raiseToWake: Boolean = true,
    val sound: Boolean = true,
    /** 0 off, 1 clock, 2 photos. */
    val alwaysOn: Int = 0,
    val timeoutSeconds: Int = 15,
    val stepGoal: Int = 8000,
    /** Index into [FACES]. */
    val face: Int = 0,
    /** Index into [ACCENTS]. */
    val accent: Int = 0,
    val compLeft: Int = 0,
    val compRight: Int = 1,
    val faces: List<Int> = listOf(0, 1, 2, 3),
    val styles: List<Int> = listOf(0, 0, 0, 0),
    val gridLauncher: Boolean = false,
) {
    /** The watch may send every setting or only the ones that changed. */
    fun merge(json: JSONObject) = copy(
        face = json.optInt("face", face),
        accent = json.optInt("accent", accent),
        brightness = json.optInt("brightness", brightness),
        doNotDisturb = json.optBoolean("dnd", doNotDisturb),
        clock24 = json.optBoolean("h24", clock24),
        raiseToWake = json.optBoolean("raise", raiseToWake),
        sound = json.optBoolean("sound", sound),
        alwaysOn = json.optInt("aod", alwaysOn),
        timeoutSeconds = json.optInt("timeout", timeoutSeconds),
        stepGoal = json.optInt("goal", stepGoal),
        compLeft = json.optInt("compL", compLeft),
        compRight = json.optInt("compR", compRight),
        gridLauncher = json.optBoolean("grid", gridLauncher),
        faces = json.optJSONArray("faces")?.let { arr ->
            (0 until arr.length()).map { arr.optInt(it) }.distinct().filter { it in FACES.indices }.ifEmpty { faces }
        } ?: faces,
        styles = json.optJSONArray("styles")?.let { arr ->
            (0 until FACES.size).map { i -> if (i < arr.length()) arr.optInt(i, 0).coerceIn(0, 2) else styles.getOrElse(i) { 0 } }
        } ?: styles,
    )

    companion object {
        val FACES = listOf("Digital", "Analog", "Modular", "Photo")
        val COMPLICATIONS = listOf("Battery", "Steps", "Weather", "Day", "Pet", "Agenda", "None")
        val FACE_STYLES = listOf(
            listOf("Stacked", "Compact", "Minimal"),
            listOf("Classic", "Minimal", "Sport"),
            listOf("Agenda", "Weather", "Pet"),
            listOf("Bottom", "Top", "Corner"),
        )

        /** The watch's accent colours, as names and 0xRRGGBB. */
        val ACCENTS = listOf(
            "Sky" to 0xA8C7FA, "Mint" to 0xA8DAB5, "Rose" to 0xFFB1C8,
            "Violet" to 0xD0BCFF, "Peach" to 0xFFB68C, "Sun" to 0xFDD663,
        )
    }
}

/** Steps per day, as counted by the watch and remembered by the phone. */
class StepLog(saved: String? = null) {
    private val days = TreeMap<String, Int>()

    init {
        try {
            val json = JSONObject(saved ?: "{}")
            for (day in json.keys()) days[day] = json.getInt(day)
        } catch (e: Exception) {
            days.clear() // a damaged log starts afresh
        }
    }

    /** The watch counts up through the day, so the highest number seen for a day is that day's total. */
    fun record(day: String, steps: Int) {
        if (steps < 0 || runCatching { LocalDate.parse(day) }.isFailure) return
        days[day] = maxOf(days[day] ?: 0, steps)
        while (days.size > KEEP_DAYS) days.pollFirstEntry()
    }

    /** The last [count] days ending today, oldest first, with zero for days the watch said nothing about. */
    fun recent(today: LocalDate, count: Int): List<Pair<LocalDate, Int>> =
        (count - 1 downTo 0).map { today.minusDays(it.toLong()) }.map { it to (days[it.toString()] ?: 0) }

    fun save(): String = JSONObject(days as Map<*, *>).toString()

    private companion object {
        const val KEEP_DAYS = 120
    }
}
