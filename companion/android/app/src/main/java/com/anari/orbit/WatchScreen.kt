package com.anari.orbit

import android.Manifest
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.flow.update

@Composable
fun WatchTab(watch: WatchState, access: Access, settings: Settings, request: (Array<String>) -> Unit) {
    val context = LocalContext.current
    val config = watch.config?.takeIf { watch.connected && "cfg" in watch.caps }

    Heading("Watch", statusLine(watch))

    Section("Watch faces")
    Group {
        if (config != null) {
            WatchFacesSection(config)
            Accents(config.accent) { Hub.service?.configure("accent", it) }
        } else {
            Note(if (watch.connected) "The watch's face and colour will appear here. $OLD_SOFTWARE" else "Connect the watch to choose and edit watch faces.")
        }
    }

    if (config != null) {
        Section("Settings")
        Group { WatchSettings(config) }
    }

    Section("From your phone")
    var weather by remember { mutableStateOf(settings.weatherOn) }
    var calendar by remember { mutableStateOf(settings.calendarOn) }
    Group {
        SwitchLine(
            "Weather",
            "Sends today's weather to the watch about once an hour. To do that, the phone's position, rounded to about a kilometre, goes to the Open-Meteo weather service.",
            weather,
        ) {
            weather = it
            settings.weatherOn = it
            if (it && !access.roughLocation) {
                request(arrayOf(Manifest.permission.ACCESS_COARSE_LOCATION, Manifest.permission.ACCESS_FINE_LOCATION))
            } else {
                Hub.service?.settingsChanged()
            }
        }
        if (weather && !access.roughLocation) Note("Weather needs the Location permission below.")
        SwitchLine("Calendar", "Keeps the watch's calendar in step with this phone's.", calendar) {
            calendar = it
            settings.calendarOn = it
            if (it && !access.calendar) request(arrayOf(Manifest.permission.READ_CALENDAR)) else Hub.service?.settingsChanged()
        }
        if (calendar && watch.connected && "agenda" !in watch.caps) Note(OLD_SOFTWARE)
    }

    Section("Assistant")
    Group { Assistant(settings, watch.assistant) }

    Section("Firmware update")
    Group { FirmwareUpdate(watch) }

    Section("Permissions")
    Group {
        RequiredSteps(access, request)
        Step(
            "Location (optional)",
            "For weather, and for Maps on the watch while Maps is open there. Choose \"Allow all the time\" so Maps works with the phone in your pocket.",
            access.location && access.locationAlways,
            if (access.location) "Always" else "Allow",
        ) {
            // Android grants these in two steps: while in use first, then all the time.
            request(
                if (access.location) arrayOf(Manifest.permission.ACCESS_BACKGROUND_LOCATION)
                else arrayOf(Manifest.permission.ACCESS_FINE_LOCATION, Manifest.permission.ACCESS_COARSE_LOCATION),
            )
        }
        Step("Calendar (optional)", "For the watch's calendar.", access.calendar, "Allow") {
            request(arrayOf(Manifest.permission.READ_CALENDAR))
        }
    }

    if (watch.address != null) {
        Spacer(Modifier.height(20.dp))
        TextButton(
            modifier = Modifier.padding(horizontal = 20.dp),
            onClick = {
                LinkService.stop(context)
                forget(context)
                settings.watchAddress = null
                // A different watch will not have these photos.
                PhotoStore.get(context).markAllUnsent()
                Hub.state.update { WatchState(steps = it.steps) }
            },
        ) { Text("Forget this watch", color = MaterialTheme.colorScheme.error) }
    }
}
