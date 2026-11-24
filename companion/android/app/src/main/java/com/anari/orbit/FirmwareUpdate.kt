package com.anari.orbit

import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
internal fun FirmwareUpdate(watch: WatchState) {
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
            statusMsg = ""
            isError = false
            // Asking the document provider for the file's name is a call to another app.
            scope.launch { pickedName = withContext(Dispatchers.IO) { OtaUpdater.fileName(context, uri) } }
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
