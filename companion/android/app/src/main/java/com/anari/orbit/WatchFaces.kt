package com.anari.orbit

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
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
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.anari.orbit.link.WatchConfig

@Composable
internal fun WatchFacesSection(config: WatchConfig) {
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
                3 -> { // Photo
                    drawCircle(
                        Brush.verticalGradient(listOf(accentColor.copy(alpha = 0.5f), Color(0xFF1A1D24))),
                        radius = w * 0.46f,
                        center = Offset(cx, cy),
                    )
                    val ty = if (style == 1) cy - h * 0.24f else cy + h * 0.14f
                    drawRoundRect(Color.White, Offset(cx - w * 0.20f, ty), Size(w * 0.40f, h * 0.13f), CornerRadius(5f))
                }
                4 -> { // Minimal: 12 batons, accent cardinals, slim hands, date pill
                    for (i in 0 until 12) {
                        val cardinal = i % 3 == 0
                        val ang = (i * 30f - 90f) * Math.PI / 180.0
                        val r0 = if (cardinal) w * 0.38f else w * 0.41f
                        val r1 = w * 0.46f
                        drawLine(
                            color = if (cardinal) accentColor else Color(0xFF5F6368),
                            start = Offset(cx + (r0 * kotlin.math.cos(ang)).toFloat(), cy + (r0 * kotlin.math.sin(ang)).toFloat()),
                            end = Offset(cx + (r1 * kotlin.math.cos(ang)).toFloat(), cy + (r1 * kotlin.math.sin(ang)).toFloat()),
                            strokeWidth = if (cardinal) 3.5f else 2f,
                            cap = StrokeCap.Round,
                        )
                    }
                    drawRoundRect(Color(0xFF212121), Offset(cx - w * 0.12f, cy + h * 0.16f), Size(w * 0.24f, h * 0.08f), CornerRadius(8f))
                    drawLine(Color.White, Offset(cx, cy), Offset(cx - w * 0.14f, cy - h * 0.12f), strokeWidth = 4f, cap = StrokeCap.Round)
                    drawLine(Color.White, Offset(cx, cy), Offset(cx + w * 0.20f, cy - h * 0.26f), strokeWidth = 2.5f, cap = StrokeCap.Round)
                    drawLine(accentColor, Offset(cx, cy), Offset(cx, cy + h * 0.32f), strokeWidth = 1.5f, cap = StrokeCap.Round)
                    drawCircle(accentColor, radius = 4f, center = Offset(cx, cy))
                }
                5 -> { // Custom: built on the setup page, tap on the watch to switch between them
                    drawCircle(Color(0xFF071224), radius = w * 0.46f, center = Offset(cx, cy))
                    drawRoundRect(Color.White, Offset(cx - w * 0.22f, cy - h * 0.12f), Size(w * 0.44f, h * 0.16f), CornerRadius(6f))
                    for (i in -1..1) {
                        drawCircle(
                            if (i == 0) accentColor else accentColor.copy(alpha = 0.4f),
                            radius = w * 0.035f,
                            center = Offset(cx + i * w * 0.10f, cy + h * 0.20f),
                        )
                    }
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
internal fun Picker(title: String, options: List<String>, current: Int, choose: (Int) -> Unit) {
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
internal fun Accents(current: Int, choose: (Int) -> Unit) {
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
