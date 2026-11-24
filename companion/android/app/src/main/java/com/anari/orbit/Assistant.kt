package com.anari.orbit

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Button
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
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
internal fun Assistant(settings: Settings, activity: String) {
    val scope = rememberCoroutineScope()
    // Reading the key means decrypting it with the Keystore, so that happens off the main thread.
    var saved by remember { mutableStateOf<Boolean?>(null) }
    LaunchedEffect(Unit) { saved = withContext(Dispatchers.IO) { settings.openAiKey != null } }
    var typed by remember { mutableStateOf("") }
    var showModels by remember { mutableStateOf(false) }

    Note(
        "With your OpenAI key here, the watch's assistant can work through the phone instead of joining Wi-Fi. " +
            "The key is stored encrypted on this phone and sent only to OpenAI.",
    )
    Column(Modifier.padding(horizontal = 20.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(12.dp)) {
        if (saved == true) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("Key saved", Modifier.weight(1f), style = MaterialTheme.typography.bodyLarge)
                TextButton(onClick = {
                    saved = false
                    scope.launch(Dispatchers.IO) { settings.openAiKey = null }
                }) { Text("Remove") }
            }
        } else if (saved == false) {
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
                    val key = typed.trim()
                    typed = ""
                    saved = true
                    scope.launch(Dispatchers.IO) { settings.openAiKey = key }
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
