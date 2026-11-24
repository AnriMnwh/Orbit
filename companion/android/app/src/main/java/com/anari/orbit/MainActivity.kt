package com.anari.orbit

import android.Manifest
import android.bluetooth.le.ScanFilter
import android.companion.AssociationInfo
import android.companion.AssociationRequest
import android.companion.BluetoothLeDeviceFilter
import android.companion.CompanionDeviceManager
import android.content.Context
import android.content.IntentSender
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.app.NotificationManagerCompat
import androidx.lifecycle.compose.LifecycleResumeEffect
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.anari.orbit.link.Link
import com.anari.orbit.link.StepLog
import com.anari.orbit.link.WatchConfig
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.time.LocalDate
import java.util.regex.Pattern

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val settings = Settings(this)
        Hub.state.update {
            it.copy(address = settings.watchAddress, steps = StepLog(settings.stepLog).recent(LocalDate.now(), 7))
        }
        LinkService.start(this, quick = true)
        // Test builds only: "adb shell am start -n com.anari.orbit/.MainActivity --ez demo true" fills the
        // screens with a made-up connected watch, so they can be looked at without one.
        if (BuildConfig.DEBUG && intent.getBooleanExtra("demo", false)) {
            Hub.state.value = demoWatch(intent.getIntExtra("accent", 0))
        }
        setContent {
            val watch by Hub.state.collectAsStateWithLifecycle()
            // The app wears the colour chosen on the watch; before the watch has said, the one it wore last.
            OrbitTheme(accent = watch.config?.accent ?: settings.accent) { OrbitApp(watch, settings) }
        }
    }
}

/** A made-up watch with everything switched on, for looking at the screens in test builds. */
private fun demoWatch(accent: Int) = WatchState(
    address = "00:00:00:00:00:00",
    connected = true,
    orbitLink = true,
    firmware = "Orbit 0.1.0",
    battery = 82,
    track = "Night Drive",
    artist = "Lumen Arcade",
    caps = Link.CAPS.toSet(),
    config = WatchConfig(face = 3, accent = accent, alwaysOn = 1, clock24 = true),
    weather = "11°, light rain",
    nextEvent = "Band practice, Wed 19:00",
    steps = listOf(5200, 8100, 3900, 11200, 6400, 7300, 2850).mapIndexed { i, n -> LocalDate.now().minusDays(6L - i) to n },
)

private enum class Tab(val label: String, val icon: Int) {
    Home("Home", R.drawable.ic_home),
    Alerts("Alerts", R.drawable.ic_alerts),
    Photos("Photos", R.drawable.ic_photos),
    Watch("Watch", R.drawable.ic_watch),
}

/** What Android has and has not been allowed to do for the app. Re-read whenever the screen comes back. */
data class Access(
    val bluetooth: Boolean,
    val statusNotification: Boolean,
    val notifications: Boolean,
    val roughLocation: Boolean,
    val location: Boolean,
    val locationAlways: Boolean,
    val calendar: Boolean,
) {
    companion object {
        fun read(context: Context): Access {
            fun has(permission: String) = context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED
            return Access(
                bluetooth = has(Manifest.permission.BLUETOOTH_CONNECT),
                statusNotification = has(Manifest.permission.POST_NOTIFICATIONS),
                notifications = context.packageName in NotificationManagerCompat.getEnabledListenerPackages(context),
                roughLocation = has(Manifest.permission.ACCESS_COARSE_LOCATION),
                location = has(Manifest.permission.ACCESS_FINE_LOCATION),
                locationAlways = has(Manifest.permission.ACCESS_BACKGROUND_LOCATION),
                calendar = has(Manifest.permission.READ_CALENDAR),
            )
        }
    }
}

@Composable
private fun OrbitApp(watch: WatchState, settings: Settings) {
    val context = LocalContext.current
    var tab by rememberSaveable { mutableStateOf(Tab.Home) }
    var access by remember { mutableStateOf(Access.read(context)) }
    val messages = remember { SnackbarHostState() }
    val scope = rememberCoroutineScope()

    LifecycleResumeEffect(Unit) {
        access = Access.read(context)
        onPauseOrDispose { }
    }
    val ask = rememberLauncherForActivityResult(ActivityResultContracts.RequestMultiplePermissions()) {
        access = Access.read(context)
        LinkService.start(context, quick = true)
        Hub.service?.settingsChanged()
    }
    val request: (Array<String>) -> Unit = { ask.launch(it) }
    val say: (String) -> Unit = { text -> scope.launch { messages.showSnackbar(text) } }

    Scaffold(
        snackbarHost = { SnackbarHost(messages) },
        bottomBar = {
            NavigationBar {
                for (entry in Tab.entries) {
                    NavigationBarItem(
                        selected = tab == entry,
                        onClick = { tab = entry },
                        icon = { Icon(painterResource(entry.icon), contentDescription = null) },
                        label = { Text(entry.label) },
                    )
                }
            }
        },
    ) { bars ->
        // Each tab keeps its own scroll position while it is the one showing.
        key(tab) {
            Column(
                Modifier
                    .padding(bars)
                    .fillMaxSize()
                    .verticalScroll(rememberScrollState())
                    .padding(top = 16.dp, bottom = 32.dp),
            ) {
                when (tab) {
                    Tab.Home -> HomeTab(watch, access, settings, request, say)
                    Tab.Alerts -> AlertsTab(watch, access, settings)
                    Tab.Photos -> PhotosTab(watch)
                    Tab.Watch -> WatchTab(watch, access, settings, request)
                }
            }
        }
    }
}

// ------------------------------------------------------- shared small pieces

/** The name of a tab at the top of its screen, with one line saying where things stand. */
@Composable
fun Heading(text: String, below: String = "") {
    Column(Modifier.padding(start = 24.dp, end = 24.dp, top = 24.dp, bottom = 8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Text(text, style = MaterialTheme.typography.headlineLarge)
        if (below.isNotEmpty()) {
            Text(below, style = MaterialTheme.typography.bodyLarge, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
}

/** The label above a group, set as the watch sets its own: small capitals, spaced out, in the dim text colour. */
@Composable
fun Section(title: String) {
    Text(
        title.uppercase(),
        Modifier.padding(start = 32.dp, end = 32.dp, top = 28.dp, bottom = 10.dp),
        style = MaterialTheme.typography.labelMedium.copy(letterSpacing = 1.6.sp),
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** One rounded surface holding the rows that belong together, like a card on the watch. */
@Composable
fun Group(content: @Composable ColumnScope.() -> Unit) {
    Surface(
        Modifier.padding(horizontal = 16.dp).fillMaxWidth(),
        shape = MaterialTheme.shapes.extraLarge,
        color = MaterialTheme.colorScheme.surfaceContainer,
    ) {
        Column(Modifier.padding(vertical = 8.dp), content = content)
    }
}

/** A quieter line of explanation. Works inside a group or on the bare screen. */
@Composable
fun Note(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        modifier.padding(horizontal = 20.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
fun Line(title: String, why: String = "", trailing: @Composable () -> Unit) {
    Row(
        Modifier.padding(horizontal = 20.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
            Text(title, style = MaterialTheme.typography.bodyLarge)
            if (why.isNotEmpty()) {
                Text(why, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
        trailing()
    }
}

/** Something Android must be asked for, with a button until it is granted. */
@Composable
fun Step(title: String, why: String, done: Boolean, action: String, onClick: () -> Unit) = Line(title, why) {
    if (done) {
        Text("Done", style = MaterialTheme.typography.labelLarge, color = MaterialTheme.colorScheme.primary)
    } else {
        FilledTonalButton(onClick) { Text(action) }
    }
}

@Composable
fun SwitchLine(title: String, why: String, on: Boolean, enabled: Boolean = true, onChange: (Boolean) -> Unit) =
    Line(title, why) { Switch(checked = on, onCheckedChange = onChange, enabled = enabled) }

/** Asks Android to find the watch. Only a device that calls itself exactly "Orbit" is offered. */
fun pair(context: Context, show: (IntentSender) -> Unit, paired: (String) -> Unit, failed: (String) -> Unit) {
    val filter = BluetoothLeDeviceFilter.Builder()
        .setNamePattern(Pattern.compile("^Orbit$"))
        .setScanFilter(ScanFilter.Builder().build())
        .build()
    val request = AssociationRequest.Builder().addDeviceFilter(filter).setSingleDevice(false).build()
    val manager = context.getSystemService(CompanionDeviceManager::class.java)
    try {
        manager.associate(request, context.mainExecutor, object : CompanionDeviceManager.Callback() {
            override fun onAssociationPending(intentSender: IntentSender) = show(intentSender)

            override fun onAssociationCreated(associationInfo: AssociationInfo) {
                // Android writes addresses in lower case; Bluetooth wants capitals.
                val address = associationInfo.deviceMacAddress?.toString()?.uppercase()
                if (address != null) paired(address) else failed("Android did not say which watch was chosen.")
            }

            override fun onFailure(error: CharSequence?) = failed("No watch was paired. Make sure it is nearby, awake and not in Keys or MIDI mode.")
        })
    } catch (e: Exception) {
        failed("Pairing could not start: ${e.message}")
    }
}

fun forget(context: Context) {
    val manager = context.getSystemService(CompanionDeviceManager::class.java)
    for (association in manager.myAssociations) manager.disassociate(association.id)
}
