package com.anari.orbit

import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.compositeOver
import androidx.compose.ui.graphics.drawscope.scale
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.ExperimentalTextApi
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontVariation
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.ceil
import kotlin.math.cos
import kotlin.math.sin

/**
 * The watch's look, carried to the phone so the two read as one product. The
 * values come from assets/logo/README.md in the watch project: its ink ground,
 * its greys, its typeface, and the accent colour the wearer picked.
 */
object Brand {
    val Ink = Color(0xFF0F1115)
    val Surface = Color(0xFF212121)
    val Raised = Color(0xFF313131)
    val Line = Color(0xFF424242)
    val Text = Color(0xFFF1F3F4)
    val TextDim = Color(0xFFAEB3BB)
    val TextFaint = Color(0xFF62676F)
    val Good = Color(0xFF4ADE80)
    val Warn = Color(0xFFFFC53D)
    val Bad = Color(0xFFFF5D5D)

    /** Each accent as the watch shows it on black, and a deeper tone that holds up on a light ground. */
    val Accents = listOf(
        Color(0xFFA8C7FA) to Color(0xFF2F6FE6), // sky
        Color(0xFFA8DAB5) to Color(0xFF1B7F45), // mint
        Color(0xFFFFB1C8) to Color(0xFFC2306A), // rose
        Color(0xFFD0BCFF) to Color(0xFF7E5BE8), // violet
        Color(0xFFFFB68C) to Color(0xFFB8500F), // peach
        Color(0xFFFDD663) to Color(0xFF7A5E00), // sun
    )
}

@OptIn(ExperimentalTextApi::class)
private fun montserrat(weight: Int) = Font(
    R.font.montserrat,
    FontWeight(weight),
    variationSettings = FontVariation.Settings(FontVariation.weight(weight)),
)

private val Montserrat = FontFamily(montserrat(400), montserrat(500), montserrat(600))

/** Material's type scale in the watch's typeface: regular for text, 600 for titles, as on the watch. */
private val OrbitType = Typography().run {
    val title = FontWeight(600)
    copy(
        displaySmall = displaySmall.copy(fontFamily = Montserrat, fontWeight = title, letterSpacing = (-0.5).sp),
        headlineLarge = headlineLarge.copy(fontFamily = Montserrat, fontWeight = title, letterSpacing = (-0.5).sp),
        headlineMedium = headlineMedium.copy(fontFamily = Montserrat, fontWeight = title, letterSpacing = (-0.25).sp),
        headlineSmall = headlineSmall.copy(fontFamily = Montserrat, fontWeight = title),
        titleLarge = titleLarge.copy(fontFamily = Montserrat, fontWeight = title),
        titleMedium = titleMedium.copy(fontFamily = Montserrat, fontWeight = title),
        titleSmall = titleSmall.copy(fontFamily = Montserrat, fontWeight = title),
        bodyLarge = bodyLarge.copy(fontFamily = Montserrat, letterSpacing = 0.sp),
        bodyMedium = bodyMedium.copy(fontFamily = Montserrat, letterSpacing = 0.sp),
        bodySmall = bodySmall.copy(fontFamily = Montserrat),
        labelLarge = labelLarge.copy(fontFamily = Montserrat, fontWeight = title),
        labelMedium = labelMedium.copy(fontFamily = Montserrat, fontWeight = FontWeight(500)),
        labelSmall = labelSmall.copy(fontFamily = Montserrat, fontWeight = FontWeight(500)),
    )
}

/** The watch's cards have a 30 px corner on its 466 px screen, which is about 28 dp here. */
private val OrbitShapes = Shapes(
    small = RoundedCornerShape(12.dp),
    medium = RoundedCornerShape(16.dp),
    large = RoundedCornerShape(24.dp),
    extraLarge = RoundedCornerShape(28.dp),
)

/** [accent] is the index of the colour chosen on the watch. */
@Composable
fun OrbitTheme(accent: Int, content: @Composable () -> Unit) {
    val (pastel, deep) = Brand.Accents[accent.coerceIn(0, Brand.Accents.lastIndex)]
    val colors = if (isSystemInDarkTheme()) {
        darkColorScheme(
            primary = pastel, onPrimary = Brand.Ink,
            primaryContainer = pastel.copy(alpha = 0.24f).compositeOver(Brand.Surface), onPrimaryContainer = pastel,
            secondary = pastel, onSecondary = Brand.Ink,
            secondaryContainer = pastel.copy(alpha = 0.20f).compositeOver(Brand.Surface), onSecondaryContainer = pastel,
            tertiary = pastel, onTertiary = Brand.Ink,
            background = Brand.Ink, onBackground = Brand.Text,
            surface = Brand.Ink, onSurface = Brand.Text,
            surfaceVariant = Brand.Raised, onSurfaceVariant = Brand.TextDim,
            surfaceContainerLowest = Color.Black, surfaceContainerLow = Color(0xFF191A1D),
            surfaceContainer = Brand.Surface, surfaceContainerHigh = Brand.Raised, surfaceContainerHighest = Brand.Raised,
            outline = Color(0xFF7D828A), outlineVariant = Brand.Line,
            error = Brand.Bad, onError = Brand.Ink,
        )
    } else {
        val paper = Color(0xFFF2F3F6)
        lightColorScheme(
            primary = deep, onPrimary = Color.White,
            primaryContainer = pastel.copy(alpha = 0.55f).compositeOver(Color.White), onPrimaryContainer = Color(0xFF14161A),
            secondary = deep, onSecondary = Color.White,
            secondaryContainer = pastel.copy(alpha = 0.45f).compositeOver(Color.White), onSecondaryContainer = Color(0xFF14161A),
            tertiary = deep, onTertiary = Color.White,
            background = paper, onBackground = Color(0xFF14161A),
            surface = paper, onSurface = Color(0xFF14161A),
            surfaceVariant = Color(0xFFE4E7EC), onSurfaceVariant = Color(0xFF4B5058),
            surfaceContainerLowest = Color.White, surfaceContainerLow = Color(0xFFF7F8FA),
            surfaceContainer = Color.White, surfaceContainerHigh = Color(0xFFE9EBF0), surfaceContainerHighest = Color(0xFFE2E5EA),
            outline = Color(0xFF7A7F88), outlineVariant = Color(0xFFD9DCE3),
            error = Color(0xFFC62828), onError = Color.White,
        )
    }
    MaterialTheme(colorScheme = colors, typography = OrbitType, shapes = OrbitShapes, content = content)
}

/**
 * The Orbit mark, drawn from the same arcs as the app icon. While the app is
 * looking for the watch the body circles the ring, as it does on the watch's
 * own start-up screen; once the watch is found it finishes the lap and
 * settles into its gap. With animations turned off in Android it stays put.
 */
@Composable
fun OrbitMark(searching: Boolean, modifier: Modifier = Modifier) {
    val context = LocalContext.current
    val dark = isSystemInDarkTheme()
    val ring = remember { PathParser().parsePathString(RING).toPath() }
    val still = remember {
        android.provider.Settings.Global.getFloat(
            context.contentResolver, android.provider.Settings.Global.ANIMATOR_DURATION_SCALE, 1f,
        ) == 0f
    }
    val angle = remember { Animatable(REST) }

    LaunchedEffect(searching, still) {
        if (searching && !still) {
            while (true) angle.animateTo(angle.value + 360f, tween(2600, easing = LinearEasing))
        } else {
            val home = REST + ceil((angle.value - REST) / 360f) * 360f
            angle.animateTo(home, tween(900, easing = CubicBezierEasing(0.16f, 1f, 0.3f, 1f)))
            angle.snapTo(REST)
        }
    }

    // The mark's own colours, never the accent: deeper ones on a light ground.
    val near = if (dark) Color(0xFF7AB0FF) else Color(0xFF2F6FE6)
    val far = if (dark) Color(0xFFC9B3FF) else Color(0xFF7E5BE8)
    val body = if (dark) Color(0xFFFFB68C) else Color(0xFFE87A33)
    Canvas(modifier) {
        scale(size.minDimension / 108f, pivot = Offset.Zero) {
            drawPath(ring, Brush.linearGradient(listOf(near, far), Offset(34.91f, 73.09f), Offset(73.09f, 34.91f)))
            val turn = Math.toRadians(angle.value.toDouble())
            drawCircle(body, 6f, Offset(54f + 24.75f * cos(turn).toFloat(), 54f + 24.75f * sin(turn).toFloat()))
        }
    }
}

// The ring's outline and the body's resting place, from assets/logo/orbit-mark.svg (a 108 unit square).
private const val RING = "M78.627,42.932 A27.000,27.000 0 1 1 65.068,29.373 A9.600,9.600 0 0 0 62.408,33.420 " +
    "A19.500,19.500 0 1 0 74.580,45.592 A9.600,9.600 0 0 0 78.627,42.932 Z"
private const val REST = -45f
