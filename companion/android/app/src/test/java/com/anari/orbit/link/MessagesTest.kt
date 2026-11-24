package com.anari.orbit.link

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.time.LocalDate

class MessagesTest {

    private val openMeteo = """
        {"latitude":51.5,"longitude":-0.12,"timezone":"Europe/London",
         "current":{"time":"2026-11-10T09:00","interval":900,"temperature_2m":11.4,"weather_code":61},
         "daily":{"time":["2026-11-10"],"temperature_2m_max":[13.9],"temperature_2m_min":[7.2]}}
    """.trimIndent()

    @Test
    fun `a forecast reply becomes today's weather`() {
        val report = Weather.parse(openMeteo)!!
        assertEquals(11.4, report.temp, 0.001)
        assertEquals(13.9, report.high, 0.001)
        assertEquals(7.2, report.low, 0.001)
        assertEquals("Light rain", report.text)
        assertEquals("11°, light rain", report.summary)
    }

    @Test
    fun `anything else is not weather`() {
        assertNull(Weather.parse("{\"error\":true,\"reason\":\"bad\"}"))
        assertNull(Weather.parse("<html>busy</html>"))
    }

    @Test
    fun `every weather description fits the watch's line`() {
        assertTrue((-1..99).all { Weather.describe(it).length <= 23 })
        assertEquals("Thunderstorm", Weather.describe(95))
        assertEquals("Weather", Weather.describe(42))
    }

    @Test
    fun `weather is sent in kelvin, as the watch already expects`() {
        val message = Messages.weather(WeatherReport(temp = 20.0, high = 25.5, low = -3.0, code = 2))
        assertEquals("weather", message.getString("t"))
        assertEquals(293.15, message.getDouble("temp"), 0.001)
        assertEquals(298.65, message.getDouble("hi"), 0.001)
        assertEquals(270.15, message.getDouble("lo"), 0.001)
        assertEquals("Partly cloudy", message.getString("txt"))
    }

    @Test
    fun `the phone's position is blurred to about a kilometre`() {
        val message = Messages.place(51.507351, -0.127758)
        assertEquals(51.51, message.getDouble("lat"), 1e-9)
        assertEquals(-0.13, message.getDouble("lon"), 1e-9)
    }

    @Test
    fun `calendar entries go one to a line and each line fits`() {
        val long = "Quarterly planning with the whole studio and the visiting team — ".repeat(4)
        val events = List(8) { AgendaEvent(1_800_000_000L + it * 3600, 1_800_003_600L + it * 3600, long, long, it == 7) }
        val lines = Messages.agenda(events)

        assertEquals(8, lines.size)
        lines.forEachIndexed { i, line ->
            assertEquals(i, line.getInt("i"))
            assertEquals(8, line.getInt("n"))
            assertTrue(line.getString("title").length <= 40)
            assertTrue(line.getString("loc").length <= 30)
            assertTrue(Link.line(line).size <= Link.MAX_LINE)
        }
        assertTrue(lines[7].getBoolean("allday"))
    }

    private fun utc(text: String) = java.time.Instant.parse(text).toEpochMilli()

    private fun allDay(date: String, title: String, days: Long = 1) = Agenda.Row(
        utc("${date}T00:00:00Z"), utc("${date}T00:00:00Z") + days * 86_400_000L, title, "", true,
    )

    @Test
    fun `today's all-day entry stays listed until local midnight, east or west of Greenwich`() {
        val rows = listOf(allDay("2026-11-09", "Yesterday"), allDay("2026-11-10", "Today"), allDay("2026-11-13", "Too far off"))

        // 23:00 on the 10th in Sydney is midday UTC; 23:00 on the 10th in Los Angeles is 07:00 UTC on the 11th.
        val sydney = Agenda.upcoming(rows, utc("2026-11-10T12:00:00Z"), java.time.ZoneId.of("Australia/Sydney"))
        val losAngeles = Agenda.upcoming(rows, utc("2026-11-11T07:00:00Z"), java.time.ZoneId.of("America/Los_Angeles"))

        assertEquals(listOf("Today"), sydney.map { it.title })
        assertEquals(listOf("Today"), losAngeles.map { it.title })
        assertTrue(sydney[0].allDay)
        // Passed on as the UTC midnights the watch expects.
        assertEquals(utc("2026-11-10T00:00:00Z") / 1000, sydney[0].start)
        assertEquals(utc("2026-11-11T00:00:00Z") / 1000, sydney[0].end)
    }

    @Test
    fun `timed entries are kept from now to two days out, soonest first`() {
        val now = utc("2026-11-10T12:00:00Z")
        fun timed(start: String, end: String, title: String) = Agenda.Row(utc(start), utc(end), title, "", false)
        val rows = listOf(
            timed("2026-11-11T09:00:00Z", "2026-11-11T10:00:00Z", "Tomorrow"),
            timed("2026-11-10T08:00:00Z", "2026-11-10T09:00:00Z", "Over"),
            timed("2026-11-10T11:30:00Z", "2026-11-10T12:30:00Z", "Under way"),
            timed("2026-11-13T09:00:00Z", "2026-11-13T10:00:00Z", "Too far off"),
            allDay("2026-11-11", "Tomorrow, all day"),
            allDay("2026-11-08", "Festival", days = 5),
        )

        val listed = Agenda.upcoming(rows, now, java.time.ZoneOffset.UTC).map { it.title }
        assertEquals(listOf("Festival", "Under way", "Tomorrow, all day", "Tomorrow"), listed)
        assertEquals(2, Agenda.upcoming(rows, now, java.time.ZoneOffset.UTC, limit = 2).size)
    }

    @Test
    fun `the dots for a month mark every local date an entry touches`() {
        val zone = java.time.ZoneId.of("America/New_York") // five hours behind UTC in November
        val month = java.time.YearMonth.of(2026, 11)
        fun timed(start: String, end: String) = Agenda.Row(utc(start), utc(end), "x", "", false)
        val rows = listOf(
            timed("2026-11-04T23:00:00Z", "2026-11-05T00:30:00Z"), // 18:00 to 19:30 on the 4th, New York time
            timed("2026-11-10T02:00:00Z", "2026-11-10T05:00:00Z"), // 21:00 on the 9th until midnight exactly
            timed("2026-11-17T22:00:00Z", "2026-11-19T15:00:00Z"), // runs over three days
            timed("2026-12-01T03:00:00Z", "2026-12-01T04:00:00Z"), // 22:00 on 30 November
            timed("2026-12-01T15:00:00Z", "2026-12-01T16:00:00Z"), // December: not this month
            allDay("2026-11-01", "All day"),                       // the 1st, whatever the time zone
            allDay("2026-10-30", "Trip", days = 3),                // ends on 1 November
        )

        assertEquals(listOf(1, 4, 9, 17, 18, 19, 30), Agenda.busyDays(rows, month, zone))
        assertEquals(emptyList<Int>(), Agenda.busyDays(emptyList(), month, zone))
        val line = Messages.busy(month, listOf(1, 4))
        assertEquals("busy", line.getString("t"))
        assertEquals("2026-11", line.getString("month"))
        assertEquals("[1,4]", line.getJSONArray("days").toString())
    }

    @Test
    fun `the agenda can reach as far as the watch asks`() {
        val now = utc("2026-11-10T12:00:00Z")
        val rows = listOf(
            Agenda.Row(utc("2026-11-25T09:00:00Z"), utc("2026-11-25T10:00:00Z"), "In two weeks", "", false),
            Agenda.Row(utc("2026-12-20T09:00:00Z"), utc("2026-12-20T10:00:00Z"), "Beyond a month", "", false),
            allDay("2026-12-09", "Last day in reach"),
            allDay("2026-12-10", "First day out of reach"),
        )
        val zone = java.time.ZoneOffset.UTC

        assertEquals(emptyList<String>(), Agenda.upcoming(rows, now, zone).map { it.title })
        assertEquals(listOf("In two weeks", "Last day in reach"), Agenda.upcoming(rows, now, zone, limit = 64, days = 30).map { it.title })
    }

    @Test
    fun `one day's entries are those that touch it, all-day ones first`() {
        val zone = java.time.ZoneId.of("America/New_York")
        val date = java.time.LocalDate.parse("2026-12-24")
        val rows = listOf(
            Agenda.Row(utc("2026-12-24T23:00:00Z"), utc("2026-12-25T01:00:00Z"), "Dinner", "Home", false), // 18:00 local
            Agenda.Row(utc("2026-12-24T14:00:00Z"), utc("2026-12-24T15:00:00Z"), "Wrap presents", "", false), // 09:00 local
            Agenda.Row(utc("2026-12-25T04:00:00Z"), utc("2026-12-25T05:00:00Z"), "Late call", "", false), // 23:00 local
            Agenda.Row(utc("2026-12-25T06:00:00Z"), utc("2026-12-25T07:00:00Z"), "Tomorrow", "", false), // 01:00 on the 25th
            allDay("2026-12-24", "Christmas Eve"),
            allDay("2026-12-25", "Christmas Day"),
        )

        val day = Agenda.onDate(rows, date, zone)
        assertEquals(listOf("Christmas Eve", "Wrap presents", "Dinner", "Late call"), day.map { it.title })
        assertEquals(2, Agenda.onDate(rows, date, zone, limit = 2).size)

        val lines = Messages.day(date, day)
        assertEquals(4, lines.size)
        assertTrue(lines.all { it.getString("t") == "day" && it.getString("date") == "2026-12-24" && it.getInt("n") == 4 })
        assertEquals(2, lines[2].getInt("i"))
        assertTrue(lines[0].getBoolean("allday"))

        val empty = Messages.day(date, emptyList())
        assertEquals(1, empty.size)
        assertEquals(0, empty[0].getInt("n"))
        assertEquals("2026-12-24", empty[0].getString("date"))
    }

    @Test
    fun `watch faces and accents merge like the rest`() {
        val config = WatchConfig().merge(JSONObject("""{"brightness":70,"face":2,"accent":2}"""))
        assertEquals("Modular", WatchConfig.FACES[config.face])
        assertEquals("Rose", WatchConfig.ACCENTS[config.accent].first)
        assertEquals(2, config.merge(JSONObject("""{"dnd":true}""")).face)
        assertEquals(6, WatchConfig.ACCENTS.size)
    }

    @Test
    fun `an empty calendar clears the watch's list`() {
        val lines = Messages.agenda(emptyList())
        assertEquals(1, lines.size)
        assertEquals(0, lines[0].getInt("n"))
    }

    @Test
    fun `an untitled entry is shown as busy`() {
        assertEquals("Busy", Messages.agenda(listOf(AgendaEvent(1, 2, " ", "", false)))[0].getString("title"))
    }

    @Test
    fun `quick replies are trimmed, clipped and capped at five`() {
        val list = Messages.replies(listOf(" OK ", "", "On my way", "x".repeat(40), "a", "b", "c", "d")).getJSONArray("list")
        assertEquals(5, list.length())
        assertEquals("OK", list.getString(0))
        assertEquals(24, list.getString(2).length)
    }

    @Test
    fun `a setting change names one setting`() {
        val message = Messages.configure("brightness", 40)
        assertEquals("cfg", message.getString("t"))
        assertEquals(40, message.getJSONObject("set").getInt("brightness"))
    }

    @Test
    fun `a photo is announced with its length and checksum`() {
        val bytes = "123456789".toByteArray()
        val message = Messages.photo("abc", bytes)
        assertEquals("photo", message.getString("kind"))
        assertEquals(9, message.getInt("len"))
        assertEquals("cbf43926", message.getString("crc"))
    }

    @Test
    fun `watch settings merge whole or partial reports`() {
        val first = WatchConfig().merge(JSONObject("""{"brightness":40,"dnd":true,"h24":true,"aod":2,"timeout":30,"goal":10000}"""))
        assertEquals(40, first.brightness)
        assertTrue(first.doNotDisturb)
        assertEquals(2, first.alwaysOn)
        assertEquals(10000, first.stepGoal)
        assertTrue(first.raiseToWake) // not mentioned, so unchanged

        val second = first.merge(JSONObject("""{"dnd":false}"""))
        assertFalse(second.doNotDisturb)
        assertEquals(40, second.brightness)
    }

    @Test
    fun `the step log keeps each day's highest count and survives saving`() {
        val log = StepLog()
        log.record("2026-11-09", 4000)
        log.record("2026-11-10", 1200)
        log.record("2026-11-10", 900) // the watch restarted and counted from zero again
        log.record("2026-11-10", 2500)
        log.record("not a day", 5)
        log.record("2026-11-08", -1)

        val week = StepLog(log.save()).recent(LocalDate.parse("2026-11-10"), 7)
        assertEquals(7, week.size)
        assertEquals(LocalDate.parse("2026-11-04"), week.first().first)
        assertEquals(listOf(0, 0, 0, 0, 0, 4000, 2500), week.map { it.second })
    }

    @Test
    fun `a damaged step log starts afresh and old days fall off`() {
        assertEquals(0, StepLog("{broken").recent(LocalDate.parse("2026-11-10"), 1)[0].second)

        val log = StepLog()
        val start = LocalDate.parse("2026-01-01")
        repeat(200) { log.record(start.plusDays(it.toLong()).toString(), it + 1) }
        val restored = StepLog(log.save())
        assertEquals(0, restored.recent(start, 1)[0].second)
        assertEquals(200, restored.recent(start.plusDays(199), 1)[0].second)
    }
}
