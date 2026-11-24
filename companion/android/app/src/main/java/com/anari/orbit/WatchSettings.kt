package com.anari.orbit

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.anari.orbit.link.WatchConfig
import kotlin.math.roundToInt

@Composable
internal fun WatchSettings(config: WatchConfig) {
    var brightness by remember(config.brightness) { mutableFloatStateOf(config.brightness.toFloat()) }
    Column(Modifier.padding(horizontal = 20.dp, vertical = 10.dp)) {
        Text("Brightness, ${brightness.roundToInt()}%", style = MaterialTheme.typography.bodyLarge)
        // The replacement for this form of Slider is still in beta; keep the settled one until it is not.
        @Suppress("DEPRECATION")
        Slider(
            value = brightness,
            onValueChange = { brightness = it },
            valueRange = 5f..100f,
            // Tell the watch once, when the finger lifts, not for every step along the way.
            onValueChangeFinished = { Hub.service?.configure("brightness", brightness.roundToInt()) },
        )
    }
    SwitchLine("Do not disturb", "Notifications arrive silently.", config.doNotDisturb) { Hub.service?.configure("dnd", it) }
    SwitchLine("24-hour clock", "", config.clock24) { Hub.service?.configure("h24", it) }
    SwitchLine("Raise to wake", "The screen lights when you lift your wrist.", config.raiseToWake) { Hub.service?.configure("raise", it) }
    SwitchLine("Sounds", "", config.sound) { Hub.service?.configure("sound", it) }
    SwitchLine("Grid app launcher", "Show apps in a 3-column icon grid instead of a list.", config.gridLauncher) {
        Hub.service?.configure("grid", it)
    }
    Picker("Always-on display", listOf("Off", "Clock", "Photos"), config.alwaysOn) { Hub.service?.configure("aod", it) }
    val timeouts = listOf(10, 15, 30, 60)
    Picker("Screen rests after", listOf("10 s", "15 s", "30 s", "1 min"), timeouts.indexOf(config.timeoutSeconds)) {
        Hub.service?.configure("timeout", timeouts[it])
    }
    val goals = listOf(5000, 8000, 10000, 12000)
    Picker("Daily step goal", listOf("5,000", "8,000", "10,000", "12,000"), goals.indexOf(config.stepGoal)) {
        Hub.service?.configure("goal", goals[it])
    }
}
