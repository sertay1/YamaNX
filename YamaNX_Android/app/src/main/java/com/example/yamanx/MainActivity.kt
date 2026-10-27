package com.example.yamanx

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.content.res.Configuration
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyGridScope
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.grid.rememberLazyGridState
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.ArrowBack
import androidx.compose.material.icons.automirrored.rounded.HelpOutline
import androidx.compose.material.icons.automirrored.rounded.KeyboardArrowRight
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.automirrored.rounded.Sort
import androidx.compose.material.icons.rounded.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.painter.ColorPainter
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.layout.layout
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.core.app.NotificationManagerCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import coil.compose.AsyncImage
import coil.request.ImageRequest
import com.example.yamanx.data.Patch
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import java.io.File

// ═════════════════════════════════════════════════════════════════════════════
//  DESIGN SYSTEM
// ═════════════════════════════════════════════════════════════════════════════
object Yx {
    val Bg          = Color(0xFF09090F)
    val Surface1    = Color(0xFF111119)
    val Surface2    = Color(0xFF181823)
    val Surface3    = Color(0xFF22222F)
    val Stroke      = Color(0x14FFFFFF)
    val StrokeHi    = Color(0x26FFFFFF)

    val Red         = Color(0xFFE60012)
    val RedBright   = Color(0xFFFF3B4E)
    val RedDeep     = Color(0xFFB0000F)
    val Green       = Color(0xFF2EE59D)
    val Amber       = Color(0xFFFFB547)
    val Gold        = Color(0xFFFFD24A)
    val Blue        = Color(0xFF4DA3FF)
    val Purple      = Color(0xFFA78BFA)

    val Text        = Color(0xFFF4F4F8)
    val TextMuted   = Color(0xFF9A9AAE)
    val TextFaint   = Color(0xFF62627A)

    val RedGradient = Brush.linearGradient(listOf(RedBright, Red, RedDeep))
}

val LocalBottomPad = compositionLocalOf { 24.dp }

private fun coverUrl(titleId: String) =
    "https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/$titleId.jpg"

private data class StatusInfo(val label: String, val color: Color, val icon: ImageVector)

private fun Patch.status(): StatusInfo? = when {
    isInstalled   -> StatusInfo("Yüklü", Yx.Green, Icons.Rounded.CheckCircle)
    gameInstalled -> StatusInfo("Cihazda", Yx.Amber, Icons.Rounded.SportsEsports)
    else          -> null
}

private fun Patch.versionText(): String = patchVersion.trim().ifBlank { "—" }

private enum class PatchFilter(val label: String) { ALL("Tümü"), ON_DEVICE("Cihazımda"), INSTALLED("Yüklü") }

// ─── Tabs ─────────────────────────────────────────────────────────────────────
private data class Tab(val route: String, val label: String, val icon: ImageVector, val iconSelected: ImageVector)

private val tabs = listOf(
    Tab("discover",  "Yamalar",   Icons.Rounded.GridView,     Icons.Rounded.GridView),
    Tab("favorites", "Favoriler", Icons.Rounded.StarOutline,   Icons.Rounded.Star),
    Tab("installed", "Yüklü",     Icons.Rounded.Inventory2,   Icons.Rounded.Inventory2),
    Tab("settings",  "Ayarlar",   Icons.Rounded.Settings,     Icons.Rounded.Settings),
    Tab("about",     "Hakkında",  Icons.Rounded.Info,         Icons.Rounded.Info),
)

// ═════════════════════════════════════════════════════════════════════════════
//  ACTIVITY
// ═════════════════════════════════════════════════════════════════════════════
class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT),
            navigationBarStyle = SystemBarStyle.dark(android.graphics.Color.TRANSPARENT)
        )
        super.onCreate(savedInstanceState)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                if (!Environment.isExternalStorageManager()) {
                    startActivity(
                        Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                            .setData(Uri.parse("package:$packageName"))
                    )
                }
            } catch (e: Exception) {
                try {
                    startActivity(
                        Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                            .setData(Uri.parse("package:$packageName"))
                    )
                } catch (_: Exception) {}
            }
        }

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    primary = Yx.Red,
                    onPrimary = Color.White,
                    background = Yx.Bg,
                    onBackground = Yx.Text,
                    surface = Yx.Surface1,
                    onSurface = Yx.Text,
                    surfaceVariant = Yx.Surface2,
                    onSurfaceVariant = Yx.TextMuted,
                )
            ) {
                val viewModel: AppViewModel = viewModel()

                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                    val notifLauncher = rememberLauncherForActivityResult(
                        ActivityResultContracts.RequestPermission()
                    ) {}
                    LaunchedEffect(Unit) { notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS) }
                }

                Box(Modifier.fillMaxSize().background(Yx.Bg)) {
                    YamaApp(viewModel)
                    if (viewModel.isDownloading.value) DownloadDialog(viewModel)
                    if (viewModel.showUpdatePromptDialog.value) {
                        UpdatePromptDialog(
                            version = viewModel.updateTagName.value,
                            releaseNotes = viewModel.updateReleaseNotes.value,
                            onUpdate = {
                                viewModel.showUpdatePromptDialog.value = false
                                viewModel.downloadAndInstallUpdate(this@MainActivity)
                            },
                            onDismiss = {
                                viewModel.showUpdatePromptDialog.value = false
                            }
                        )
                    }
                    SplashOverlay()
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        try {
            val yamaDir = File(Environment.getExternalStorageDirectory(), "YamaNX")
            if (!yamaDir.exists()) yamaDir.mkdirs()
        } catch (_: Exception) {}
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  SPLASH
// ═════════════════════════════════════════════════════════════════════════════
private var splashAlreadyShown = false

@Composable
fun SplashOverlay() {
    if (splashAlreadyShown) return
    var visible by remember { mutableStateOf(true) }
    val alpha by animateFloatAsState(if (visible) 1f else 0f, tween(450), label = "splashA")
    val scale by animateFloatAsState(if (visible) 1f else 1.08f, tween(450), label = "splashS")
    LaunchedEffect(Unit) {
        delay(1100)
        visible = false
        delay(450)
        splashAlreadyShown = true
    }
    if (alpha <= 0f) return

    Box(
        Modifier
            .fillMaxSize()
            .graphicsLayer { this.alpha = alpha }
            .background(Yx.Bg)
            .ambientGlow()
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) {},
        contentAlignment = Alignment.Center
    ) {
        Column(
            horizontalAlignment = Alignment.CenterHorizontally,
            modifier = Modifier.graphicsLayer { scaleX = scale; scaleY = scale }
        ) {
            Image(
                painterResource(R.drawable.logo_wide), "YamaNX",
                contentScale = ContentScale.Fit,
                modifier = Modifier.width(230.dp)
            )
            Spacer(Modifier.height(18.dp))
            Text("Türkçe Yama Arşivi", color = Yx.TextMuted, fontSize = 13.sp, letterSpacing = 0.5.sp)
        }
        val inf = rememberInfiniteTransition(label = "splashBar")
        val x by inf.animateFloat(0f, 1f, infiniteRepeatable(tween(900, easing = FastOutSlowInEasing)), label = "x")
        Box(
            Modifier
                .align(Alignment.BottomCenter)
                .navigationBarsPadding()
                .padding(bottom = 56.dp)
                .width(120.dp)
                .height(3.dp)
                .clip(CircleShape)
                .background(Yx.Surface3)
        ) {
            Box(
                Modifier
                    .fillMaxHeight()
                    .fillMaxWidth(0.35f)
                    .graphicsLayer { translationX = (x * 1.65f - 0.35f) * size.width / 0.35f * 0.35f * 2.2f }
                    .background(Yx.RedGradient, CircleShape)
            )
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  UPDATE PROMPT DIALOG
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun UpdatePromptDialog(
    version: String,
    releaseNotes: String,
    onUpdate: () -> Unit,
    onDismiss: () -> Unit
) {
    AlertDialog(
        onDismissRequest = onDismiss,
        containerColor = Yx.Surface1,
        shape = RoundedCornerShape(28.dp),
        tonalElevation = 12.dp,
        icon = {
            Box(
                Modifier
                    .size(54.dp)
                    .clip(CircleShape)
                    .background(Yx.Red.copy(alpha = 0.15f))
                    .border(1.dp, Yx.Red.copy(alpha = 0.4f), CircleShape),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    Icons.Rounded.RocketLaunch,
                    contentDescription = null,
                    tint = Yx.RedBright,
                    modifier = Modifier.size(26.dp)
                )
            }
        },
        title = {
            Text(
                text = "Yeni Güncelleme Mevcut!",
                color = Yx.Text,
                fontSize = 19.sp,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
        },
        text = {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    text = if (version.isNotBlank()) "YamaNX $version sürümü yayında." else "Yeni bir sürüm yayınlandı.",
                    color = Yx.TextMuted,
                    fontSize = 13.sp,
                    textAlign = TextAlign.Center
                )
                Spacer(Modifier.height(4.dp))
                Text(
                    text = "Hemen güncellemek ister misiniz?",
                    color = Yx.Text,
                    fontSize = 14.sp,
                    fontWeight = FontWeight.SemiBold,
                    textAlign = TextAlign.Center
                )
                if (releaseNotes.isNotBlank()) {
                    Spacer(Modifier.height(12.dp))
                    Column(
                        Modifier
                            .fillMaxWidth()
                            .heightIn(max = 130.dp)
                            .clip(RoundedCornerShape(16.dp))
                            .background(Yx.Surface2)
                            .border(1.dp, Yx.Stroke, RoundedCornerShape(16.dp))
                            .padding(10.dp)
                            .verticalScroll(rememberScrollState())
                    ) {
                        Text(
                            text = "Yenilikler:",
                            color = Yx.RedBright,
                            fontSize = 11.sp,
                            fontWeight = FontWeight.Bold
                        )
                        Spacer(Modifier.height(2.dp))
                        Text(
                            text = releaseNotes,
                            color = Yx.Text.copy(alpha = 0.9f),
                            fontSize = 12.sp,
                            lineHeight = 17.sp
                        )
                    }
                }
            }
        },
        confirmButton = {
            GradientButton(
                text = "Hemen Güncelle",
                icon = Icons.Rounded.Download,
                onClick = onUpdate,
                modifier = Modifier.fillMaxWidth()
            )
        },
        dismissButton = {
            TextButton(
                onClick = onDismiss,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text("Daha Sonra", color = Yx.TextMuted, fontSize = 13.sp)
            }
        }
    )
}

// ═════════════════════════════════════════════════════════════════════════════
//  APP SHELL
// ═════════════════════════════════════════════════════════════════════════════
fun Modifier.ambientGlow(): Modifier = this.drawBehind {
    drawRect(
        Brush.radialGradient(
            colors = listOf(Yx.Red.copy(alpha = 0.16f), Color.Transparent),
            center = Offset(size.width * 0.9f, -size.width * 0.05f),
            radius = size.width * 1.0f
        )
    )
    drawRect(
        Brush.radialGradient(
            colors = listOf(Yx.Purple.copy(alpha = 0.06f), Color.Transparent),
            center = Offset(0f, size.height * 0.55f),
            radius = size.width * 0.9f
        )
    )
}

@Composable
fun YamaApp(viewModel: AppViewModel) {
    val nav = rememberNavController()
    val route = nav.currentBackStackEntryAsState().value?.destination?.route
    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    val isWide = configuration.screenWidthDp >= 600 || isLandscape
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    // Refresh installed state whenever the user comes back to the app
    DisposableEffect(lifecycleOwner) {
        val obs = LifecycleEventObserver { _, e ->
            if (e == Lifecycle.Event.ON_RESUME) viewModel.checkLoadFolderAccess()
        }
        lifecycleOwner.lifecycle.addObserver(obs)
        onDispose { lifecycleOwner.lifecycle.removeObserver(obs) }
    }

    // Global toast channel (updater messages)
    LaunchedEffect(viewModel.updateToastMessage.value) {
        viewModel.updateToastMessage.value?.let {
            Toast.makeText(context, it, Toast.LENGTH_SHORT).show()
            viewModel.updateToastMessage.value = null
        }
    }

    fun go(r: String) {
        viewModel.requestScrollToTop(r)
        if (route != r) {
            nav.navigate(r) {
                popUpTo(nav.graph.findStartDestination().id) { saveState = false }
                launchSingleTop = true
                restoreState = false
            }
        }
    }
    val openGuide: () -> Unit = { nav.navigate("guide") { launchSingleTop = true } }
    val back: () -> Unit = { nav.popBackStack() }

    val navBottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding()

    val host: @Composable () -> Unit = {
        NavHost(
            navController = nav,
            startDestination = "discover",
            enterTransition = { fadeIn(tween(220)) + scaleIn(tween(220), initialScale = 0.985f) },
            exitTransition = { fadeOut(tween(150)) },
            popEnterTransition = { fadeIn(tween(220)) },
            popExitTransition = { fadeOut(tween(150)) }
        ) {
            composable("discover")  { DiscoverScreen(viewModel, openGuide) }
            composable("favorites") { FavoritesScreen(viewModel) }
            composable("installed") { InstalledScreen(viewModel, openGuide) }
            composable("settings")  { SettingsScreen(viewModel, openGuide) }
            composable("about")     { AboutScreen(viewModel) }
            composable("guide")     { GuideScreen(back) }
        }
    }

    Box(Modifier.fillMaxSize().ambientGlow()) {
        if (isWide) {
            Row(
                Modifier
                    .fillMaxSize()
                    .windowInsetsPadding(WindowInsets.safeDrawing)
            ) {
                SideRail(route, ::go, viewModel)
                Box(Modifier.weight(1f).fillMaxHeight()) {
                    CompositionLocalProvider(LocalBottomPad provides 20.dp) { host() }
                }
            }
        } else {
            CompositionLocalProvider(LocalBottomPad provides navBottom + 104.dp) { host() }
            FloatingNavBar(
                route = route,
                onSelect = ::go,
                viewModel = viewModel,
                modifier = Modifier.align(Alignment.BottomCenter)
            )
        }
    }
}

// ─── Floating bottom bar (phones) ─────────────────────────────────────────────
@Composable
fun FloatingNavBar(route: String?, onSelect: (String) -> Unit, viewModel: AppViewModel, modifier: Modifier = Modifier) {
    val shape = RoundedCornerShape(28.dp)
    Box(
        modifier
            .fillMaxWidth()
            .background(Brush.verticalGradient(listOf(Color.Transparent, Yx.Bg.copy(alpha = 0.92f), Yx.Bg)))
            .navigationBarsPadding()
            .padding(start = 14.dp, end = 14.dp, top = 18.dp, bottom = 10.dp)
    ) {
        Row(
            Modifier
                .fillMaxWidth()
                .height(70.dp)
                .shadow(28.dp, shape, ambientColor = Color.Black, spotColor = Color.Black)
                .clip(shape)
                .background(Yx.Surface1)
                .border(1.dp, Yx.StrokeHi, shape)
                .padding(horizontal = 6.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            tabs.forEach { tab ->
                NavTabItem(
                    tab = tab,
                    selected = route == tab.route,
                    badge = tabBadge(tab.route, viewModel),
                    onClick = { onSelect(tab.route) },
                    modifier = Modifier.weight(1f).fillMaxHeight()
                )
            }
        }
    }
}

// ─── Side rail (tablets / landscape) ──────────────────────────────────────────
@Composable
fun SideRail(route: String?, onSelect: (String) -> Unit, viewModel: AppViewModel) {
    val screenHeight = LocalConfiguration.current.screenHeightDp
    val compact = screenHeight < 540
    val tabHeight = if (compact) 44.dp else 66.dp
    val spacing = if (compact) 4.dp else 6.dp

    Column(
        Modifier
            .width(if (compact) 72.dp else 96.dp)
            .fillMaxHeight()
            .background(Yx.Surface1.copy(alpha = 0.95f))
            .drawBehind {
                drawLine(Yx.Stroke, Offset(size.width, 0f), Offset(size.width, size.height), 1.dp.toPx())
            }
            .padding(vertical = if (compact) 8.dp else 16.dp)
            .verticalScroll(rememberScrollState()),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Image(
            painterResource(R.drawable.app_badge), "YamaNX",
            modifier = Modifier.size(if (compact) 36.dp else 52.dp).clip(RoundedCornerShape(14.dp))
        )
        Spacer(Modifier.height(if (compact) 8.dp else 22.dp))
        tabs.forEach { tab ->
            NavTabItem(
                tab = tab,
                selected = route == tab.route,
                badge = tabBadge(tab.route, viewModel),
                onClick = { onSelect(tab.route) },
                showLabel = !compact,
                modifier = Modifier.fillMaxWidth().height(tabHeight).padding(horizontal = 4.dp)
            )
            Spacer(Modifier.height(spacing))
        }
        Spacer(Modifier.weight(1f, fill = !compact))
        if (!compact) {
            Text("v${viewModel.currentAppVersion}", color = Yx.TextFaint, fontSize = 11.sp)
        }
    }
}

private sealed interface TabBadge {
    data class Count(val n: Int) : TabBadge
    data object Dot : TabBadge
}

@Composable
private fun tabBadge(route: String, vm: AppViewModel): TabBadge? = when {
    route == "favorites" && vm.favorites.isNotEmpty() -> TabBadge.Count(vm.favorites.size)
    route == "settings" && vm.isUpdateAvailable.value -> TabBadge.Dot
    else -> null
}

@Composable
private fun NavTabItem(
    tab: Tab,
    selected: Boolean,
    badge: TabBadge?,
    onClick: () -> Unit,
    modifier: Modifier,
    showLabel: Boolean = true
) {
    val pillColor by animateColorAsState(if (selected) Yx.Red.copy(alpha = 0.20f) else Color.Transparent, label = "pill")
    val iconColor by animateColorAsState(if (selected) Yx.RedBright else Yx.TextMuted, label = "icon")
    val textColor by animateColorAsState(if (selected) Yx.Text else Yx.TextMuted, label = "text")
    val pillWidth by animateDpAsState(if (selected) 56.dp else 40.dp, spring(dampingRatio = 0.7f), label = "pw")

    Column(
        modifier
            .clip(RoundedCornerShape(20.dp))
            .clickable(onClick = onClick),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center
    ) {
        Box(contentAlignment = Alignment.Center) {
            Box(
                Modifier
                    .width(pillWidth)
                    .height(30.dp)
                    .clip(CircleShape)
                    .background(pillColor)
            )
            Box(
                Modifier.size(30.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    if (selected) tab.iconSelected else tab.icon,
                    contentDescription = tab.label,
                    tint = iconColor,
                    modifier = Modifier.size(22.dp)
                )
                when (badge) {
                    is TabBadge.Count -> Box(
                        Modifier
                            .align(Alignment.TopEnd)
                            .offset(x = 5.dp, y = (-3).dp)
                            .defaultMinSize(minWidth = 16.dp, minHeight = 16.dp)
                            .clip(CircleShape)
                            .background(Yx.Gold)
                            .border(1.5.dp, Yx.Surface1, CircleShape)
                            .padding(horizontal = 3.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Text(
                            text = if (badge.n > 99) "99+" else "${badge.n}",
                            color = Color(0xFF1A1400),
                            fontSize = 9.sp,
                            fontWeight = FontWeight.Black,
                            textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                            lineHeight = 10.sp
                        )
                    }
                    TabBadge.Dot -> Box(
                        Modifier
                            .align(Alignment.TopEnd)
                            .offset(x = 2.dp, y = 0.dp)
                            .size(9.dp)
                            .clip(CircleShape)
                            .background(Yx.Green)
                            .border(1.5.dp, Yx.Surface1, CircleShape)
                    )
                    null -> {}
                }
            }
        }
        if (showLabel) {
            Spacer(Modifier.height(4.dp))
            Text(
                tab.label,
                color = textColor,
                fontSize = 11.sp,
                fontWeight = if (selected) FontWeight.SemiBold else FontWeight.Medium,
                maxLines = 1
            )
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  SHARED COMPONENTS
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun Modifier.pressScale(source: MutableInteractionSource, pressed: Float = 0.96f): Modifier {
    val isPressed by source.collectIsPressedAsState()
    val s by animateFloatAsState(if (isPressed) pressed else 1f, spring(stiffness = Spring.StiffnessMediumLow), label = "press")
    return this.graphicsLayer { scaleX = s; scaleY = s }
}

/** Lets a child escape the horizontal content padding of its lazy parent (edge-to-edge rows). */
fun Modifier.bleedHorizontal(amount: Dp): Modifier = this.layout { measurable, constraints ->
    val px = amount.roundToPx()
    val w = constraints.maxWidth + px * 2
    val placeable = measurable.measure(constraints.copy(minWidth = w, maxWidth = w))
    layout(constraints.maxWidth, placeable.height) { placeable.place(-px, 0) }
}

@Composable
fun ScreenHeader(
    title: String,
    subtitle: String? = null,
    overline: String? = null,
    onBack: (() -> Unit)? = null,
    actions: @Composable RowScope.() -> Unit = {}
) {
    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    Row(
        Modifier
            .fillMaxWidth()
            .then(if (!isLandscape) Modifier.statusBarsPadding() else Modifier)
            .padding(
                start = 20.dp,
                end = 16.dp,
                top = if (isLandscape) 8.dp else 16.dp,
                bottom = if (isLandscape) 6.dp else 12.dp
            ),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (onBack != null) {
            CircleIconButton(Icons.AutoMirrored.Rounded.ArrowBack, "Geri", onBack)
            Spacer(Modifier.width(14.dp))
        }
        Column(Modifier.weight(1f)) {
            if (overline != null) {
                Text(
                    overline.uppercase(),
                    color = Yx.RedBright, fontSize = 11.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.6.sp
                )
                Spacer(Modifier.height(2.dp))
            }
            Text(title, color = Yx.Text, fontSize = 28.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = (-0.5).sp)
            if (subtitle != null) {
                Spacer(Modifier.height(2.dp))
                Text(subtitle, color = Yx.TextMuted, fontSize = 13.sp)
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically, content = actions)
    }
}

@Composable
fun CircleIconButton(icon: ImageVector, description: String, onClick: () -> Unit, tint: Color = Yx.Text, size: Dp = 44.dp) {
    val src = remember { MutableInteractionSource() }
    Box(
        Modifier
            .size(size)
            .pressScale(src, 0.9f)
            .clip(CircleShape)
            .background(Yx.Surface2)
            .border(1.dp, Yx.Stroke, CircleShape)
            .clickable(interactionSource = src, indication = LocalIndication.current, onClick = onClick),
        contentAlignment = Alignment.Center
    ) {
        Icon(icon, description, tint = tint, modifier = Modifier.size(20.dp))
    }
}

@Composable
fun SearchField(
    value: String,
    onChange: (String) -> Unit,
    placeholder: String,
    modifier: Modifier = Modifier,
    height: Dp = 54.dp
) {
    val focus = LocalFocusManager.current
    val shape = RoundedCornerShape(18.dp)
    Row(
        modifier
            .height(height)
            .clip(shape)
            .background(Yx.Surface2)
            .border(1.dp, Yx.Stroke, shape)
            .padding(start = 16.dp, end = 8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(Icons.Rounded.Search, null, tint = Yx.TextMuted, modifier = Modifier.size(21.dp))
        Spacer(Modifier.width(12.dp))
        Box(Modifier.weight(1f), contentAlignment = Alignment.CenterStart) {
            if (value.isEmpty()) Text(placeholder, color = Yx.TextFaint, fontSize = 14.sp, maxLines = 1)
            BasicTextField(
                value = value,
                onValueChange = onChange,
                singleLine = true,
                textStyle = TextStyle(color = Yx.Text, fontSize = 14.sp, fontWeight = FontWeight.Medium),
                cursorBrush = SolidColor(Yx.RedBright),
                keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                keyboardActions = KeyboardActions(onSearch = { focus.clearFocus() }),
                modifier = Modifier.fillMaxWidth()
            )
        }
        AnimatedVisibility(value.isNotEmpty(), enter = fadeIn() + scaleIn(), exit = fadeOut() + scaleOut()) {
            Box(
                Modifier
                    .size(34.dp)
                    .clip(CircleShape)
                    .background(Yx.Surface3)
                    .clickable { onChange("") },
                contentAlignment = Alignment.Center
            ) {
                Icon(Icons.Rounded.Close, "Temizle", tint = Yx.TextMuted, modifier = Modifier.size(16.dp))
            }
        }
    }
}

@Composable
fun FilterChipPill(label: String, count: Int?, selected: Boolean, onClick: () -> Unit, accent: Color = Yx.Red) {
    val bg by animateColorAsState(if (selected) accent else Yx.Surface2, label = "chipBg")
    val fg by animateColorAsState(if (selected) Color.White else Yx.TextMuted, label = "chipFg")
    Row(
        Modifier
            .height(38.dp)
            .clip(CircleShape)
            .background(bg)
            .border(1.dp, if (selected) Color.Transparent else Yx.Stroke, CircleShape)
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, color = fg, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
        if (count != null) {
            Spacer(Modifier.width(6.dp))
            Text("$count", color = fg.copy(alpha = 0.7f), fontSize = 12.sp, fontWeight = FontWeight.Medium)
        }
    }
}

@Composable
fun StatusBadge(label: String, color: Color, icon: ImageVector? = null) {
    Row(
        Modifier
            .clip(CircleShape)
            .background(Color.Black.copy(alpha = 0.55f))
            .border(1.dp, color.copy(alpha = 0.55f), CircleShape)
            .padding(start = if (icon != null) 5.dp else 8.dp, end = 8.dp, top = 3.dp, bottom = 3.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        if (icon != null) {
            Icon(icon, null, tint = color, modifier = Modifier.size(12.dp))
            Spacer(Modifier.width(4.dp))
        }
        Text(label, color = color, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun SortPill(label: String, onClick: () -> Unit) {
    Row(
        Modifier
            .clip(CircleShape)
            .background(Yx.Surface2)
            .border(1.dp, Yx.Stroke, CircleShape)
            .clickable(onClick = onClick)
            .padding(start = 10.dp, end = 12.dp, top = 6.dp, bottom = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(Icons.AutoMirrored.Rounded.Sort, null, tint = Yx.Blue, modifier = Modifier.size(15.dp))
        Spacer(Modifier.width(6.dp))
        Text(label, color = Yx.Text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
fun CoverImage(patch: Patch, imageLoader: coil.ImageLoader, modifier: Modifier = Modifier) {
    AsyncImage(
        model = ImageRequest.Builder(LocalContext.current)
            .data(coverUrl(patch.titleId))
            .crossfade(250)
            .diskCacheKey(patch.titleId)
            .memoryCacheKey(patch.titleId)
            .build(),
        imageLoader = imageLoader,
        contentDescription = patch.name,
        contentScale = ContentScale.Crop,
        placeholder = ColorPainter(Yx.Surface3),
        error = painterResource(R.drawable.splash),
        modifier = modifier.background(Yx.Surface3)
    )
}

@Composable
fun GradientButton(
    text: String,
    icon: ImageVector?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    brush: Brush = Yx.RedGradient,
    enabled: Boolean = true,
    height: Dp = 54.dp
) {
    val src = remember { MutableInteractionSource() }
    val shape = RoundedCornerShape(18.dp)
    Row(
        modifier
            .height(height)
            .pressScale(src)
            .graphicsLayer { alpha = if (enabled) 1f else 0.45f }
            .clip(shape)
            .background(brush)
            .clickable(interactionSource = src, indication = LocalIndication.current, enabled = enabled, onClick = onClick)
            .padding(horizontal = 18.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center
    ) {
        if (icon != null) {
            Icon(icon, null, tint = Color.White, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(10.dp))
        }
        Text(text, color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun SoftButton(
    text: String,
    icon: ImageVector?,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    tint: Color = Yx.Text,
    background: Color = Yx.Surface2,
    border: Color = Yx.Stroke,
    enabled: Boolean = true,
    height: Dp = 50.dp
) {
    val src = remember { MutableInteractionSource() }
    val shape = RoundedCornerShape(16.dp)
    Row(
        modifier
            .height(height)
            .pressScale(src)
            .graphicsLayer { alpha = if (enabled) 1f else 0.5f }
            .clip(shape)
            .background(background)
            .border(1.dp, border, shape)
            .clickable(interactionSource = src, indication = LocalIndication.current, enabled = enabled, onClick = onClick)
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.Center
    ) {
        if (icon != null) {
            Icon(icon, null, tint = tint, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(8.dp))
        }
        Text(text, color = tint, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
fun SmoothProgressBar(progress: Float, modifier: Modifier = Modifier, brush: Brush = Yx.RedGradient) {
    val p by animateFloatAsState(progress.coerceIn(0f, 1f), tween(300), label = "bar")
    Box(modifier.height(8.dp).clip(CircleShape).background(Yx.Surface3)) {
        Box(Modifier.fillMaxHeight().fillMaxWidth(p).clip(CircleShape).background(brush))
    }
}

@Composable
fun EmptyState(icon: ImageVector, title: String, message: String, accent: Color = Yx.TextMuted, action: (@Composable () -> Unit)? = null) {
    Column(
        Modifier.fillMaxWidth().padding(horizontal = 32.dp, vertical = 48.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Box(
            Modifier
                .size(84.dp)
                .clip(CircleShape)
                .background(Brush.radialGradient(listOf(accent.copy(alpha = 0.22f), accent.copy(alpha = 0.04f))))
                .border(1.dp, accent.copy(alpha = 0.25f), CircleShape),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, null, tint = accent, modifier = Modifier.size(38.dp))
        }
        Spacer(Modifier.height(18.dp))
        Text(title, color = Yx.Text, fontSize = 17.sp, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center)
        Spacer(Modifier.height(6.dp))
        Text(message, color = Yx.TextMuted, fontSize = 13.sp, lineHeight = 19.sp, textAlign = TextAlign.Center)
        if (action != null) {
            Spacer(Modifier.height(20.dp))
            action()
        }
    }
}

@Composable
fun SectionTitle(title: String, trailing: (@Composable () -> Unit)? = null, modifier: Modifier = Modifier) {
    Row(modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Text(title, color = Yx.Text, fontSize = 18.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
        trailing?.invoke()
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  PATCH CARDS
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun PatchCard(patch: Patch, imageLoader: coil.ImageLoader, onClick: () -> Unit) {
    val src = remember { MutableInteractionSource() }
    val shape = RoundedCornerShape(20.dp)
    Column(
        Modifier
            .fillMaxWidth()
            .pressScale(src)
            .clip(shape)
            .background(Yx.Surface1)
            .border(1.dp, Yx.Stroke, shape)
            .clickable(interactionSource = src, indication = LocalIndication.current, onClick = onClick)
            .padding(6.dp)
    ) {
        Box {
            CoverImage(
                patch, imageLoader,
                Modifier.fillMaxWidth().aspectRatio(16f / 9f).clip(RoundedCornerShape(15.dp))
            )
            if (patch.isFavorited) {
                Box(
                    Modifier
                        .align(Alignment.TopStart)
                        .padding(6.dp)
                        .size(24.dp)
                        .clip(CircleShape)
                        .background(Yx.Gold),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(Icons.Rounded.Star, "Favori", tint = Color(0xFF2A2000), modifier = Modifier.size(15.dp))
                }
            }
            patch.status()?.let {
                Box(Modifier.align(Alignment.TopEnd).padding(6.dp)) { StatusBadge(it.label, it.color, it.icon) }
            }
        }
        Column(Modifier.padding(start = 6.dp, end = 6.dp, top = 9.dp, bottom = 6.dp)) {
            Text(
                patch.name,
                color = Yx.Text,
                fontSize = 13.sp,
                fontWeight = FontWeight.SemiBold,
                lineHeight = 17.sp,
                maxLines = 2,
                minLines = 2,
                overflow = TextOverflow.Ellipsis
            )
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(
                    patch.yapimci.ifBlank { "Bilinmiyor" },
                    color = Yx.TextMuted,
                    fontSize = 11.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.weight(1f)
                )
                if (patch.size.isNotBlank()) {
                    Spacer(Modifier.width(6.dp))
                    Text(patch.size, color = Yx.TextFaint, fontSize = 10.sp, fontWeight = FontWeight.Medium, maxLines = 1)
                }
            }
        }
    }
}

@Composable
fun SkeletonCard() {
    val inf = rememberInfiniteTransition(label = "sk")
    val a by inf.animateFloat(0.35f, 0.8f, infiniteRepeatable(tween(850), RepeatMode.Reverse), label = "ska")
    val shape = RoundedCornerShape(20.dp)
    Column(
        Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(Yx.Surface1)
            .border(1.dp, Yx.Stroke, shape)
            .padding(6.dp)
            .graphicsLayer { alpha = a }
    ) {
        Box(Modifier.fillMaxWidth().aspectRatio(16f / 9f).clip(RoundedCornerShape(15.dp)).background(Yx.Surface3))
        Spacer(Modifier.height(10.dp))
        Box(Modifier.padding(horizontal = 6.dp).fillMaxWidth(0.85f).height(11.dp).clip(CircleShape).background(Yx.Surface3))
        Spacer(Modifier.height(7.dp))
        Box(Modifier.padding(horizontal = 6.dp).fillMaxWidth(0.55f).height(11.dp).clip(CircleShape).background(Yx.Surface3))
        Spacer(Modifier.height(10.dp))
        Box(Modifier.padding(horizontal = 6.dp).fillMaxWidth(0.4f).height(9.dp).clip(CircleShape).background(Yx.Surface2))
        Spacer(Modifier.height(8.dp))
    }
}

@Composable
fun FeaturedCard(patch: Patch, imageLoader: coil.ImageLoader, width: Dp = 290.dp, onClick: () -> Unit) {
    val src = remember { MutableInteractionSource() }
    val shape = RoundedCornerShape(24.dp)
    Box(
        Modifier
            .width(width)
            .aspectRatio(16f / 9f)
            .pressScale(src)
            .clip(shape)
            .border(1.dp, Yx.StrokeHi, shape)
            .clickable(interactionSource = src, indication = LocalIndication.current, onClick = onClick)
    ) {
        CoverImage(patch, imageLoader, Modifier.fillMaxSize())
        Box(
            Modifier
                .fillMaxSize()
                .background(
                    Brush.verticalGradient(
                        0f to Color.Transparent,
                        0.45f to Color.Transparent,
                        1f to Color.Black.copy(alpha = 0.92f)
                    )
                )
        )
        Box(
            Modifier
                .align(Alignment.TopStart)
                .padding(12.dp)
                .clip(CircleShape)
                .background(Yx.RedGradient)
                .padding(horizontal = 10.dp, vertical = 4.dp)
        ) {
            Text("YENİ", color = Color.White, fontSize = 10.sp, fontWeight = FontWeight.ExtraBold, letterSpacing = 1.sp)
        }
        patch.status()?.let {
            Box(Modifier.align(Alignment.TopEnd).padding(12.dp)) { StatusBadge(it.label, it.color, it.icon) }
        }
        Column(Modifier.align(Alignment.BottomStart).padding(start = 16.dp, end = 16.dp, bottom = 14.dp)) {
            Text(
                patch.name, color = Color.White, fontSize = 16.sp, fontWeight = FontWeight.Bold,
                maxLines = 1, overflow = TextOverflow.Ellipsis
            )
            Spacer(Modifier.height(2.dp))
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Rounded.Translate, null, tint = Color.White.copy(alpha = 0.7f), modifier = Modifier.size(12.dp))
                Spacer(Modifier.width(5.dp))
                Text(
                    patch.yapimci.ifBlank { "Bilinmiyor" }, color = Color.White.copy(alpha = 0.75f), fontSize = 12.sp,
                    maxLines = 1, overflow = TextOverflow.Ellipsis
                )
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  DISCOVER (ALL PATCHES)
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun DiscoverScreen(viewModel: AppViewModel, onOpenGuide: () -> Unit) {
    var query by rememberSaveable { mutableStateOf("") }
    var filter by rememberSaveable { mutableStateOf(PatchFilter.ALL) }
    var selected by remember { mutableStateOf<Patch?>(null) }
    var showSort by remember { mutableStateOf(false) }
    val gridState = rememberLazyGridState()

    val patchesSize = viewModel.allPatches.size
    val sortMode = viewModel.currentSortMode.value
    val installedCount = viewModel.allPatches.count { it.isInstalled }
    val onDeviceCount = viewModel.allPatches.count { it.gameInstalled || it.isInstalled }

    val filtered = remember(patchesSize, query, sortMode, filter, installedCount, onDeviceCount) {
        val q = viewModel.normalizeForSearch(query)
        val currentFilter = filter
        val base = viewModel.allPatches.filter { patch ->
            when (currentFilter) {
                PatchFilter.ALL -> true
                PatchFilter.ON_DEVICE -> patch.gameInstalled || patch.isInstalled
                PatchFilter.INSTALLED -> patch.isInstalled
            }
        }
        val searched = if (q.isBlank()) base else base.filter { viewModel.normalizeForSearch(it.name).contains(q) }
        viewModel.sortPatches(searched, sortMode)
    }
    val featured = remember(patchesSize) {
        viewModel.allPatches.sortedByDescending { it.originalIndex }.take(10)
    }

    val loading = viewModel.isLoading.value
    val error = viewModel.errorMessage.value
    val showFeatured = !loading && error == null && query.isBlank() && filter == PatchFilter.ALL && featured.isNotEmpty()
    val sectionIndex = if (showFeatured) 4 else 3

    LaunchedEffect(Unit) {
        viewModel.scrollToTopEvents.collect { target ->
            if (target == "discover") gridState.scrollToItem(0)
        }
    }

    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    val isCompactLandscape = isLandscape && configuration.screenHeightDp < 540

    val screenWidthDp = configuration.screenWidthDp
    val gridColumns = when {
        screenWidthDp >= 1100 -> 4
        screenWidthDp >= 650 -> 3
        else -> 2
    }

    LazyVerticalGrid(
        columns = GridCells.Fixed(gridColumns),
        state = gridState,
        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = LocalBottomPad.current),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.fillMaxSize()
    ) {
        if (isCompactLandscape) {
            fullItem("compact_top_bar") {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = 8.dp, bottom = 4.dp)
                ) {
                    Image(
                        painterResource(R.drawable.logo_wide), "YamaNX",
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.height(34.dp)
                    )
                    Spacer(Modifier.width(10.dp))
                    SearchField(query, { query = it }, "Oyun ara…", Modifier.weight(1f), height = 44.dp)
                    Spacer(Modifier.width(8.dp))
                    val src = remember { MutableInteractionSource() }
                    Box(
                        Modifier
                            .size(44.dp)
                            .pressScale(src, 0.92f)
                            .clip(RoundedCornerShape(14.dp))
                            .background(Yx.RedGradient)
                            .clickable(interactionSource = src, indication = LocalIndication.current) { showSort = true },
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Rounded.Tune, "Sırala", tint = Color.White, modifier = Modifier.size(20.dp))
                    }
                    Spacer(Modifier.width(8.dp))
                    CircleIconButton(Icons.AutoMirrored.Rounded.HelpOutline, "Nasıl Kurulur", onOpenGuide, size = 42.dp)
                }
            }
        } else {
            // 0 — Brand header
            fullItem("header") {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    modifier = Modifier
                        .then(if (!isLandscape) Modifier.statusBarsPadding() else Modifier)
                        .padding(top = 10.dp, bottom = 6.dp)
                        .fillMaxWidth()
                ) {
                    Image(
                        painterResource(R.drawable.logo_wide), "YamaNX",
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.height(68.dp)
                    )
                    Spacer(Modifier.weight(1f))
                    CircleIconButton(Icons.AutoMirrored.Rounded.HelpOutline, "Nasıl Kurulur", onOpenGuide)
                }
            }

            // 1 — Search + sort
            fullItem("search") {
                Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(top = 6.dp)) {
                    SearchField(query, { query = it }, "Oyun ara… (örn: Zelda)", Modifier.weight(1f))
                    Spacer(Modifier.width(10.dp))
                    val src = remember { MutableInteractionSource() }
                    Box(
                        Modifier
                            .size(54.dp)
                            .pressScale(src, 0.92f)
                            .clip(RoundedCornerShape(18.dp))
                            .background(Yx.RedGradient)
                            .clickable(interactionSource = src, indication = LocalIndication.current) { showSort = true },
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Rounded.Tune, "Sırala", tint = Color.White, modifier = Modifier.size(22.dp))
                    }
                }
            }
        }

        // 2 — Filter chips
        fullItem("chips") {
            LazyRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                contentPadding = PaddingValues(horizontal = 16.dp),
                modifier = Modifier.bleedHorizontal(16.dp)
            ) {
                item { FilterChipPill("Tümü", patchesSize, filter == PatchFilter.ALL, { filter = PatchFilter.ALL }) }
                item { FilterChipPill("Cihazımda", onDeviceCount, filter == PatchFilter.ON_DEVICE, { filter = PatchFilter.ON_DEVICE }, Yx.Amber.copy(alpha = 0.9f)) }
                item { FilterChipPill("Yüklü", installedCount, filter == PatchFilter.INSTALLED, { filter = PatchFilter.INSTALLED }, Color(0xFF16A36A)) }
            }
        }

        // 3 — Featured carousel
        if (showFeatured) {
            fullItem("featured") {
                Column(Modifier.padding(top = 6.dp)) {
                    SectionTitle("Yeni Eklenenler", trailing = {
                        Icon(Icons.Rounded.AutoAwesome, null, tint = Yx.Gold, modifier = Modifier.size(18.dp))
                    })
                    Spacer(Modifier.height(12.dp))
                    LazyRow(
                        horizontalArrangement = Arrangement.spacedBy(12.dp),
                        contentPadding = PaddingValues(horizontal = 16.dp),
                        modifier = Modifier.bleedHorizontal(16.dp)
                    ) {
                        items(featured, key = { "f_" + it.titleId }) { p ->
                            FeaturedCard(p, viewModel.imageLoader, width = if (isCompactLandscape) 220.dp else 290.dp) { selected = p }
                        }
                    }
                }
            }
        }

        // Section header
        fullItem("section") {
            SectionTitle(
                when (filter) {
                    PatchFilter.ALL -> if (query.isBlank()) "Tüm Yamalar" else "Arama Sonuçları"
                    PatchFilter.ON_DEVICE -> "Cihazımdaki Oyunlar"
                    PatchFilter.INSTALLED -> "Yüklü Yamalar"
                },
                trailing = { SortPill(sortMode.shortLabel) { showSort = true } },
                modifier = Modifier.padding(top = 10.dp)
            )
        }

        when {
            loading -> items(8, key = { "sk$it" }) { SkeletonCard() }
            error != null -> fullItem("error") {
                EmptyState(
                    Icons.Rounded.WifiOff, "Bağlantı sorunu", error, Yx.RedBright
                ) {
                    GradientButton("Tekrar Dene", Icons.Rounded.Refresh, { viewModel.fetchPatches() }, Modifier.width(200.dp))
                }
            }
            filtered.isEmpty() -> fullItem("empty") {
                when {
                    query.isNotBlank() -> EmptyState(Icons.Rounded.SearchOff, "Sonuç bulunamadı", "\"$query\" için eşleşen bir yama yok. Farklı bir isim deneyin.")
                    filter == PatchFilter.INSTALLED -> EmptyState(Icons.Rounded.Inventory2, "Henüz yüklü yama yok", "Bir oyun seçip \"İndir ve Kur\" diyerek ilk yamanı yükleyebilirsin.")
                    else -> EmptyState(Icons.Rounded.SportsEsports, "Cihazda oyun bulunamadı", "Yüklü sekmesinden oyun klasörünü seçerek cihazındaki oyunları eşleştirebilirsin.", Yx.Amber)
                }
            }
            else -> items(filtered, key = { it.titleId }) { p ->
                PatchCard(p, viewModel.imageLoader) { selected = p }
            }
        }
    }

    if (showSort) SortSheet(viewModel) { showSort = false }
    selected?.let { PatchDetailSheet(it, viewModel) { selected = null } }
}

private fun LazyGridScope.fullItem(key: String, content: @Composable () -> Unit) {
    item(key = key, span = { GridItemSpan(maxLineSpan) }) { content() }
}

// ═════════════════════════════════════════════════════════════════════════════
//  FAVORITES
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun FavoritesScreen(viewModel: AppViewModel) {
    var query by rememberSaveable { mutableStateOf("") }
    var selected by remember { mutableStateOf<Patch?>(null) }
    var showSort by remember { mutableStateOf(false) }
    val gridState = rememberLazyGridState()

    val patchesSize = viewModel.allPatches.size
    val favSize = viewModel.favorites.size
    val sortMode = viewModel.currentSortMode.value
    val list = remember(patchesSize, favSize, query, sortMode) {
        val q = viewModel.normalizeForSearch(query)
        val base = viewModel.allPatches.filter { it.isFavorited }
        val searched = if (q.isBlank()) base else base.filter { viewModel.normalizeForSearch(it.name).contains(q) }
        viewModel.sortPatches(searched, sortMode)
    }

    LaunchedEffect(Unit) {
        viewModel.scrollToTopEvents.collect { target ->
            if (target == "favorites") gridState.scrollToItem(0)
        }
    }

    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE
    val isCompactLandscape = isLandscape && configuration.screenHeightDp < 540

    val screenWidthDp = configuration.screenWidthDp
    val gridColumns = when {
        screenWidthDp >= 1100 -> 4
        screenWidthDp >= 650 -> 3
        else -> 2
    }

    LazyVerticalGrid(
        columns = GridCells.Fixed(gridColumns),
        state = gridState,
        contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = LocalBottomPad.current),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp),
        modifier = Modifier.fillMaxSize()
    ) {
        fullItem("header") {
            Box(Modifier.bleedHorizontal(16.dp)) {
                ScreenHeader(
                    title = "Favoriler",
                    overline = "Koleksiyonun",
                    subtitle = if (favSize == 0) "Sevdiğin yamaları burada topla" else "$favSize favori yama"
                )
            }
        }
        if (favSize > 0) {
            fullItem("search") {
                SearchField(
                    query, { query = it }, "Favorilerde ara…", Modifier.fillMaxWidth(),
                    height = if (isCompactLandscape) 44.dp else 54.dp
                )
            }
            fullItem("section") {
                SectionTitle(
                    if (query.isBlank()) "Tümü" else "Arama Sonuçları",
                    trailing = { SortPill(sortMode.shortLabel) { showSort = true } },
                    modifier = Modifier.padding(top = 6.dp)
                )
            }
        }
        if (list.isEmpty()) {
            fullItem("empty") {
                if (favSize == 0) EmptyState(
                    Icons.Rounded.Star, "Henüz favorin yok",
                    "Bir yamanın detayını açıp yıldıza dokunarak favorilerine ekleyebilirsin.", Yx.Gold
                ) else EmptyState(Icons.Rounded.SearchOff, "Sonuç bulunamadı", "Favorilerinde \"$query\" ile eşleşen yama yok.")
            }
        } else {
            items(list, key = { it.titleId }) { p -> PatchCard(p, viewModel.imageLoader) { selected = p } }
        }
    }

    if (showSort) SortSheet(viewModel) { showSort = false }
    selected?.let { PatchDetailSheet(it, viewModel) { selected = null } }
}

// ═════════════════════════════════════════════════════════════════════════════
//  SORT SHEET
// ═════════════════════════════════════════════════════════════════════════════
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SortSheet(viewModel: AppViewModel, onDismiss: () -> Unit) {
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scope = rememberCoroutineScope()
    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = state,
        containerColor = Yx.Surface1,
        scrimColor = Color.Black.copy(alpha = 0.6f),
        shape = RoundedCornerShape(topStart = 30.dp, topEnd = 30.dp),
        dragHandle = { SheetHandle() }
    ) {
        Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.TopCenter) {
            Column(
                Modifier
                    .fillMaxWidth()
                    .widthIn(max = 540.dp)
                    .padding(horizontal = 20.dp)
                    .padding(bottom = 20.dp)
            ) {
                Text("Sıralama", color = Yx.Text, fontSize = 22.sp, fontWeight = FontWeight.ExtraBold)
                Spacer(Modifier.height(2.dp))
                Text("Yamaların nasıl listeleneceğini seç", color = Yx.TextMuted, fontSize = 13.sp)
                Spacer(Modifier.height(18.dp))
                SortMode.entries.forEach { mode ->
                    val current = viewModel.currentSortMode.value == mode
                    val icon = when (mode.ordinal) {
                        0, 1 -> Icons.Rounded.SortByAlpha
                        2 -> Icons.Rounded.NewReleases
                        3 -> Icons.Rounded.History
                        else -> Icons.Rounded.Storage
                    }
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .padding(vertical = 4.dp)
                            .clip(RoundedCornerShape(18.dp))
                            .background(if (current) Yx.Red.copy(alpha = 0.12f) else Yx.Surface2)
                            .border(1.dp, if (current) Yx.Red.copy(alpha = 0.55f) else Yx.Stroke, RoundedCornerShape(18.dp))
                            .clickable {
                                viewModel.setSortMode(mode)
                                scope.launch { state.hide() }.invokeOnCompletion { onDismiss() }
                            }
                            .padding(horizontal = 14.dp, vertical = 14.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Box(
                            Modifier
                                .size(38.dp)
                                .clip(RoundedCornerShape(12.dp))
                                .background(if (current) Yx.Red.copy(alpha = 0.22f) else Yx.Surface3),
                            contentAlignment = Alignment.Center
                        ) {
                            Icon(icon, null, tint = if (current) Yx.RedBright else Yx.TextMuted, modifier = Modifier.size(20.dp))
                        }
                        Spacer(Modifier.width(14.dp))
                        Text(
                            mode.label,
                            color = if (current) Yx.Text else Yx.TextMuted,
                            fontSize = 15.sp,
                            fontWeight = if (current) FontWeight.Bold else FontWeight.Medium,
                            modifier = Modifier.weight(1f)
                        )
                        if (current) {
                            Box(
                                Modifier.size(26.dp).clip(CircleShape).background(Yx.Green),
                                contentAlignment = Alignment.Center
                            ) {
                                Icon(Icons.Rounded.Check, "Seçili", tint = Color(0xFF00321F), modifier = Modifier.size(17.dp))
                            }
                        } else {
                            Box(Modifier.size(26.dp).clip(CircleShape).border(2.dp, Yx.Surface3, CircleShape))
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun SheetHandle() {
    Box(
        Modifier
            .padding(top = 12.dp, bottom = 14.dp)
            .width(42.dp)
            .height(5.dp)
            .clip(CircleShape)
            .background(Yx.Surface3)
    )
}

// ═════════════════════════════════════════════════════════════════════════════
//  PATCH DETAIL SHEET
// ═════════════════════════════════════════════════════════════════════════════
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PatchDetailSheet(patch: Patch, viewModel: AppViewModel, onDismiss: () -> Unit) {
    val state = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    val scope = rememberCoroutineScope()
    var confirmRemove by remember { mutableStateOf(false) }
    fun close(after: () -> Unit = {}) {
        scope.launch { state.hide() }.invokeOnCompletion { onDismiss(); after() }
    }

    val configuration = LocalConfiguration.current
    val isLandscape = configuration.orientation == Configuration.ORIENTATION_LANDSCAPE

    ModalBottomSheet(
        onDismissRequest = onDismiss,
        sheetState = state,
        containerColor = Yx.Surface1,
        scrimColor = Color.Black.copy(alpha = 0.65f),
        shape = RoundedCornerShape(topStart = 28.dp, topEnd = 28.dp),
        dragHandle = { SheetHandle() }
    ) {
        Box(
            modifier = Modifier.fillMaxWidth(),
            contentAlignment = Alignment.TopCenter
        ) {
            if (isLandscape) {
                // Yatay ekran: 2 sütunlu kompakt düzen (Geniş ekranda devasa büyümesi engellendi)
                Row(
                    Modifier
                        .fillMaxWidth()
                        .widthIn(max = 760.dp)
                        .verticalScroll(rememberScrollState())
                        .padding(horizontal = 24.dp)
                        .padding(bottom = 20.dp),
                    horizontalArrangement = Arrangement.spacedBy(20.dp),
                    verticalAlignment = Alignment.Top
                ) {
                    // Sol Sütun: Kapak görseli ve Title ID
                    Column(
                        Modifier.weight(0.44f),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Box(
                            Modifier
                                .fillMaxWidth()
                                .aspectRatio(16f / 9f)
                                .clip(RoundedCornerShape(18.dp))
                                .border(1.dp, Yx.StrokeHi, RoundedCornerShape(18.dp))
                        ) {
                            CoverImage(patch, viewModel.imageLoader, Modifier.fillMaxSize())
                            Box(
                                Modifier.fillMaxSize().background(
                                    Brush.verticalGradient(0.55f to Color.Transparent, 1f to Color.Black.copy(alpha = 0.6f))
                                )
                            )
                            patch.status()?.let {
                                Box(Modifier.align(Alignment.BottomStart).padding(10.dp)) {
                                    StatusBadge(it.label, it.color, it.icon)
                                }
                            }
                        }

                        Spacer(Modifier.height(8.dp))
                        Row(
                            Modifier
                                .fillMaxWidth()
                                .clip(RoundedCornerShape(12.dp))
                                .background(Yx.Surface2)
                                .padding(horizontal = 12.dp, vertical = 7.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Title ID", color = Yx.TextFaint, fontSize = 11.sp, modifier = Modifier.weight(1f))
                            Text(patch.titleId, color = Yx.TextMuted, fontSize = 11.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Medium)
                        }
                    }

                    // Sağ Sütun: Başlık, Çevirmen, Bilgiler ve Butonlar
                    Column(
                        Modifier.weight(0.56f)
                    ) {
                        Text(
                            patch.name,
                            color = Yx.Text,
                            fontSize = 18.sp,
                            fontWeight = FontWeight.ExtraBold,
                            lineHeight = 23.sp,
                            maxLines = 2,
                            overflow = TextOverflow.Ellipsis
                        )
                        Spacer(Modifier.height(4.dp))
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(Icons.Rounded.Translate, null, tint = Yx.RedBright, modifier = Modifier.size(14.dp))
                            Spacer(Modifier.width(6.dp))
                            Text("Çeviri: ", color = Yx.TextMuted, fontSize = 12.sp)
                            Text(patch.yapimci.ifBlank { "Belirtilmemiş" }, color = Yx.Text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                        }

                        Spacer(Modifier.height(10.dp))
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            InfoTile(Icons.Rounded.Storage, "Boyut", patch.size.ifBlank { "—" }, Yx.Blue, Modifier.weight(1f))
                            InfoTile(Icons.Rounded.NewReleases, "Sürüm", patch.versionText(), Yx.Purple, Modifier.weight(1f))
                            val st = patch.status()
                            InfoTile(
                                st?.icon ?: Icons.Rounded.CloudDownload, "Durum", st?.label ?: "İndirilmedi",
                                st?.color ?: Yx.TextMuted, Modifier.weight(1f)
                            )
                        }

                        Spacer(Modifier.height(14.dp))

                        AnimatedContent(targetState = confirmRemove && patch.isInstalled, label = "actions_landscape") { confirming ->
                            if (confirming) {
                                Column {
                                    Text(
                                        "Bu yamayı cihazından kaldırmak istediğine emin misin?",
                                        color = Yx.Text, fontSize = 13.sp, textAlign = TextAlign.Center,
                                        modifier = Modifier.fillMaxWidth()
                                    )
                                    Spacer(Modifier.height(10.dp))
                                    Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                        SoftButton("Vazgeç", null, { confirmRemove = false }, Modifier.weight(1f), height = 48.dp)
                                        GradientButton(
                                            "Evet, Kaldır", Icons.Rounded.Delete,
                                            { viewModel.removePatch(patch); close() },
                                            Modifier.weight(1f),
                                            height = 48.dp
                                        )
                                    }
                                }
                            } else {
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                    val fav = patch.isFavorited
                                    val favSrc = remember { MutableInteractionSource() }
                                    Box(
                                        Modifier
                                            .size(48.dp)
                                            .pressScale(favSrc, 0.88f)
                                            .clip(RoundedCornerShape(16.dp))
                                            .background(if (fav) Yx.Gold.copy(alpha = 0.16f) else Yx.Surface2)
                                            .border(1.dp, if (fav) Yx.Gold.copy(alpha = 0.7f) else Yx.Stroke, RoundedCornerShape(16.dp))
                                            .clickable(interactionSource = favSrc, indication = LocalIndication.current) { viewModel.toggleFavorite(patch) },
                                        contentAlignment = Alignment.Center
                                    ) {
                                        Icon(
                                            if (fav) Icons.Rounded.Star else Icons.Rounded.StarOutline,
                                            if (fav) "Favorilerden çıkar" else "Favorilere ekle",
                                            tint = if (fav) Yx.Gold else Yx.Text,
                                            modifier = Modifier.size(22.dp)
                                        )
                                    }
                                    Spacer(Modifier.width(8.dp))
                                    if (patch.isInstalled) {
                                        SoftButton(
                                            "Yamayı Kaldır", Icons.Rounded.Delete, { confirmRemove = true },
                                            Modifier.weight(1f),
                                            tint = Yx.RedBright, background = Yx.Red.copy(alpha = 0.10f), border = Yx.Red.copy(alpha = 0.45f),
                                            height = 48.dp
                                        )
                                    } else {
                                        GradientButton(
                                            if (viewModel.isDownloading.value) "Başka indirme sürüyor…" else "İndir ve Kur",
                                            Icons.Rounded.Download,
                                            {
                                                if (!viewModel.isDownloading.value) {
                                                    viewModel.downloadAndInstallPatch(patch)
                                                    close()
                                                }
                                            },
                                            Modifier.weight(1f),
                                            height = 48.dp,
                                            enabled = !viewModel.isDownloading.value
                                        )
                                    }
                                }
                            }
                        }
                    }
                }
            } else {
                // Dikey ekran düzeni
                Column(
                    Modifier
                        .fillMaxWidth()
                        .widthIn(max = 560.dp)
                        .verticalScroll(rememberScrollState())
                        .padding(horizontal = 20.dp)
                        .padding(bottom = 24.dp)
                ) {
                    // Hero
                    Box(
                        Modifier
                            .fillMaxWidth()
                            .aspectRatio(16f / 9f)
                            .clip(RoundedCornerShape(24.dp))
                            .border(1.dp, Yx.StrokeHi, RoundedCornerShape(24.dp))
                    ) {
                        CoverImage(patch, viewModel.imageLoader, Modifier.fillMaxSize())
                        Box(
                            Modifier.fillMaxSize().background(
                                Brush.verticalGradient(0.55f to Color.Transparent, 1f to Color.Black.copy(alpha = 0.6f))
                            )
                        )
                        patch.status()?.let {
                            Box(Modifier.align(Alignment.BottomStart).padding(12.dp)) { StatusBadge(it.label, it.color, it.icon) }
                        }
                    }

                    Spacer(Modifier.height(18.dp))
                    Text(patch.name, color = Yx.Text, fontSize = 22.sp, fontWeight = FontWeight.ExtraBold, lineHeight = 27.sp)
                    Spacer(Modifier.height(6.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Icon(Icons.Rounded.Translate, null, tint = Yx.RedBright, modifier = Modifier.size(15.dp))
                        Spacer(Modifier.width(6.dp))
                        Text("Çeviri: ", color = Yx.TextMuted, fontSize = 13.sp)
                        Text(patch.yapimci.ifBlank { "Belirtilmemiş" }, color = Yx.Text, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                    }

                    Spacer(Modifier.height(18.dp))
                    Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                        InfoTile(Icons.Rounded.Storage, "Boyut", patch.size.ifBlank { "—" }, Yx.Blue, Modifier.weight(1f))
                        InfoTile(Icons.Rounded.NewReleases, "Sürüm", patch.versionText(), Yx.Purple, Modifier.weight(1f))
                        val st = patch.status()
                        InfoTile(
                            st?.icon ?: Icons.Rounded.CloudDownload, "Durum", st?.label ?: "İndirilmedi",
                            st?.color ?: Yx.TextMuted, Modifier.weight(1f)
                        )
                    }
                    Spacer(Modifier.height(10.dp))
                    Row(
                        Modifier
                            .fillMaxWidth()
                            .clip(RoundedCornerShape(14.dp))
                            .background(Yx.Surface2)
                            .padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("Title ID", color = Yx.TextFaint, fontSize = 12.sp, modifier = Modifier.weight(1f))
                        Text(patch.titleId, color = Yx.TextMuted, fontSize = 12.sp, fontFamily = FontFamily.Monospace, fontWeight = FontWeight.Medium)
                    }

                    Spacer(Modifier.height(22.dp))

                    AnimatedContent(targetState = confirmRemove && patch.isInstalled, label = "actions") { confirming ->
                        if (confirming) {
                            Column {
                                Text(
                                    "Bu yamayı cihazından kaldırmak istediğine emin misin?",
                                    color = Yx.Text, fontSize = 14.sp, textAlign = TextAlign.Center,
                                    modifier = Modifier.fillMaxWidth()
                                )
                                Spacer(Modifier.height(14.dp))
                                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                                    SoftButton("Vazgeç", null, { confirmRemove = false }, Modifier.weight(1f), height = 54.dp)
                                    GradientButton(
                                        "Evet, Kaldır", Icons.Rounded.Delete,
                                        { viewModel.removePatch(patch); close() },
                                        Modifier.weight(1f)
                                    )
                                }
                            }
                        } else {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                val fav = patch.isFavorited
                                val favSrc = remember { MutableInteractionSource() }
                                Box(
                                    Modifier
                                        .size(54.dp)
                                        .pressScale(favSrc, 0.88f)
                                        .clip(RoundedCornerShape(18.dp))
                                        .background(if (fav) Yx.Gold.copy(alpha = 0.16f) else Yx.Surface2)
                                        .border(1.dp, if (fav) Yx.Gold.copy(alpha = 0.7f) else Yx.Stroke, RoundedCornerShape(18.dp))
                                        .clickable(interactionSource = favSrc, indication = LocalIndication.current) { viewModel.toggleFavorite(patch) },
                                    contentAlignment = Alignment.Center
                                ) {
                                    Icon(
                                        if (fav) Icons.Rounded.Star else Icons.Rounded.StarOutline,
                                        if (fav) "Favorilerden çıkar" else "Favorilere ekle",
                                        tint = if (fav) Yx.Gold else Yx.Text,
                                        modifier = Modifier.size(24.dp)
                                    )
                                }
                                Spacer(Modifier.width(10.dp))
                                if (patch.isInstalled) {
                                    SoftButton(
                                        "Yamayı Kaldır", Icons.Rounded.Delete, { confirmRemove = true },
                                        Modifier.weight(1f),
                                        tint = Yx.RedBright, background = Yx.Red.copy(alpha = 0.10f), border = Yx.Red.copy(alpha = 0.45f),
                                        height = 54.dp
                                    )
                                } else {
                                    GradientButton(
                                        if (viewModel.isDownloading.value) "Başka indirme sürüyor…" else "İndir ve Kur",
                                        Icons.Rounded.Download,
                                        {
                                            if (!viewModel.isDownloading.value) {
                                                viewModel.downloadAndInstallPatch(patch)
                                                close()
                                            }
                                        },
                                        Modifier.weight(1f),
                                        enabled = !viewModel.isDownloading.value
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun InfoTile(icon: ImageVector, label: String, value: String, accent: Color, modifier: Modifier = Modifier) {
    Column(
        modifier
            .clip(RoundedCornerShape(18.dp))
            .background(Yx.Surface2)
            .border(1.dp, Yx.Stroke, RoundedCornerShape(18.dp))
            .padding(12.dp)
    ) {
        Box(
            Modifier.size(30.dp).clip(RoundedCornerShape(10.dp)).background(accent.copy(alpha = 0.15f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, null, tint = accent, modifier = Modifier.size(17.dp))
        }
        Spacer(Modifier.height(10.dp))
        Text(label, color = Yx.TextFaint, fontSize = 11.sp)
        Spacer(Modifier.height(1.dp))
        Text(value, color = Yx.Text, fontSize = 13.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  INSTALLED
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun InstalledScreen(viewModel: AppViewModel, onOpenGuide: () -> Unit) {
    val context = LocalContext.current
    var selected by remember { mutableStateOf<Patch?>(null) }
    val listState = rememberLazyListState()

    LaunchedEffect(Unit) {
        viewModel.scrollToTopEvents.collect { target ->
            if (target == "installed") listState.scrollToItem(0)
        }
    }

    val folderLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            try {
                context.contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                )
            } catch (_: Exception) {}
            viewModel.setGameFolderUri(uri)
        }
    }

    val hasFolder = viewModel.gameFolderUri.value != null
    val installed = viewModel.allPatches.filter { it.isInstalled }
    val onDevice = viewModel.allPatches.filter { it.gameInstalled && !it.isInstalled }

    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxWidth().widthIn(max = 860.dp),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = LocalBottomPad.current),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
        item {
            Box(Modifier.bleedHorizontal(16.dp)) {
                ScreenHeader(title = "Yüklü", overline = "Cihazın", subtitle = "Yüklü yamalar ve cihazındaki oyunlar")
            }
        }

        // Stats
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                StatTile("${installed.size}", "Yüklü yama", Icons.Rounded.CheckCircle, Yx.Green, Modifier.weight(1f))
                StatTile("${onDevice.size}", "Yaması bekleyen", Icons.Rounded.SportsEsports, Yx.Amber, Modifier.weight(1f))
            }
        }

        // Folder card
        item {
            if (!hasFolder) {
                Column(
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(26.dp))
                        .background(Brush.linearGradient(listOf(Color(0xFF3A0A12), Yx.Surface1)))
                        .border(1.dp, Yx.Red.copy(alpha = 0.35f), RoundedCornerShape(26.dp))
                        .padding(20.dp)
                ) {
                    Box(
                        Modifier.size(48.dp).clip(RoundedCornerShape(16.dp)).background(Yx.RedGradient),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Rounded.FolderOpen, null, tint = Color.White, modifier = Modifier.size(26.dp))
                    }
                    Spacer(Modifier.height(14.dp))
                    Text("Oyun klasörünü seç", color = Yx.Text, fontSize = 18.sp, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "Cihazındaki oyunları tarayıp yaması olanları otomatik olarak burada gösterelim.",
                        color = Yx.TextMuted, fontSize = 13.sp, lineHeight = 19.sp
                    )
                    Spacer(Modifier.height(16.dp))
                    GradientButton("Klasör Seç", Icons.Rounded.Folder, { folderLauncher.launch(null) }, Modifier.fillMaxWidth())
                }
            } else {
                Row(
                    Modifier
                        .fillMaxWidth()
                        .clip(RoundedCornerShape(22.dp))
                        .background(Yx.Surface1)
                        .border(1.dp, Yx.Green.copy(alpha = 0.3f), RoundedCornerShape(22.dp))
                        .padding(14.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Box(
                        Modifier.size(44.dp).clip(RoundedCornerShape(14.dp)).background(Yx.Green.copy(alpha = 0.14f)),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Rounded.Folder, null, tint = Yx.Green, modifier = Modifier.size(22.dp))
                    }
                    Spacer(Modifier.width(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text("Oyun klasörü bağlı", color = Yx.Text, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                        Text("Oyunlar otomatik eşleştiriliyor", color = Yx.TextMuted, fontSize = 12.sp)
                    }
                    SoftButton("Değiştir", null, { folderLauncher.launch(null) }, height = 38.dp)
                }
            }
        }

        // Guide banner
        item {
            Row(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(22.dp))
                    .background(Yx.Surface1)
                    .border(1.dp, Yx.Stroke, RoundedCornerShape(22.dp))
                    .clickable(onClick = onOpenGuide)
                    .padding(14.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Box(
                    Modifier.size(44.dp).clip(RoundedCornerShape(14.dp)).background(Yx.Blue.copy(alpha = 0.14f)),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(Icons.AutoMirrored.Rounded.HelpOutline, null, tint = Yx.Blue, modifier = Modifier.size(22.dp))
                }
                Spacer(Modifier.width(12.dp))
                Column(Modifier.weight(1f)) {
                    Text("Yama nasıl kurulur?", color = Yx.Text, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                    Text("Eden emülatöründe adım adım kurulum", color = Yx.TextMuted, fontSize = 12.sp)
                }
                Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, null, tint = Yx.TextFaint)
            }
        }

        if (installed.isEmpty() && onDevice.isEmpty()) {
            item {
                EmptyState(
                    Icons.Rounded.Inventory2, "Burası şimdilik boş",
                    if (hasFolder) "Yaması bulunan bir oyun tespit edilemedi. Yamalar sekmesinden yama indirebilirsin."
                    else "Oyun klasörünü seçtiğinde ve yama yüklediğinde içerikler burada listelenecek."
                )
            }
        }

        if (installed.isNotEmpty()) {
            item { SectionTitle("Yüklü Yamalar", modifier = Modifier.padding(top = 8.dp)) }
            items(installed, key = { "i_" + it.titleId }) { p ->
                InstalledRow(p, viewModel.imageLoader) { selected = p }
            }
        }
        if (onDevice.isNotEmpty()) {
            item { SectionTitle("Yaması Kurulabilir", modifier = Modifier.padding(top = 8.dp)) }
            items(onDevice, key = { "d_" + it.titleId }) { p ->
                InstalledRow(p, viewModel.imageLoader) { selected = p }
            }
        }
    }
    }

    selected?.let { PatchDetailSheet(it, viewModel) { selected = null } }
}

@Composable
fun StatTile(value: String, label: String, icon: ImageVector, accent: Color, modifier: Modifier = Modifier) {
    Row(
        modifier
            .clip(RoundedCornerShape(22.dp))
            .background(Yx.Surface1)
            .border(1.dp, Yx.Stroke, RoundedCornerShape(22.dp))
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(value, color = Yx.Text, fontSize = 26.sp, fontWeight = FontWeight.ExtraBold)
            Text(label, color = Yx.TextMuted, fontSize = 12.sp)
        }
        Box(
            Modifier.size(38.dp).clip(CircleShape).background(accent.copy(alpha = 0.14f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, null, tint = accent, modifier = Modifier.size(20.dp))
        }
    }
}

@Composable
fun InstalledRow(patch: Patch, imageLoader: coil.ImageLoader, onClick: () -> Unit) {
    val src = remember { MutableInteractionSource() }
    Row(
        Modifier
            .fillMaxWidth()
            .pressScale(src, 0.98f)
            .clip(RoundedCornerShape(20.dp))
            .background(Yx.Surface1)
            .border(1.dp, Yx.Stroke, RoundedCornerShape(20.dp))
            .clickable(interactionSource = src, indication = LocalIndication.current, onClick = onClick)
            .padding(8.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        CoverImage(patch, imageLoader, Modifier.width(96.dp).aspectRatio(16f / 9f).clip(RoundedCornerShape(14.dp)))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(patch.name, color = Yx.Text, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(2.dp))
            Text(patch.yapimci.ifBlank { "Bilinmiyor" }, color = Yx.TextMuted, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Spacer(Modifier.height(6.dp))
            patch.status()?.let { st ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(7.dp).clip(CircleShape).background(st.color))
                    Spacer(Modifier.width(6.dp))
                    Text(
                        if (patch.isInstalled) "Yama yüklü" else "Oyun cihazda • yama kurulmadı",
                        color = st.color, fontSize = 11.sp, fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }
        Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, null, tint = Yx.TextFaint)
        Spacer(Modifier.width(4.dp))
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  GUIDE
// ═════════════════════════════════════════════════════════════════════════════
private val guideSteps = listOf(
    "Oyun Klasörünü Seçin" to "Alttaki menüden 'Yüklü' sekmesine gidin ve cihazınızdaki Oyun Klasörünü seçin.",
    "Yamayı İndirin" to "'Yamalar' veya 'Yüklü' sekmesinden oyunun üstüne dokunup 'İndir ve Kur' butonuna basın.",
    "Eden Emülatörüne Geçin" to "Eden emülatörünü açın. Yamasını kurduğunuz oyunun üstüne basılı tutun ve 'Add-ons' seçeneğine dokunun.",
    "Mod Kurulum Menüsü" to "Açılan pencerede 'Install' butonuna, ardından 'Mods and cheats' seçeneğine dokunun.",
    "Yamayı Seçin" to "Cihazınızın ana dosya dizininden 'YamaNX / [Oyun Adı] / [Title ID]' klasörünü bulun ve içine girin.",
    "Kurulumu Tamamlayın" to "İçeride 'romfs' klasörünü gördüğünüzde, alttaki 'Bu klasörü kullan' butonuna basarak işlemi tamamlayın.",
)

@Composable
fun GuideScreen(onBack: () -> Unit) {
    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(
            Modifier.fillMaxWidth().widthIn(max = 860.dp),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = LocalBottomPad.current)
        ) {
            item {
                Box(Modifier.bleedHorizontal(16.dp)) {
                    ScreenHeader(title = "Nasıl Kurulur", overline = "Rehber", subtitle = "6 adımda yamanı kur", onBack = onBack)
                }
                Spacer(Modifier.height(8.dp))
            }
            itemsIndexedSteps()
        }
    }
}

private fun androidx.compose.foundation.lazy.LazyListScope.itemsIndexedSteps() {
    guideSteps.forEachIndexed { i, (title, desc) ->
        item(key = "step$i") {
            val last = i == guideSteps.lastIndex
            Row(Modifier.fillMaxWidth().height(IntrinsicSize.Min)) {
                Column(Modifier.width(44.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Box(
                        Modifier
                            .size(36.dp)
                            .clip(CircleShape)
                            .background(if (last) Brush.linearGradient(listOf(Yx.Green, Color(0xFF10B981))) else Yx.RedGradient),
                        contentAlignment = Alignment.Center
                    ) {
                        if (last) Icon(Icons.Rounded.Check, null, tint = Color.White, modifier = Modifier.size(20.dp))
                        else Text("${i + 1}", color = Color.White, fontWeight = FontWeight.ExtraBold, fontSize = 15.sp)
                    }
                    if (!last) {
                        Box(
                            Modifier
                                .width(2.dp)
                                .weight(1f)
                                .background(Brush.verticalGradient(listOf(Yx.Red.copy(alpha = 0.6f), Yx.Surface3)))
                        )
                    }
                }
                Spacer(Modifier.width(12.dp))
                Column(
                    Modifier
                        .weight(1f)
                        .padding(bottom = if (last) 0.dp else 14.dp)
                        .clip(RoundedCornerShape(20.dp))
                        .background(Yx.Surface1)
                        .border(1.dp, Yx.Stroke, RoundedCornerShape(20.dp))
                        .padding(16.dp)
                ) {
                    Text("ADIM ${i + 1}", color = Yx.TextFaint, fontSize = 10.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.4.sp)
                    Spacer(Modifier.height(3.dp))
                    Text(title, color = Yx.Text, fontSize = 15.sp, fontWeight = FontWeight.Bold)
                    Spacer(Modifier.height(5.dp))
                    Text(desc, color = Yx.TextMuted, fontSize = 13.sp, lineHeight = 19.sp)
                }
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  SETTINGS
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun SettingsScreen(viewModel: AppViewModel, onOpenGuide: () -> Unit) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    var resumeTick by remember { mutableIntStateOf(0) }
    DisposableEffect(lifecycleOwner) {
        val obs = LifecycleEventObserver { _, e -> if (e == Lifecycle.Event.ON_RESUME) resumeTick++ }
        lifecycleOwner.lifecycle.addObserver(obs)
        onDispose { lifecycleOwner.lifecycle.removeObserver(obs) }
    }

    val notifLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { resumeTick++ }
    val storageLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) {
        resumeTick++
        viewModel.checkLoadFolderAccess()
    }
    val folderLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            try {
                context.contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                )
            } catch (_: Exception) {}
            viewModel.setGameFolderUri(uri)
        }
        resumeTick++
    }

    val hasStorage = remember(resumeTick) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) Environment.isExternalStorageManager()
        else ContextCompat.checkSelfPermission(context, Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
    }
    val hasInstall = remember(resumeTick) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) context.packageManager.canRequestPackageInstalls() else true
    }
    val hasNotif = remember(resumeTick) { NotificationManagerCompat.from(context).areNotificationsEnabled() }
    val hasFolder = viewModel.gameFolderUri.value != null

    fun openAppSettings() {
        try {
            context.startActivity(
                Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS)
                    .setData(Uri.parse("package:${context.packageName}"))
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            )
        } catch (_: Exception) {
            Toast.makeText(context, "Sistem ayarları açılamadı.", Toast.LENGTH_SHORT).show()
        }
    }

    val listState = rememberLazyListState()
    LaunchedEffect(Unit) {
        viewModel.scrollToTopEvents.collect { target ->
            if (target == "settings") listState.scrollToItem(0)
        }
    }

    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxWidth().widthIn(max = 860.dp),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = LocalBottomPad.current),
            verticalArrangement = Arrangement.spacedBy(22.dp)
        ) {
        item {
            Box(Modifier.bleedHorizontal(16.dp)) {
                ScreenHeader(title = "Ayarlar", overline = "Uygulama", subtitle = "Güncellemeler ve izinler")
            }
        }

        item { UpdateCard(viewModel) }

        item {
            SettingsGroup("İzinler") {
                SettingsRow(
                    icon = Icons.Rounded.FolderShared, accent = Yx.Blue,
                    title = "Tüm Dosyalara Erişim",
                    subtitle = "Yamaların YamaNX klasörüne çıkarılması için gerekli",
                    trailing = { PermissionPill(hasStorage) },
                    onClick = {
                        try {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                                context.startActivity(
                                    Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                                        .setData(Uri.parse("package:${context.packageName}"))
                                )
                            } else storageLauncher.launch(Manifest.permission.WRITE_EXTERNAL_STORAGE)
                        } catch (_: Exception) { openAppSettings() }
                    }
                )
                SettingsDivider()
                SettingsRow(
                    icon = Icons.Rounded.InstallMobile, accent = Yx.Purple,
                    title = "Bilinmeyen Uygulamaları Yükleme",
                    subtitle = "Uygulama içinden güncelleme kurabilmek için gerekli",
                    trailing = { PermissionPill(hasInstall) },
                    onClick = {
                        try {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                                context.startActivity(
                                    Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES)
                                        .setData(Uri.parse("package:${context.packageName}"))
                                )
                            }
                        } catch (_: Exception) { openAppSettings() }
                    }
                )
                SettingsDivider()
                SettingsRow(
                    icon = Icons.Rounded.Notifications, accent = Yx.Amber,
                    title = "Bildirimler",
                    subtitle = "İndirme ilerlemesini bildirimde göstermek için",
                    trailing = { PermissionPill(hasNotif) },
                    onClick = {
                        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU && !hasNotif) {
                            notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
                        } else {
                            try {
                                context.startActivity(
                                    Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                                        .putExtra(Settings.EXTRA_APP_PACKAGE, context.packageName)
                                )
                            } catch (_: Exception) { openAppSettings() }
                        }
                    }
                )
                SettingsDivider()
                SettingsRow(
                    icon = Icons.Rounded.SportsEsports, accent = Yx.Green,
                    title = "Oyun Klasörü",
                    subtitle = if (hasFolder) "Klasör bağlı, oyunlar eşleştiriliyor" else "Cihazdaki oyunları eşleştirmek için klasör seç",
                    trailing = { PermissionPill(hasFolder, grantedText = "Bağlı", deniedText = "Seç") },
                    onClick = { folderLauncher.launch(null) }
                )
            }
        }

        item {
            SettingsGroup("Diğer") {
                SettingsRow(
                    icon = Icons.AutoMirrored.Rounded.HelpOutline, accent = Yx.Blue,
                    title = "Kurulum Rehberi",
                    subtitle = "Yamayı Eden emülatörüne adım adım kur",
                    trailing = { Icon(Icons.AutoMirrored.Rounded.KeyboardArrowRight, null, tint = Yx.TextFaint) },
                    onClick = onOpenGuide
                )
                SettingsDivider()
                SettingsRow(
                    icon = Icons.Rounded.Tune, accent = Yx.TextMuted,
                    title = "Sistem Uygulama Ayarları",
                    subtitle = "İzin sorunlarında Android ayarlarını aç",
                    trailing = { Icon(Icons.AutoMirrored.Rounded.OpenInNew, null, tint = Yx.TextFaint, modifier = Modifier.size(18.dp)) },
                    onClick = { openAppSettings() }
                )
            }
        }
    }
    }
}

@Composable
fun UpdateCard(viewModel: AppViewModel) {
    val context = LocalContext.current
    val hasUpdate = viewModel.isUpdateAvailable.value
    val shape = RoundedCornerShape(28.dp)
    var notesOpen by remember { mutableStateOf(false) }

    Column(
        Modifier
            .fillMaxWidth()
            .clip(shape)
            .background(
                if (hasUpdate) Brush.linearGradient(listOf(Color(0xFF0D3326), Yx.Surface1))
                else Brush.linearGradient(listOf(Color(0xFF1E1020), Yx.Surface1))
            )
            .border(1.dp, if (hasUpdate) Yx.Green.copy(alpha = 0.4f) else Yx.StrokeHi, shape)
            .padding(20.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Image(
                painterResource(R.drawable.app_badge), null,
                modifier = Modifier.size(58.dp).clip(RoundedCornerShape(18.dp))
            )
            Spacer(Modifier.width(14.dp))
            Column(Modifier.weight(1f)) {
                Text("YamaNX", color = Yx.Text, fontSize = 19.sp, fontWeight = FontWeight.ExtraBold)
                Text("Sürüm ${viewModel.currentAppVersion}", color = Yx.TextMuted, fontSize = 13.sp)
                Spacer(Modifier.height(6.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.size(8.dp).clip(CircleShape).background(if (hasUpdate) Yx.Green else Yx.TextFaint))
                    Spacer(Modifier.width(6.dp))
                    Text(
                        if (hasUpdate) "Yeni sürüm hazır: ${viewModel.updateTagName.value}" else "GitHub üzerinden kontrol edilir",
                        color = if (hasUpdate) Yx.Green else Yx.TextMuted,
                        fontSize = 12.sp, fontWeight = FontWeight.SemiBold
                    )
                }
            }
        }

        if (hasUpdate && viewModel.updateReleaseNotes.value.isNotBlank()) {
            Spacer(Modifier.height(16.dp))
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(16.dp))
                    .background(Color.Black.copy(alpha = 0.25f))
                    .clickable { notesOpen = !notesOpen }
                    .padding(14.dp)
                    .animateContentSize()
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("Sürüm notları", color = Yx.Text, fontSize = 13.sp, fontWeight = FontWeight.Bold, modifier = Modifier.weight(1f))
                    Icon(
                        if (notesOpen) Icons.Rounded.ExpandLess else Icons.Rounded.ExpandMore,
                        null, tint = Yx.TextMuted, modifier = Modifier.size(20.dp)
                    )
                }
                Spacer(Modifier.height(6.dp))
                Text(
                    viewModel.updateReleaseNotes.value,
                    color = Yx.TextMuted, fontSize = 12.sp, lineHeight = 18.sp,
                    maxLines = if (notesOpen) Int.MAX_VALUE else 3,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }

        Spacer(Modifier.height(18.dp))
        when {
            viewModel.isDownloadingUpdate.value -> {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(viewModel.updateStatusText.value, color = Yx.Text, fontSize = 13.sp, modifier = Modifier.weight(1f))
                    Text(
                        "${(viewModel.updateDownloadProgress.value * 100).toInt()}%",
                        color = Yx.Green, fontSize = 13.sp, fontWeight = FontWeight.Bold
                    )
                }
                Spacer(Modifier.height(10.dp))
                SmoothProgressBar(
                    viewModel.updateDownloadProgress.value, Modifier.fillMaxWidth(),
                    Brush.horizontalGradient(listOf(Color(0xFF10B981), Yx.Green))
                )
            }
            hasUpdate -> GradientButton(
                "Şimdi Güncelle", Icons.Rounded.SystemUpdate,
                { viewModel.downloadAndInstallUpdate(context) },
                Modifier.fillMaxWidth(),
                brush = Brush.linearGradient(listOf(Color(0xFF34D399), Color(0xFF059669)))
            )
            else -> SoftButton(
                if (viewModel.isCheckingUpdate.value) "Denetleniyor…" else "Güncellemeleri Denetle",
                if (viewModel.isCheckingUpdate.value) null else Icons.Rounded.Refresh,
                { viewModel.checkForUpdates(isManual = true) },
                Modifier.fillMaxWidth(),
                enabled = !viewModel.isCheckingUpdate.value
            )
        }
    }
}

@Composable
fun SettingsGroup(title: String, content: @Composable ColumnScope.() -> Unit) {
    Column {
        Text(
            title.uppercase(), color = Yx.TextFaint, fontSize = 11.sp, fontWeight = FontWeight.Bold,
            letterSpacing = 1.4.sp, modifier = Modifier.padding(start = 8.dp, bottom = 10.dp)
        )
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(24.dp))
                .background(Yx.Surface1)
                .border(1.dp, Yx.Stroke, RoundedCornerShape(24.dp)),
            content = content
        )
    }
}

@Composable
fun SettingsRow(
    icon: ImageVector,
    accent: Color,
    title: String,
    subtitle: String,
    trailing: @Composable () -> Unit,
    onClick: () -> Unit
) {
    Row(
        Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(horizontal = 16.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            Modifier.size(42.dp).clip(RoundedCornerShape(14.dp)).background(accent.copy(alpha = 0.14f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, null, tint = accent, modifier = Modifier.size(21.dp))
        }
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(title, color = Yx.Text, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.height(2.dp))
            Text(subtitle, color = Yx.TextMuted, fontSize = 12.sp, lineHeight = 16.sp)
        }
        Spacer(Modifier.width(10.dp))
        trailing()
    }
}

@Composable
fun SettingsDivider() {
    HorizontalDivider(Modifier.padding(start = 72.dp), thickness = 1.dp, color = Yx.Stroke)
}

@Composable
fun PermissionPill(granted: Boolean, grantedText: String = "Verildi", deniedText: String = "İzin Ver") {
    val c = if (granted) Yx.Green else Yx.RedBright
    Row(
        Modifier
            .clip(CircleShape)
            .background(c.copy(alpha = 0.12f))
            .border(1.dp, c.copy(alpha = 0.4f), CircleShape)
            .padding(start = 7.dp, end = 10.dp, top = 5.dp, bottom = 5.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(if (granted) Icons.Rounded.Check else Icons.Rounded.PriorityHigh, null, tint = c, modifier = Modifier.size(13.dp))
        Spacer(Modifier.width(4.dp))
        Text(if (granted) grantedText else deniedText, color = c, fontSize = 11.sp, fontWeight = FontWeight.Bold)
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  ABOUT
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun AboutScreen(viewModel: AppViewModel) {
    val context = LocalContext.current
    fun openUrl(url: String) {
        try { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) } catch (_: Exception) {}
    }

    val aboutText = """Merhaba, ben SertAy.

YamaNX uygulamasını, daha önce hiçbir yazılım tecrübem olmadan tamamen yapay zeka desteğiyle sıfırdan geliştirmeye çalıştım. Bu nedenle ortaya çıkması uzun vaktimi alan, bolca emek içeren bir süreç oldu. Uygulamayı kullanırken herhangi bir hatayla karşılaşırsanız veya geliştirilmesi yönünde önerileriniz olursa lütfen benimle iletişim kurmaktan çekinmeyin.

Yamaların yapımcısı ben değilim. Arşivde; Swatalk'ın 470'ten fazla ücretsiz yaması ve 200'den fazla Soner Çakır, Profesör Pikachu, Dede00, emre, davetsiz57 gibi pek çok çevirmen arkadaşın internetten bulduğum yamaları yer alıyor. Tespit edebildiğim tüm isimleri oyun seçim ekranına ekledim.

Sürekli güncellenen arşivimize eksik yamaların eklenmesi için bana, sıfırdan çeviri istekleriniz için ise doğrudan yapımcılara yazabilirsiniz.

Bana destek olmak için sosyal medya hesaplarımdan takip edebilirsiniz."""

    val listState = rememberLazyListState()
    LaunchedEffect(Unit) {
        viewModel.scrollToTopEvents.collect { target ->
            if (target == "about") listState.scrollToItem(0)
        }
    }

    Box(Modifier.fillMaxSize(), contentAlignment = Alignment.TopCenter) {
        LazyColumn(
            state = listState,
            modifier = Modifier.fillMaxWidth().widthIn(max = 860.dp),
            contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = LocalBottomPad.current),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
        item {
            Box(Modifier.bleedHorizontal(16.dp)) {
                ScreenHeader(title = "Hakkında", overline = "YamaNX")
            }
        }

        // Brand hero
        item {
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(28.dp))
                    .background(
                        Brush.radialGradient(
                            listOf(Yx.Red.copy(alpha = 0.22f), Yx.Surface1),
                            radius = 900f
                        )
                    )
                    .border(1.dp, Yx.StrokeHi, RoundedCornerShape(28.dp))
                    .padding(vertical = 28.dp, horizontal = 20.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Image(
                    painterResource(R.drawable.logo_wide), "YamaNX",
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.fillMaxWidth(0.72f)
                )
                Spacer(Modifier.height(16.dp))
                MiniChip("v2.1.0", Yx.RedBright)
            }
        }

        // Story
        item {
            Column(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(24.dp))
                    .background(Yx.Surface1)
                    .border(1.dp, Yx.Stroke, RoundedCornerShape(24.dp))
                    .padding(20.dp)
            ) {
                Text(aboutText, color = Yx.Text.copy(alpha = 0.9f), fontSize = 14.sp, lineHeight = 22.sp)
            }
        }

        item { SectionTitle("Bağlantılar", modifier = Modifier.padding(top = 8.dp, start = 4.dp)) }

        item {
            PersonCardWide(
                name = "SertAy", role = "Geliştirici",
                imageRes = R.drawable.photo_sertay, crop = false,
                onClick = { openUrl("https://linktr.ee/yamanx") }
            )
        }
        item {
            Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                PersonCard(
                    "Swatalk", "Çevirmen", R.drawable.photo_swatalk,
                    { openUrl("https://discord.com/invite/xshWw2jBK6") }, Modifier.weight(1f)
                )
                PersonCard(
                    "Soner Çakır", "Çevirmen", R.drawable.photo_sonercakir,
                    { openUrl("https://discord.com/invite/tB93ZUsRYE") }, Modifier.weight(1f)
                )
            }
        }
    }
}
}

@Composable
fun MiniChip(text: String, color: Color) {
    Box(
        Modifier
            .clip(CircleShape)
            .background(color.copy(alpha = 0.12f))
            .border(1.dp, color.copy(alpha = 0.3f), CircleShape)
            .padding(horizontal = 10.dp, vertical = 4.dp)
    ) {
        Text(text, color = color, fontSize = 11.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
fun PersonCardWide(name: String, role: String, imageRes: Int, crop: Boolean, onClick: () -> Unit) {
    val src = remember { MutableInteractionSource() }
    Row(
        Modifier
            .fillMaxWidth()
            .pressScale(src, 0.98f)
            .clip(RoundedCornerShape(24.dp))
            .background(Yx.Surface1)
            .border(1.dp, Yx.Stroke, RoundedCornerShape(24.dp))
            .clickable(interactionSource = src, indication = LocalIndication.current, onClick = onClick)
            .padding(12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Image(
            painterResource(imageRes), name,
            contentScale = if (crop) ContentScale.Crop else ContentScale.Fit,
            modifier = Modifier.size(84.dp).clip(RoundedCornerShape(18.dp)).background(Yx.Surface2)
        )
        Spacer(Modifier.width(14.dp))
        Column(Modifier.weight(1f)) {
            Text(name, color = Yx.Text, fontSize = 17.sp, fontWeight = FontWeight.Bold)
            Text(role, color = Yx.TextMuted, fontSize = 13.sp)
            Spacer(Modifier.height(8.dp))
            LinkLabel()
        }
    }
}

@Composable
fun PersonCard(name: String, role: String, imageRes: Int, onClick: () -> Unit, modifier: Modifier = Modifier) {
    val src = remember { MutableInteractionSource() }
    Column(
        modifier
            .pressScale(src, 0.97f)
            .clip(RoundedCornerShape(24.dp))
            .background(Yx.Surface1)
            .border(1.dp, Yx.Stroke, RoundedCornerShape(24.dp))
            .clickable(interactionSource = src, indication = LocalIndication.current, onClick = onClick)
            .padding(10.dp)
    ) {
        Image(
            painterResource(imageRes), name,
            contentScale = ContentScale.Crop,
            modifier = Modifier.fillMaxWidth().aspectRatio(1f).clip(RoundedCornerShape(18.dp)).background(Yx.Surface2)
        )
        Spacer(Modifier.height(10.dp))
        Column(Modifier.padding(horizontal = 4.dp)) {
            Text(name, color = Yx.Text, fontSize = 15.sp, fontWeight = FontWeight.Bold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(role, color = Yx.TextMuted, fontSize = 12.sp)
            Spacer(Modifier.height(8.dp))
            LinkLabel()
        }
        Spacer(Modifier.height(4.dp))
    }
}

@Composable
private fun LinkLabel() {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("Linke Git", color = Yx.Blue, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Spacer(Modifier.width(4.dp))
        Icon(Icons.AutoMirrored.Rounded.OpenInNew, null, tint = Yx.Blue, modifier = Modifier.size(13.dp))
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  DOWNLOAD DIALOG
// ═════════════════════════════════════════════════════════════════════════════
@Composable
fun DownloadDialog(viewModel: AppViewModel) {
    val finished = viewModel.downloadFinished.value
    val success = viewModel.downloadStatus.value.startsWith("✅")
    val progress by animateFloatAsState(viewModel.downloadProgress.value, tween(300), label = "dl")

    Dialog(
        onDismissRequest = { if (finished) viewModel.dismissDownload() },
        properties = DialogProperties(dismissOnBackPress = finished, dismissOnClickOutside = finished)
    ) {
        Column(
            Modifier
                .fillMaxWidth()
                .clip(RoundedCornerShape(30.dp))
                .background(Yx.Surface1)
                .border(1.dp, Yx.StrokeHi, RoundedCornerShape(30.dp))
                .padding(26.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Box(Modifier.size(116.dp), contentAlignment = Alignment.Center) {
                if (!finished) {
                    CircularProgressIndicator(
                        progress = { progress },
                        modifier = Modifier.fillMaxSize(),
                        color = Yx.RedBright,
                        strokeWidth = 9.dp,
                        trackColor = Yx.Surface3,
                        strokeCap = StrokeCap.Round
                    )
                    Text("${(progress * 100).toInt()}%", color = Yx.Text, fontSize = 24.sp, fontWeight = FontWeight.ExtraBold)
                } else {
                    val c = if (success) Yx.Green else Yx.RedBright
                    Box(
                        Modifier
                            .size(96.dp)
                            .clip(CircleShape)
                            .background(Brush.radialGradient(listOf(c.copy(alpha = 0.3f), c.copy(alpha = 0.06f))))
                            .border(2.dp, c.copy(alpha = 0.5f), CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(
                            if (success) Icons.Rounded.Check else Icons.Rounded.ErrorOutline,
                            null, tint = c, modifier = Modifier.size(48.dp)
                        )
                    }
                }
            }

            Spacer(Modifier.height(20.dp))
            Text(
                when {
                    !finished -> "Yama kuruluyor"
                    success -> "Kurulum tamamlandı"
                    else -> "Bir sorun oluştu"
                },
                color = Yx.Text, fontSize = 19.sp, fontWeight = FontWeight.ExtraBold
            )
            if (viewModel.downloadPatchName.value.isNotBlank()) {
                Spacer(Modifier.height(4.dp))
                Text(
                    viewModel.downloadPatchName.value, color = Yx.TextMuted, fontSize = 13.sp,
                    textAlign = TextAlign.Center, maxLines = 2, overflow = TextOverflow.Ellipsis
                )
            }
            Spacer(Modifier.height(14.dp))
            Box(
                Modifier
                    .fillMaxWidth()
                    .clip(RoundedCornerShape(14.dp))
                    .background(Yx.Surface2)
                    .padding(horizontal = 14.dp, vertical = 10.dp),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    viewModel.downloadStatus.value.removePrefix("✅ ").removePrefix("❌ "),
                    color = if (finished && !success) Yx.RedBright else Yx.TextMuted,
                    fontSize = 12.sp, textAlign = TextAlign.Center, lineHeight = 17.sp
                )
            }
            if (finished) {
                Spacer(Modifier.height(20.dp))
                GradientButton(
                    "Tamam", null, { viewModel.dismissDownload() }, Modifier.fillMaxWidth(),
                    brush = if (success) Brush.linearGradient(listOf(Color(0xFF34D399), Color(0xFF059669))) else Yx.RedGradient
                )
            } else {
                Spacer(Modifier.height(16.dp))
                SmoothProgressBar(progress, Modifier.fillMaxWidth())
            }
        }
    }
}
