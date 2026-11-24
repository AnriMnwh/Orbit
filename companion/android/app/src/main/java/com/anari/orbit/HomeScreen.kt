package com.anari.orbit

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import java.time.format.TextStyle
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

internal const val OLD_SOFTWARE = "This needs newer software on the watch. It will switch on by itself once the watch has it."

fun statusLine(watch: WatchState): String = when {
    watch.address == null -> "No watch paired yet"
    !watch.connected -> "Looking for your watch"
    else -> listOfNotNull(
        "Connected",
        watch.battery.takeIf { it >= 0 }?.let { if (watch.charging) "$it%, charging" else "$it%" },
        watch.firmware.takeIf { it.isNotEmpty() },
    ).joinToString(" · ")
}

@Composable
fun HomeTab(watch: WatchState, access: Access, settings: Settings, request: (Array<String>) -> Unit, say: (String) -> Unit) {
    val context = LocalContext.current
    var problem by remember { mutableStateOf("") }
    // The system's own sheet lists nearby watches; the result arrives in pair()'s callback.
    val chooser = rememberLauncherForActivityResult(ActivityResultContracts.StartIntentSenderForResult()) { }

    Column(
        Modifier.fillMaxWidth().padding(start = 24.dp, end = 24.dp, top = 24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        // The mark is the watch: its body circles while the app looks for it and settles when it is found.
        OrbitMark(searching = watch.address != null && !watch.connected, Modifier.size(156.dp))
        Text(
            when {
                watch.address == null -> "Pair your Orbit"
                watch.connected -> "Connected"
                else -> "Looking for your watch"
            },
            style = MaterialTheme.typography.headlineSmall,
        )
        Spacer(Modifier.height(6.dp))
        Text(
            when {
                watch.address == null -> "Keep the watch close, awake and on its clock face."
                watch.connected -> watch.firmware.ifEmpty { "Orbit" }
                else -> "It connects by itself when it is in range."
            },
            style = MaterialTheme.typography.bodyLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
        if (watch.connected && watch.battery >= 0) {
            Spacer(Modifier.height(16.dp))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                LinearProgressIndicator(
                    progress = { watch.battery / 100f },
                    modifier = Modifier.width(140.dp).height(8.dp),
                    color = if (watch.battery < 15 && !watch.charging) Brand.Bad else MaterialTheme.colorScheme.primary,
                    trackColor = MaterialTheme.colorScheme.outlineVariant,
                    drawStopIndicator = {},
                )
                Text(
                    if (watch.charging) "${watch.battery}%, charging" else "${watch.battery}%",
                    style = MaterialTheme.typography.labelLarge,
                )
            }
        }
        if (problem.isNotEmpty()) {
            Spacer(Modifier.height(12.dp))
            Text(problem, color = MaterialTheme.colorScheme.error, textAlign = TextAlign.Center)
        }
        Spacer(Modifier.height(20.dp))
        if (watch.address == null) {
            Button(
                enabled = access.bluetooth,
                onClick = {
                    problem = ""
                    pair(
                        context,
                        show = { chooser.launch(IntentSenderRequest.Builder(it).build()) },
                        paired = { address ->
                            settings.watchAddress = address
                            Hub.state.update { it.copy(address = address) }
                            LinkService.start(context, quick = true)
                        },
                        failed = { problem = it },
                    )
                },
            ) { Text("Pair watch") }
        } else {
            FilledTonalButton(
                enabled = watch.connected,
                onClick = {
                    Hub.service?.findWatch()
                    say("Asked your watch to ring.")
                },
            ) { Text("Find my watch") }
        }
    }

    if (watch.connected && !watch.orbitLink) {
        Section("Basic mode")
        Group {
            Note(
                "The watch unlocks album art, the assistant and more once it is paired with this phone. " +
                    "If a pairing request was missed or declined, ask again. Older watch software stays in basic mode.",
            )
            TextButton(modifier = Modifier.padding(horizontal = 8.dp), onClick = { Hub.service?.helloAgain() }) {
                Text("Ask to pair again")
            }
        }
    }

    if (watch.track.isNotEmpty()) {
        Section("Now playing")
        Group {
            Row(Modifier.padding(horizontal = 20.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                watch.art?.let {
                    Image(it.asImageBitmap(), contentDescription = null, Modifier.size(56.dp).clip(RoundedCornerShape(16.dp)))
                    Spacer(Modifier.size(16.dp))
                }
                Column {
                    Text(watch.track, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(watch.artist, color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
            }
        }
    }

    Section("Today")
    Group {
        if (watch.weather.isNotEmpty()) Fact("Weather", watch.weather)
        if (watch.nextEvent.isNotEmpty()) Fact("Next", watch.nextEvent)
        Fact("Steps", "%,d".format(watch.steps.lastOrNull()?.second ?: 0) + (watch.config?.let { " of %,d".format(it.stepGoal) } ?: ""))
        StepChart(watch)
        if (watch.steps.all { it.second == 0 }) Note("Your step history builds up here as the watch reports it.")
    }

    if (!access.bluetooth || !access.notifications || !access.statusNotification) {
        Section("Finish setting up")
        Group { RequiredSteps(access, request) }
    }
}

@Composable
private fun Fact(label: String, value: String) {
    Row(Modifier.padding(horizontal = 20.dp, vertical = 10.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
        Text(label, Modifier.width(76.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
        Text(value, Modifier.weight(1f))
    }
}

/** Seven bars, today on the right, scaled to the goal or the best day, whichever is larger. */
@Composable
private fun StepChart(watch: WatchState) {
    val top = maxOf(watch.steps.maxOfOrNull { it.second } ?: 0, watch.config?.stepGoal ?: 0, 1)
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 20.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
        verticalAlignment = Alignment.Bottom,
    ) {
        for ((day, steps) in watch.steps) {
            Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Box(
                    Modifier
                        .fillMaxWidth()
                        .height((6 + 66 * steps / top).dp)
                        .background(
                            if (steps > 0) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant,
                            RoundedCornerShape(8.dp),
                        ),
                )
                Text(
                    day.dayOfWeek.getDisplayName(TextStyle.NARROW, LocalConfiguration.current.locales[0]),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
internal fun RequiredSteps(access: Access, request: (Array<String>) -> Unit) {
    val context = LocalContext.current
    Step("Bluetooth", "Lets the app talk to the watch.", access.bluetooth, "Allow") {
        request(arrayOf(Manifest.permission.BLUETOOTH_CONNECT))
    }
    Step("Notification access", "Sends notifications, calls and what is playing to the watch.", access.notifications, "Open") {
        context.startActivity(
            Intent(android.provider.Settings.ACTION_NOTIFICATION_LISTENER_DETAIL_SETTINGS).putExtra(
                android.provider.Settings.EXTRA_NOTIFICATION_LISTENER_COMPONENT_NAME,
                ComponentName(context, NotificationRelay::class.java).flattenToString(),
            ),
        )
    }
    Step("Connection status", "Shows a quiet notification while the watch is linked.", access.statusNotification, "Allow") {
        request(arrayOf(Manifest.permission.POST_NOTIFICATIONS))
    }
}
