package com.anari.orbit

import android.graphics.BitmapFactory
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.PickVisualMediaRequest
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.unit.dp
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

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
