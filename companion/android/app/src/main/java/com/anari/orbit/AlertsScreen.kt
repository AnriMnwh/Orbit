package com.anari.orbit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp

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
