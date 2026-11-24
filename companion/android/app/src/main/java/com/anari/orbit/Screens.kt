package com.anari.orbit

import android.Manifest
import android.content.ComponentName
import android.content.Intent
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.IntentSenderRequest
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.anari.orbit.link.WatchConfig
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.time.Instant
import java.time.LocalDate
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.time.format.TextStyle
import java.util.Locale
import kotlin.math.roundToInt

private const val OLD_SOFTWARE = "This needs newer software on the watch. It will switch on by itself once the watch has it."

fun statusLine(watch: WatchState): String = when {
    watch.address == null -> "No watch paired yet"
    !watch.connected -> "Looking for your watch"
    else -> listOfNotNull(
        "Connected",
        watch.battery.takeIf { it >= 0 }?.let { if (watch.charging) "$it%, charging" else "$it%" },
        watch.firmware.takeIf { it.isNotEmpty() },
    ).joinToString(" · ")
}

// ------------------------------------------------------------------- Home

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
                    day.dayOfWeek.getDisplayName(TextStyle.NARROW, Locale.getDefault()),
                    style = MaterialTheme.typography.labelMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
    }
}

@Composable
private fun RequiredSteps(access: Access, request: (Array<String>) -> Unit) {
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

// ----------------------------------------------------------------- Alerts

@Composable
fun AlertsTab(watch: WatchState, access: Access, settings: Settings) {
    var quiet by remember { mutableStateOf(settings.quietWhileUsingPhone) }
    var blocked by remember { mutableStateOf(settings.blockedApps) }
    val apps = remember { settings.seenApps().toList().sortedBy { it.second.lowercase() } }

    Heading("Alerts", "Choose what reaches your wrist")
    if (!access.notifications) Note("Notification access is off, so nothing is being sent. Turn it on from the Watch tab.", Modifier.padding(horizontal = 4.dp))
    Spacer(Modifier.height(8.dp))
    Group {
        SwitchLine(
            "Quiet while I'm using the phone",
            "Notifications stay off the watch while the phone is unlocked and its screen is on.",
            quiet,
        ) {
            quiet = it
            settings.quietWhileUsingPhone = it
        }
    }

    Section("Quick replies")
    Group { QuickReplies(watch, settings) }

    Section("Apps")
    Group {
        if (apps.isEmpty()) Note("Apps appear here after they send their first notification.")
        for ((packageName, label) in apps) {
            SwitchLine(label, "", packageName !in blocked) { allowed ->
                blocked = if (allowed) blocked - packageName else blocked + packageName
                settings.blockedApps = blocked
            }
        }
    }
}

@Composable
private fun QuickReplies(watch: WatchState, settings: Settings) {
    var replies by remember { mutableStateOf(settings.quickReplies + List(5) { "" }) }
    var saved by remember { mutableStateOf(true) }

    Note("Up to five short answers you can send from the watch with one tap.")
    if (watch.connected && "replies" !in watch.caps) Note("For now the watch uses its built-in replies. $OLD_SOFTWARE")
    Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        for (i in 0 until 5) {
            OutlinedTextField(
                value = replies[i],
                onValueChange = { text ->
                    replies = replies.toMutableList().also { it[i] = text.take(24) }
                    saved = false
                },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                label = { Text("Reply ${i + 1}") },
            )
        }
        Button(
            enabled = !saved,
            onClick = {
                settings.quickReplies = replies.filter { it.isNotBlank() }
                saved = true
                Hub.service?.settingsChanged()
            },
        ) { Text(if (saved) "Saved" else "Save replies") }
    }
}

// ----------------------------------------------------------------- Photos

@Composable
fun PhotosTab(watch: WatchState) {
    val context = LocalContext.current
    val store = remember { PhotoStore.get(context) }
    val photos by store.items.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    var busy by remember { mutableStateOf(false) }
    var failed by remember { mutableStateOf(0) }
    var removing by remember { mutableStateOf<PhotoStore.Item?>(null) }

    // Android's own picker: the app sees only the photos chosen, so it needs no access to the gallery.
    val picker = rememberLauncherForActivityResult(ActivityResultContracts.PickMultipleVisualMedia(12)) { chosen ->
        if (chosen.isEmpty()) return@rememberLauncherForActivityResult
        busy = true
        scope.launch {
            failed = withContext(Dispatchers.IO) { chosen.count { !store.add(context, it) } }
            busy = false
            Hub.service?.photosChanged()
        }
    }

    val waiting = photos.count { it.waiting }
    val onWatch = photos.count { it.sent }
    Heading(
        "Photos",
        when {
            photos.isEmpty() -> "Put your pictures on the watch face"
            waiting == 0 -> "$onWatch on your watch"
            else -> "$onWatch on your watch, $waiting waiting"
        },
    )
    Column(Modifier.padding(horizontal = 4.dp)) {
        Note("Each photo is cut to the watch's round screen on the phone, then sent over Bluetooth.")
        if (watch.connected && "photos" !in watch.caps) Note("Photos wait here for now. $OLD_SOFTWARE")
        if (failed > 0) Note(if (failed == 1) "One picture could not be read." else "$failed pictures could not be read.")
        when (watch.photoProblem) {
            "" -> {}
            "full" -> Note("The watch's photo library is full; it holds 60. Remove some to make room.")
            "space" -> Note("The watch is out of storage. Remove some photos to make room.")
            "lost", "busy" -> Note("A photo did not arrive whole. It will be sent again when the watch next connects.")
            else -> Note("The watch could not save a photo just now. It will be tried again when the watch next connects.")
        }
    }
    Button(
        modifier = Modifier.padding(horizontal = 24.dp, vertical = 12.dp),
        enabled = !busy,
        onClick = { picker.launch(PickVisualMediaRequest(ActivityResultContracts.PickVisualMedia.ImageOnly)) },
    ) { Text(if (busy) "Preparing…" else "Add photos") }

    for (row in photos.chunked(3)) {
        Row(Modifier.padding(horizontal = 24.dp, vertical = 10.dp), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            for (photo in row) {
                Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Thumbnail(photo, Modifier.fillMaxWidth().aspectRatio(1f).clip(CircleShape).clickable { removing = photo })
                    Text(
                        if (photo.sent) "On watch" else if (photo.waiting) "Waiting" else "Not accepted",
                        style = MaterialTheme.typography.labelMedium,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            repeat(3 - row.size) { Spacer(Modifier.weight(1f)) }
        }
    }

    removing?.let { photo ->
        AlertDialog(
            onDismissRequest = { removing = null },
            title = { Text("Remove this photo?") },
            text = { Text(if (photo.sent) "It will be taken off the watch too." else "It has not been sent to the watch yet.") },
            confirmButton = {
                TextButton(onClick = {
                    store.remove(photo.key)
                    Hub.service?.photosChanged()
                    removing = null
                }) { Text("Remove") }
            },
            dismissButton = { TextButton(onClick = { removing = null }) { Text("Keep") } },
        )
    }
}

@Composable
private fun Thumbnail(photo: PhotoStore.Item, modifier: Modifier) {
    val picture by produceState<ImageBitmap?>(null, photo.key) {
        value = withContext(Dispatchers.IO) {
            BitmapFactory.decodeFile(photo.file.path, BitmapFactory.Options().apply { inSampleSize = 2 })?.asImageBitmap()
        }
    }
    val shown = picture
    if (shown != null) Image(shown, contentDescription = "Photo. Tap to remove.", modifier)
    else Box(modifier.background(MaterialTheme.colorScheme.surfaceContainer))
}

// ------------------------------------------------------------------ Watch

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

@Composable
private fun WatchFacesSection(config: WatchConfig) {
    val accent = Brand.Accents.getOrElse(config.accent) { Brand.Accents[0] }.first
    var editingFace by remember { mutableStateOf<Int?>(null) }
    val hiddenFaces = WatchConfig.FACES.indices.filter { it !in config.faces }

    Column(Modifier.padding(vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text("My faces", Modifier.padding(horizontal = 20.dp), style = MaterialTheme.typography.bodyLarge)
        Row(
            Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 20.dp),
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            for (faceId in config.faces) {
                val isActive = config.face == faceId
                val style = config.styles.getOrElse(faceId) { 0 }
                Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Box(contentAlignment = Alignment.BottomCenter) {
                        WatchFacePreview(
                            face = faceId,
                            style = style,
                            accentColor = accent,
                            compLeft = config.compLeft,
                            compRight = config.compRight,
                            active = isActive,
                            modifier = Modifier.size(104.dp).clip(CircleShape).clickable {
                                Hub.service?.configure("face", faceId)
                            },
                        )
                        if (isActive) {
                            Text(
                                "ACTIVE",
                                style = MaterialTheme.typography.labelSmall,
                                fontWeight = FontWeight.Bold,
                                color = Color.Black,
                                modifier = Modifier
                                    .padding(bottom = 6.dp)
                                    .background(accent, RoundedCornerShape(8.dp))
                                    .padding(horizontal = 6.dp, vertical = 2.dp),
                            )
                        }
                    }
                    Text(
                        WatchConfig.FACES.getOrElse(faceId) { "Face" },
                        style = MaterialTheme.typography.labelLarge,
                        fontWeight = if (isActive) FontWeight.Bold else FontWeight.Normal,
                    )
                    FilledTonalButton(
                        onClick = { editingFace = faceId },
                        modifier = Modifier.height(32.dp),
                    ) { Text("Edit", style = MaterialTheme.typography.labelMedium) }
                }
            }
        }

        if (hiddenFaces.isNotEmpty()) {
            Spacer(Modifier.height(4.dp))
            Text("Face gallery", Modifier.padding(horizontal = 20.dp), style = MaterialTheme.typography.bodyLarge)
            Row(
                Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 20.dp),
                horizontalArrangement = Arrangement.spacedBy(16.dp),
            ) {
                for (faceId in hiddenFaces) {
                    val style = config.styles.getOrElse(faceId) { 0 }
                    Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(6.dp)) {
                        WatchFacePreview(
                            face = faceId,
                            style = style,
                            accentColor = accent,
                            compLeft = config.compLeft,
                            compRight = config.compRight,
                            active = false,
                            modifier = Modifier.size(88.dp).clip(CircleShape).clickable {
                                Hub.service?.configure("faces", config.faces + faceId)
                                Hub.service?.configure("face", faceId)
                            },
                        )
                        Text(WatchConfig.FACES.getOrElse(faceId) { "Face" }, style = MaterialTheme.typography.labelMedium)
                        TextButton(onClick = {
                            Hub.service?.configure("faces", config.faces + faceId)
                            Hub.service?.configure("face", faceId)
                        }) { Text("Add") }
                    }
                }
            }
        }
    }

    editingFace?.let { faceId ->
        FaceEditDialog(
            faceId = faceId,
            config = config,
            accentColor = accent,
            onDismiss = { editingFace = null },
        )
    }
}

@Composable
private fun WatchFacePreview(
    face: Int,
    style: Int,
    accentColor: Color,
    compLeft: Int,
    compRight: Int,
    active: Boolean,
    modifier: Modifier = Modifier,
) {
    val borderColor = if (active) accentColor else MaterialTheme.colorScheme.outlineVariant
    Box(
        modifier
            .background(Color.Black, CircleShape)
            .border(if (active) 3.dp else 1.5.dp, borderColor, CircleShape),
        contentAlignment = Alignment.Center,
    ) {
        Canvas(Modifier.matchParentSize().padding(6.dp)) {
            val w = size.width
            val h = size.height
            val cx = w / 2f
            val cy = h / 2f
            when (face) {
                0 -> { // Digital
                    if (style == 1) {
                        drawRoundRect(accentColor, Offset(cx - w * 0.28f, cy - h * 0.22f), Size(w * 0.56f, h * 0.18f), CornerRadius(6f))
                        if (compLeft != 6) drawCircle(accentColor, radius = w * 0.10f, center = Offset(cx - w * 0.16f, cy + h * 0.18f), style = Stroke(3f))
                        if (compRight != 6) drawCircle(Color.White, radius = w * 0.10f, center = Offset(cx + w * 0.16f, cy + h * 0.18f), style = Stroke(3f))
                    } else {
                        drawRoundRect(accentColor, Offset(cx - w * 0.18f, cy - h * 0.28f), Size(w * 0.36f, h * 0.20f), CornerRadius(6f))
                        drawRoundRect(Color(0xFFF1F3F4), Offset(cx - w * 0.18f, cy - h * 0.02f), Size(w * 0.36f, h * 0.20f), CornerRadius(6f))
                        if (style != 2) {
                            if (compLeft != 6) drawCircle(accentColor, radius = w * 0.09f, center = Offset(cx - w * 0.32f, cy), style = Stroke(3f))
                            if (compRight != 6) drawCircle(Color.White, radius = w * 0.09f, center = Offset(cx + w * 0.32f, cy), style = Stroke(3f))
                        }
                    }
                }
                1 -> { // Analog
                    val ticks = if (style == 1) 12 else 24
                    for (i in 0 until ticks) {
                        val ang = (i * (360f / ticks) - 90f) * Math.PI / 180.0
                        val r0 = w * 0.40f
                        val r1 = w * 0.46f
                        drawLine(
                            color = if (style == 2 && i == 0) accentColor else Color(0xFF9EA4B0),
                            start = Offset(cx + (r0 * kotlin.math.cos(ang)).toFloat(), cy + (r0 * kotlin.math.sin(ang)).toFloat()),
                            end = Offset(cx + (r1 * kotlin.math.cos(ang)).toFloat(), cy + (r1 * kotlin.math.sin(ang)).toFloat()),
                            strokeWidth = if (i % (ticks / 12) == 0) 3f else 1.5f,
                            cap = StrokeCap.Round,
                        )
                    }
                    if (style == 0) {
                        if (compLeft != 6) drawCircle(accentColor, radius = w * 0.08f, center = Offset(cx - w * 0.22f, cy), style = Stroke(2.5f))
                        if (compRight != 6) drawCircle(Color.White, radius = w * 0.08f, center = Offset(cx + w * 0.22f, cy), style = Stroke(2.5f))
                    }
                    drawLine(Color.White, Offset(cx, cy), Offset(cx - w * 0.16f, cy - h * 0.14f), strokeWidth = 5f, cap = StrokeCap.Round)
                    drawLine(if (style == 2) accentColor else Color.White, Offset(cx, cy), Offset(cx + w * 0.22f, cy - h * 0.24f), strokeWidth = 3.5f, cap = StrokeCap.Round)
                    drawLine(accentColor, Offset(cx, cy), Offset(cx, cy + h * 0.28f), strokeWidth = 2f, cap = StrokeCap.Round)
                    drawCircle(accentColor, radius = 4f, center = Offset(cx, cy))
                }
                2 -> { // Modular
                    drawRoundRect(accentColor, Offset(cx - w * 0.20f, cy - h * 0.34f), Size(w * 0.40f, h * 0.13f), CornerRadius(5f))
                    drawRoundRect(Color(0xFF212121), Offset(cx - w * 0.34f, cy - h * 0.12f), Size(w * 0.68f, h * 0.22f), CornerRadius(10f))
                    drawRoundRect(accentColor.copy(alpha = 0.7f), Offset(cx - w * 0.28f, cy - h * 0.06f), Size(w * 0.42f, h * 0.04f), CornerRadius(3f))
                    if (compLeft != 6) drawCircle(accentColor, radius = w * 0.09f, center = Offset(cx - w * 0.16f, cy + h * 0.24f), style = Stroke(3f))
                    if (compRight != 6) drawCircle(Color.White, radius = w * 0.09f, center = Offset(cx + w * 0.16f, cy + h * 0.24f), style = Stroke(3f))
                }
                else -> { // Photo
                    drawCircle(
                        Brush.verticalGradient(listOf(accentColor.copy(alpha = 0.5f), Color(0xFF1A1D24))),
                        radius = w * 0.46f,
                        center = Offset(cx, cy),
                    )
                    val ty = if (style == 1) cy - h * 0.24f else cy + h * 0.14f
                    drawRoundRect(Color.White, Offset(cx - w * 0.20f, ty), Size(w * 0.40f, h * 0.13f), CornerRadius(5f))
                }
            }
        }
    }
}

@Composable
private fun FaceEditDialog(faceId: Int, config: WatchConfig, accentColor: Color, onDismiss: () -> Unit) {
    val faceName = WatchConfig.FACES.getOrElse(faceId) { "Face" }
    val styles = WatchConfig.FACE_STYLES.getOrElse(faceId) { listOf("Default") }
    val currentStyle = config.styles.getOrElse(faceId) { 0 }
    val pos = config.faces.indexOf(faceId)
    val canHide = config.faces.size > 1
    val hasComps = faceId in 0..2

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Edit $faceName") },
        text = {
            Column(
                Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(12.dp),
            ) {
                Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.Center) {
                    WatchFacePreview(
                        face = faceId,
                        style = currentStyle,
                        accentColor = accentColor,
                        compLeft = config.compLeft,
                        compRight = config.compRight,
                        active = config.face == faceId,
                        modifier = Modifier.size(132.dp),
                    )
                }
                Picker("Style", styles, currentStyle) { newStyle ->
                    val next = config.styles.toMutableList().apply { while (size < WatchConfig.FACES.size) add(0); this[faceId] = newStyle }
                    Hub.service?.configure("styles", next)
                }
                Accents(config.accent) { Hub.service?.configure("accent", it) }
                if (hasComps) {
                    Picker("Left complication", WatchConfig.COMPLICATIONS, config.compLeft) {
                        Hub.service?.configure("compL", it)
                    }
                    Picker("Right complication", WatchConfig.COMPLICATIONS, config.compRight) {
                        Hub.service?.configure("compR", it)
                    }
                }
                if (config.faces.size > 1 && pos >= 0) {
                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        FilledTonalButton(
                            enabled = pos > 0,
                            onClick = {
                                val next = config.faces.toMutableList()
                                next[pos] = next[pos - 1].also { next[pos - 1] = next[pos] }
                                Hub.service?.configure("faces", next)
                            },
                        ) { Text("Move left") }
                        FilledTonalButton(
                            enabled = pos < config.faces.lastIndex,
                            onClick = {
                                val next = config.faces.toMutableList()
                                next[pos] = next[pos + 1].also { next[pos + 1] = next[pos] }
                                Hub.service?.configure("faces", next)
                            },
                        ) { Text("Move right") }
                    }
                }
                if (canHide) {
                    TextButton(onClick = {
                        Hub.service?.configure("faces", config.faces.filter { it != faceId })
                        onDismiss()
                    }) { Text("Hide from watch", color = MaterialTheme.colorScheme.error) }
                }
            }
        },
        confirmButton = {
            Button(onClick = {
                Hub.service?.configure("face", faceId)
                onDismiss()
            }) { Text("Use face") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("Done") } },
    )
}

/** One choice from a short list, as chips that wrap onto more lines when they need to. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Picker(title: String, options: List<String>, current: Int, choose: (Int) -> Unit) {
    Column(Modifier.padding(horizontal = 20.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            options.forEachIndexed { index, label ->
                FilterChip(selected = current == index, onClick = { choose(index) }, label = { Text(label) })
            }
        }
    }
}

/** The watch's six accent colours. The one chosen also colours this app. */
@Composable
private fun Accents(current: Int, choose: (Int) -> Unit) {
    Column(Modifier.padding(horizontal = 20.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("Colour", style = MaterialTheme.typography.bodyLarge)
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            Brand.Accents.forEachIndexed { index, (pastel, _) ->
                val chosen = index == current
                Box(
                    Modifier
                        .size(48.dp)
                        .clip(CircleShape)
                        .clickable(role = Role.RadioButton) { choose(index) }
                        .semantics {
                            contentDescription = WatchConfig.ACCENTS[index].first
                            selected = chosen
                        },
                    contentAlignment = Alignment.Center,
                ) {
                    Box(
                        Modifier
                            .size(if (chosen) 40.dp else 32.dp)
                            .then(if (chosen) Modifier.border(3.dp, MaterialTheme.colorScheme.onSurface, CircleShape) else Modifier)
                            .padding(if (chosen) 6.dp else 0.dp)
                            .background(pastel, CircleShape),
                    )
                }
            }
        }
    }
}

@Composable
private fun WatchSettings(config: WatchConfig) {
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

@Composable
private fun Assistant(settings: Settings, activity: String) {
    var saved by remember { mutableStateOf(settings.openAiKey != null) }
    var typed by remember { mutableStateOf("") }
    var showModels by remember { mutableStateOf(false) }

    Note(
        "With your OpenAI key here, the watch's assistant can work through the phone instead of joining Wi-Fi. " +
            "The key is stored encrypted on this phone and sent only to OpenAI.",
    )
    Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (saved) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Key saved", Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
                TextButton(onClick = {
                    settings.openAiKey = null
                    saved = false
                }) { Text("Remove") }
            }
        } else {
            OutlinedTextField(
                value = typed,
                onValueChange = { typed = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("OpenAI API key") },
                singleLine = true,
                visualTransformation = PasswordVisualTransformation(),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
            )
            Button(
                enabled = typed.isNotBlank(),
                onClick = {
                    settings.openAiKey = typed.trim()
                    typed = ""
                    saved = true
                },
            ) { Text("Save key") }
        }
        if (activity.isNotEmpty()) Text(activity, color = MaterialTheme.colorScheme.onSurfaceVariant)
        TextButton(onClick = { showModels = !showModels }) { Text(if (showModels) "Hide model names" else "Change model names") }
        if (showModels) {
            // OpenAI retires model names from time to time, so they can be replaced without a new version of the app.
            ModelName("Speech to text", settings.sttModel) { settings.sttModel = it }
            ModelName("Chat", settings.chatModel) { settings.chatModel = it }
            ModelName("Text to speech", settings.ttsModel) { settings.ttsModel = it }
            ModelName("Voice", settings.voice) { settings.voice = it }
        }
    }
}

@Composable
private fun FirmwareUpdate(watch: WatchState) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val ota = watch.ota?.takeIf { System.currentTimeMillis() < it.expiresAtMs }

    var host by remember { mutableStateOf("orbit.local") }
    var pin by remember { mutableStateOf("") }
    var manual by remember { mutableStateOf(false) }
    var pickedUri by remember { mutableStateOf<Uri?>(null) }
    var pickedName by remember { mutableStateOf("") }
    var progress by remember { mutableStateOf<Float?>(null) }
    var statusMsg by remember { mutableStateOf("") }
    var isError by remember { mutableStateOf(false) }

    LaunchedEffect(ota) {
        if (ota != null) {
            if (ota.ip.isNotBlank()) host = ota.ip
            if (ota.pin.isNotBlank()) pin = ota.pin
        }
    }

    val picker = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri != null) {
            pickedUri = uri
            pickedName = OtaUpdater.fileName(context, uri)
            statusMsg = ""
            isError = false
        }
    }

    Column(Modifier.padding(horizontal = 20.dp, vertical = 10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        if (watch.firmware.isNotEmpty()) {
            Text("Current: ${watch.firmware}", style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        if (ota != null) {
            val where = ota.ip.ifBlank { "connecting to Wi-Fi…" }
            Text(
                "Watch ready for update ($where · PIN ${ota.pin})",
                style = MaterialTheme.typography.bodyMedium,
                color = Brand.Good,
            )
        } else {
            Text(
                "On the watch, open Settings > System > Allow update, then choose orbit.bin below.",
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }

        Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
            FilledTonalButton(enabled = progress == null, onClick = { picker.launch(arrayOf("*/*")) }) {
                Text(if (pickedName.isEmpty()) "Choose .bin file" else pickedName, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            TextButton(onClick = { manual = !manual }) {
                Text(if (manual) "Hide IP/PIN" else "Enter IP & PIN")
            }
        }

        if (manual) {
            OutlinedTextField(
                value = host,
                onValueChange = { host = it },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("Watch IP or hostname") },
                placeholder = { Text("orbit.local or 192.168.4.1") },
                singleLine = true,
            )
            OutlinedTextField(
                value = pin,
                onValueChange = { pin = it.filter(Char::isDigit).take(6) },
                modifier = Modifier.fillMaxWidth(),
                label = { Text("6-digit PIN") },
                singleLine = true,
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
            )
        }

        progress?.let { p ->
            LinearProgressIndicator(progress = { p }, modifier = Modifier.fillMaxWidth())
        }

        if (statusMsg.isNotEmpty()) {
            Text(
                statusMsg,
                color = if (isError) MaterialTheme.colorScheme.error else Brand.Good,
                style = MaterialTheme.typography.bodyMedium,
            )
        }

        val canInstall = pickedUri != null && progress == null && OtaUpdater.isLocalHost(host) && pin.length == 6
        Button(
            enabled = canInstall,
            onClick = {
                val uri = pickedUri ?: return@Button
                progress = 0f
                statusMsg = "Uploading firmware…"
                isError = false
                scope.launch {
                    val result = OtaUpdater.upload(context, uri, host, pin) { progress = it }
                    progress = null
                    result.onSuccess {
                        statusMsg = "Installed! The watch is restarting."
                        isError = false
                    }.onFailure { err ->
                        statusMsg = "Update failed: ${err.message ?: "unknown error"}"
                        isError = true
                    }
                }
            },
        ) { Text(if (progress != null) "Installing…" else "Install firmware") }
    }
}

@Composable
private fun ModelName(label: String, current: String, save: (String) -> Unit) {
    var text by remember { mutableStateOf(current) }
    OutlinedTextField(
        value = text,
        onValueChange = {
            text = it
            save(it)
        },
        modifier = Modifier.fillMaxWidth(),
        label = { Text(label) },
        singleLine = true,
    )
}
