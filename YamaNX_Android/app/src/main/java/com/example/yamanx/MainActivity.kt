package com.example.yamanx

import android.Manifest
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import androidx.activity.ComponentActivity
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.List
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import coil.compose.AsyncImage
import coil.request.ImageRequest
import com.example.yamanx.data.Patch
import kotlinx.coroutines.launch

// ─── Color Palette ────────────────────────────────────────────────────────────
val NintendoRed   = Color(0xFFE60012)
val DarkBg        = Color(0xFF0F0F14)
val CardBg        = Color(0xFF1A1A24)
val SurfaceBg     = Color(0xFF14141E)
val DrawerBg      = Color(0xFF0A0A12)  // deeper dark for sidebar contrast
val AccentBlue    = Color(0xFF3D9FFF)
val CompatGreen   = Color(0xFF2ECC71)
val CompatOrange  = Color(0xFFF39C12)
val CompatRed     = Color(0xFFE74C3C)
val TextPrimary   = Color(0xFFF5F5F5)
val TextSecondary = Color(0xFF9090A0)

// ─── Nav Items ────────────────────────────────────────────────────────────────
data class NavItem(val route: String, val label: String, val icon: ImageVector)

val navItems = listOf(
    NavItem("all_patches",       "Yamalar",       Icons.AutoMirrored.Filled.List),
    NavItem("installed_content", "Yüklü",         Icons.Default.CheckCircle),
    NavItem("how_to_install",    "Nasıl Kurulur", Icons.Default.HelpOutline),
    NavItem("about",             "Hakkında",      Icons.Default.Info)
)

class MainActivity : ComponentActivity() {

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                startActivity(
                    Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                        .setData(Uri.parse("package:$packageName"))
                )
            }
        }

        setContent {
            MaterialTheme(
                colorScheme = darkColorScheme(
                    primary = NintendoRed,
                    background = DarkBg,
                    surface = SurfaceBg,
                    onPrimary = Color.White,
                    onBackground = TextPrimary,
                    onSurface = TextPrimary,
                )
            ) {
                Surface(modifier = Modifier.fillMaxSize(), color = DarkBg) {
                    val navController = rememberNavController()
                    val viewModel: AppViewModel = viewModel()

                    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        val notifLauncher = rememberLauncherForActivityResult(
                            ActivityResultContracts.RequestPermission()
                        ) {}
                        LaunchedEffect(Unit) { notifLauncher.launch(Manifest.permission.POST_NOTIFICATIONS) }
                    }

                    // Animated splash overlay
                    var splashVisible by remember { mutableStateOf(true) }
                    val splashAlpha by animateFloatAsState(
                        targetValue = if (splashVisible) 1f else 0f,
                        animationSpec = tween(600, easing = FastOutSlowInEasing),
                        label = "splashAlpha"
                    )
                    val splashScale by animateFloatAsState(
                        targetValue = if (splashVisible) 1f else 1.08f,
                        animationSpec = tween(600, easing = FastOutSlowInEasing),
                        label = "splashScale"
                    )
                    LaunchedEffect(Unit) {
                        kotlinx.coroutines.delay(1400)
                        splashVisible = false
                    }

                    AppScaffold(navController, viewModel)

                    if (viewModel.isDownloading.value) {
                        DownloadDialog(viewModel)
                    }

                    // Splash overlay
                    if (splashAlpha > 0f) {
                        Box(
                            modifier = Modifier
                                .fillMaxSize()
                                .background(DarkBg)
                                .graphicsLayer { alpha = splashAlpha },
                            contentAlignment = Alignment.Center
                        ) {
                            Column(
                                horizontalAlignment = Alignment.CenterHorizontally,
                                modifier = Modifier.graphicsLayer {
                                    scaleX = splashScale
                                    scaleY = splashScale
                                }
                            ) {
                                Image(
                                    painter = painterResource(id = R.drawable.logo),
                                    contentDescription = "YamaNX",
                                    contentScale = ContentScale.Fit,
                                    modifier = Modifier.width(300.dp).height(130.dp)
                                )
                            }
                            // Bottom loading indicator
                            Column(
                                modifier = Modifier
                                    .align(Alignment.BottomCenter)
                                    .padding(bottom = 60.dp),
                                horizontalAlignment = Alignment.CenterHorizontally
                            ) {
                                LinearProgressIndicator(
                                    modifier = Modifier
                                        .width(120.dp)
                                        .height(3.dp)
                                        .clip(RoundedCornerShape(2.dp)),
                                    color = NintendoRed,
                                    trackColor = Color(0xFF252535)
                                )
                            }
                        }
                    }
                }
            }
        }
    }
}

// ─── App Scaffold (Drawer + Adaptive Layout) ──────────────────────────────────
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AppScaffold(navController: NavHostController, viewModel: AppViewModel) {
    val configuration = LocalConfiguration.current
    val screenWidthDp = configuration.screenWidthDp
    val isWideScreen = screenWidthDp >= 600

    val drawerState = rememberDrawerState(DrawerValue.Closed)
    val scope = rememberCoroutineScope()

    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentRoute = navBackStackEntry?.destination?.route

    fun navigate(route: String) {
        navController.navigate(route) {
            launchSingleTop = true
            restoreState = true
        }
        if (!isWideScreen) scope.launch { drawerState.close() }
    }

    val drawerContent: @Composable ColumnScope.() -> Unit = {
        // Make the whole drawer scrollable so items are reachable in landscape
        Column(
            modifier = Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
        ) {
            // Logo section
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(DrawerBg)
                    .padding(horizontal = 16.dp, vertical = 16.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                Image(
                    painter = painterResource(id = R.drawable.logo),
                    contentDescription = "YamaNX Logo",
                    contentScale = ContentScale.Fit,
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(if (isWideScreen) 80.dp else 110.dp)
                )
                Spacer(Modifier.height(4.dp))
                Text("v1.1.1", color = TextSecondary, fontSize = 11.sp)
            }
            HorizontalDivider(color = Color(0xFF1A1A2A))
            Spacer(Modifier.height(6.dp))

            // Nav Items
            navItems.forEach { item ->
                val selected = currentRoute == item.route
                NavigationDrawerItem(
                    icon = { Icon(item.icon, contentDescription = item.label, modifier = Modifier.size(20.dp)) },
                    label = { Text(item.label, fontSize = 14.sp, fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal) },
                    selected = selected,
                    onClick = { navigate(item.route) },
                    modifier = Modifier.padding(horizontal = 8.dp),
                    colors = NavigationDrawerItemDefaults.colors(
                        selectedContainerColor = NintendoRed.copy(alpha = 0.20f),
                        selectedIconColor = NintendoRed,
                        selectedTextColor = NintendoRed,
                        unselectedContainerColor = Color.Transparent,
                        unselectedIconColor = TextSecondary,
                        unselectedTextColor = TextSecondary
                    )
                )
                Spacer(Modifier.height(2.dp))
            }
            Spacer(Modifier.height(16.dp)) // bottom breathing room
        }
    }

    val openDrawer: () -> Unit = { scope.launch { drawerState.open() } }

    if (isWideScreen) {
        // Wide/tablet: permanent narrow sidebar
        PermanentNavigationDrawer(
            drawerContent = {
                PermanentDrawerSheet(
                    modifier = Modifier.width(190.dp),
                    drawerContainerColor = DrawerBg,
                    drawerContentColor = TextPrimary
                ) {
                    drawerContent()
                }
            }
        ) {
            AppNavHost(navController, viewModel, openDrawer, isWideScreen)
        }
    } else {
        // Phone: modal drawer
        ModalNavigationDrawer(
            drawerState = drawerState,
            drawerContent = {
                ModalDrawerSheet(
                    modifier = Modifier.width(260.dp),
                    drawerContainerColor = DrawerBg,
                    drawerContentColor = TextPrimary
                ) {
                    drawerContent()
                }
            }
        ) {
            AppNavHost(navController, viewModel, openDrawer, isWideScreen)
        }
    }
}

// ─── Nav Host ─────────────────────────────────────────────────────────────────
@Composable
fun AppNavHost(
    navController: NavHostController,
    viewModel: AppViewModel,
    openDrawer: () -> Unit,
    isWideScreen: Boolean
) {
    NavHost(
        navController = navController,
        startDestination = "all_patches",
        enterTransition = { fadeIn(tween(220)) + slideInHorizontally(tween(220)) { it / 8 } },
        exitTransition = { fadeOut(tween(160)) + slideOutHorizontally(tween(160)) { -it / 8 } },
        popEnterTransition = { fadeIn(tween(220)) + slideInHorizontally(tween(220)) { -it / 8 } },
        popExitTransition = { fadeOut(tween(160)) + slideOutHorizontally(tween(160)) { it / 8 } }
    ) {
        composable("all_patches")       { AllPatchesScreen(viewModel, openDrawer, isWideScreen) }
        composable("installed_content") { InstalledContentScreen(viewModel, openDrawer) }
        composable("how_to_install")    { HowToInstallScreen(openDrawer) }
        composable("about")             { AboutScreen(openDrawer) }
    }
}

// ─── TopAppBar helper ─────────────────────────────────────────────────────────
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun YamaTopBar(title: String, onMenuClick: () -> Unit, isWideScreen: Boolean) {
    if (isWideScreen) return  // wide: drawer always visible, no top bar needed
    TopAppBar(
        title = { Text(title, fontWeight = FontWeight.Bold, fontSize = 17.sp, color = TextPrimary) },
        navigationIcon = {
            IconButton(onClick = onMenuClick) {
                Icon(Icons.Default.Menu, contentDescription = "Menü", tint = TextPrimary)
            }
        },
        colors = TopAppBarDefaults.topAppBarColors(containerColor = SurfaceBg)
    )
}

// ─── All Patches Screen ───────────────────────────────────────────────────────
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun AllPatchesScreen(viewModel: AppViewModel, openDrawer: () -> Unit, isWideScreen: Boolean) {
    var searchQuery by remember { mutableStateOf("") }
    var selectedPatch by remember { mutableStateOf<Patch?>(null) }

    val configuration = LocalConfiguration.current
    val screenWidthDp = configuration.screenWidthDp

    // Adaptive column count — goes up to 6 for very wide/tablet screens
    val columns = when {
        screenWidthDp >= 1100 -> 6
        screenWidthDp >= 840  -> 5
        screenWidthDp >= 600  -> 4
        screenWidthDp >= 480  -> 3
        else                  -> 2
    }

    val patchesSize = viewModel.allPatches.size
    val filteredList = remember(patchesSize, searchQuery) {
        val q = viewModel.normalizeForSearch(searchQuery)
        val list = viewModel.allPatches
        if (q.isBlank()) list.toList()
        else list.filter { viewModel.normalizeForSearch(it.name).contains(q) }
    }

    Column(modifier = Modifier.fillMaxSize().background(DarkBg)) {
        // TopBar (phone only)
        if (!isWideScreen) {
            Surface(color = SurfaceBg, shadowElevation = 4.dp) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .statusBarsPadding()
                        .padding(start = 4.dp, end = 16.dp, top = 6.dp, bottom = 6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    IconButton(onClick = openDrawer) {
                        Icon(Icons.Default.Menu, contentDescription = "Menü", tint = TextPrimary)
                    }
                    Image(
                        painter = painterResource(id = R.drawable.logo),
                        contentDescription = "YamaNX Logo",
                        contentScale = ContentScale.Fit,
                        modifier = Modifier.weight(1f).height(80.dp)
                    )
                    Spacer(Modifier.width(48.dp))
                }
            }
        } else {
            // Wide screen: compact logo strip
            Box(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(SurfaceBg)
                    .padding(horizontal = 20.dp, vertical = 8.dp),
                contentAlignment = Alignment.Center
            ) {
                Image(
                    painter = painterResource(id = R.drawable.logo),
                    contentDescription = "YamaNX Logo",
                    contentScale = ContentScale.Fit,
                    modifier = Modifier.height(56.dp)
                )
            }
        }

        // Search bar
        Surface(color = SurfaceBg) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 14.dp, vertical = 10.dp)
                    .height(52.dp),
                placeholder = {
                    Text("Yama ara... (örn: Pokemon)", color = TextSecondary, fontSize = 13.sp)
                },
                leadingIcon = {
                    Icon(Icons.Default.Search, null, tint = TextSecondary, modifier = Modifier.size(20.dp))
                },
                trailingIcon = if (searchQuery.isNotEmpty()) {
                    {
                        IconButton(onClick = { searchQuery = "" }) {
                            Icon(Icons.Default.Clear, null, tint = TextSecondary, modifier = Modifier.size(18.dp))
                        }
                    }
                } else null,
                singleLine = true,
                shape = RoundedCornerShape(14.dp),
                colors = OutlinedTextFieldDefaults.colors(
                    focusedBorderColor = NintendoRed,
                    unfocusedBorderColor = Color(0xFF2A2A3A),
                    focusedContainerColor = CardBg,
                    unfocusedContainerColor = CardBg,
                    focusedTextColor = TextPrimary,
                    unfocusedTextColor = TextPrimary,
                    cursorColor = NintendoRed
                ),
                textStyle = LocalTextStyle.current.copy(fontSize = 13.sp)
            )
        }

        if (!viewModel.isLoading.value) {
            Text(
                text = "${filteredList.size} yama",
                color = TextSecondary,
                fontSize = 11.sp,
                modifier = Modifier.padding(horizontal = 14.dp, vertical = 4.dp)
            )
        }

        when {
            viewModel.isLoading.value -> {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        CircularProgressIndicator(color = NintendoRed, modifier = Modifier.size(48.dp))
                        Text("Yamalar yükleniyor...", color = TextSecondary)
                    }
                }
            }
            viewModel.errorMessage.value != null -> {
                Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                    Column(
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(10.dp),
                        modifier = Modifier.padding(24.dp)
                    ) {
                        Icon(Icons.Default.Warning, null, tint = NintendoRed, modifier = Modifier.size(52.dp))
                        Text(viewModel.errorMessage.value ?: "", color = NintendoRed, textAlign = TextAlign.Center)
                        Button(
                            onClick = { viewModel.fetchPatches() },
                            colors = ButtonDefaults.buttonColors(containerColor = NintendoRed)
                        ) { Text("Tekrar Dene") }
                    }
                }
            }
            else -> {
                LazyVerticalGrid(
                    columns = GridCells.Fixed(columns),
                    contentPadding = PaddingValues(10.dp),
                    horizontalArrangement = Arrangement.spacedBy(10.dp),
                    verticalArrangement = Arrangement.spacedBy(10.dp),
                    modifier = Modifier.fillMaxSize()
                ) {
                    items(filteredList, key = { it.titleId }) { patch ->
                        PatchGridCard(
                            patch = patch,
                            imageLoader = viewModel.imageLoader,
                            onClick = { selectedPatch = patch }
                        )
                    }
                }
            }
        }
    }

    selectedPatch?.let { patch ->
        PatchDetailDialog(
            patch = patch,
            viewModel = viewModel,
            onDismiss = { selectedPatch = null }
        )
    }
}

// ─── Grid Card ────────────────────────────────────────────────────────────────
@Composable
fun PatchGridCard(patch: Patch, imageLoader: coil.ImageLoader, onClick: () -> Unit) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() }
            .shadow(3.dp, RoundedCornerShape(14.dp)),
        shape = RoundedCornerShape(14.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg)
    ) {
        Box {
            AsyncImage(
                model = ImageRequest.Builder(LocalContext.current)
                    .data("https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.titleId}.jpg")
                    .crossfade(true)
                    .diskCacheKey(patch.titleId)
                    .memoryCacheKey(patch.titleId)
                    .build(),
                imageLoader = imageLoader,
                contentDescription = patch.name,
                contentScale = ContentScale.Crop,
                modifier = Modifier
                    .fillMaxWidth()
                    .aspectRatio(16f / 9f)
                    .clip(RoundedCornerShape(topStart = 14.dp, topEnd = 14.dp))
                    .background(Color(0xFF1E1E2E)),
                error = painterResource(id = R.drawable.splash),
                placeholder = painterResource(id = R.drawable.splash)
            )
            Row(
                modifier = Modifier.align(Alignment.TopEnd).padding(5.dp),
                horizontalArrangement = Arrangement.spacedBy(3.dp)
            ) {
                when {
                    patch.isInstalled   -> StatusChip("Yüklü", CompatGreen)
                    patch.gameInstalled -> StatusChip("Yüklü Değil", CompatOrange)
                }
            }
        }
        Column(modifier = Modifier.padding(horizontal = 9.dp, vertical = 7.dp)) {
            Text(
                patch.name,
                color = TextPrimary,
                fontWeight = FontWeight.SemiBold,
                fontSize = 12.sp,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
                lineHeight = 16.sp
            )
        }
    }
}

@Composable
fun StatusChip(label: String, color: Color) {
    Box(
        modifier = Modifier
            .background(color.copy(alpha = 0.92f), RoundedCornerShape(5.dp))
            .padding(horizontal = 5.dp, vertical = 2.dp)
    ) {
        Text(label, color = Color.White, fontSize = 9.sp, fontWeight = FontWeight.Bold)
    }
}

// ─── Patch Detail Dialog ──────────────────────────────────────────────────────
@Composable
fun PatchDetailDialog(patch: Patch, viewModel: AppViewModel, onDismiss: () -> Unit) {
    val context = LocalContext.current
    var showRemoveConfirm by remember { mutableStateOf(false) }

    val configuration = LocalConfiguration.current
    val isLandscape = configuration.screenWidthDp > configuration.screenHeightDp
    val isWide = configuration.screenWidthDp >= 480

    Dialog(
        onDismissRequest = onDismiss,
        properties = DialogProperties(usePlatformDefaultWidth = false)
    ) {
        Card(
            modifier = Modifier
                .fillMaxWidth(if (isWide) 0.88f else 0.95f)
                .wrapContentHeight(),
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = CardBg)
        ) {
            if (isLandscape) {
                // ── Landscape: side-by-side layout
                Row(modifier = Modifier.height(IntrinsicSize.Min)) {
                    AsyncImage(
                        model = ImageRequest.Builder(context)
                            .data("https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.titleId}.jpg")
                            .crossfade(true).build(),
                        imageLoader = viewModel.imageLoader,
                        contentDescription = patch.name,
                        contentScale = ContentScale.Crop,
                        modifier = Modifier
                            .width(200.dp)
                            .fillMaxHeight()
                            .clip(RoundedCornerShape(topStart = 20.dp, bottomStart = 20.dp)),
                        error = painterResource(id = R.drawable.splash)
                    )
                    Column(
                        modifier = Modifier
                            .weight(1f)
                            .verticalScroll(rememberScrollState())
                            .padding(horizontal = 18.dp, vertical = 14.dp)
                    ) {
                        Text(patch.name, color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 17.sp, lineHeight = 22.sp)
                        Spacer(Modifier.height(8.dp))
                        InfoRow(Icons.Default.Person, "Yapımcı", patch.yapimci.ifBlank { "Belirtilmemiş" })
                        InfoRow(Icons.Default.Storage, "Boyut", patch.size)
                        InfoRow(Icons.Default.NewLabel, "Yama Sürümü",
                            if (patch.patchVersion.isBlank() || patch.patchVersion.trim() == "\r") "Belirtilmemiş"
                            else patch.patchVersion.trim())
                        Spacer(Modifier.height(6.dp))
                        val (compatText, compatColor) = when {
                            patch.patchVersion.isBlank() || patch.patchVersion.trim() == "\r" -> "BELİRSİZ" to CompatOrange
                            patch.gameInstalled -> "UYUMLU" to CompatGreen
                            else -> "BELİRSİZ" to CompatOrange
                        }
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("Uyumluluk:", color = TextSecondary, fontSize = 13.sp)
                            Box(modifier = Modifier.background(compatColor.copy(alpha = 0.18f), RoundedCornerShape(8.dp)).padding(horizontal = 10.dp, vertical = 3.dp)) {
                                Text(compatText, color = compatColor, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                            }
                        }
                        Spacer(Modifier.height(12.dp))
                        DialogButtons(patch, viewModel, showRemoveConfirm, { showRemoveConfirm = it }, onDismiss)
                    }
                }
            } else {
                // ── Portrait: vertical layout
                Column {
                    Box(modifier = Modifier.fillMaxWidth().height(185.dp)) {
                        AsyncImage(
                            model = ImageRequest.Builder(context)
                                .data("https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.titleId}.jpg")
                                .crossfade(true).build(),
                            imageLoader = viewModel.imageLoader,
                            contentDescription = patch.name,
                            contentScale = ContentScale.Crop,
                            modifier = Modifier
                                .fillMaxSize()
                                .clip(RoundedCornerShape(topStart = 20.dp, topEnd = 20.dp)),
                            error = painterResource(id = R.drawable.splash)
                        )
                        Box(
                            modifier = Modifier
                                .fillMaxWidth().height(80.dp).align(Alignment.BottomCenter)
                                .background(Brush.verticalGradient(listOf(Color.Transparent, CardBg)))
                        )
                    }
                    Column(modifier = Modifier.padding(horizontal = 18.dp, vertical = 12.dp)) {
                        Text(patch.name, color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 18.sp, lineHeight = 24.sp)
                        Spacer(Modifier.height(10.dp))
                        InfoRow(Icons.Default.Person, "Yapımcı", patch.yapimci.ifBlank { "Belirtilmemiş" })
                        InfoRow(Icons.Default.Storage, "Boyut", patch.size)
                        InfoRow(
                            Icons.Default.NewLabel, "Yama Sürümü",
                            if (patch.patchVersion.isBlank() || patch.patchVersion.trim() == "\r") "Belirtilmemiş"
                            else patch.patchVersion.trim()
                        )
                        Spacer(Modifier.height(8.dp))
                        val (compatText, compatColor) = when {
                            patch.patchVersion.isBlank() || patch.patchVersion.trim() == "\r" -> "BELİRSİZ" to CompatOrange
                            patch.gameInstalled -> "UYUMLU" to CompatGreen
                            else -> "BELİRSİZ" to CompatOrange
                        }
                        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            Text("Uyumluluk:", color = TextSecondary, fontSize = 13.sp)
                            Box(modifier = Modifier.background(compatColor.copy(alpha = 0.18f), RoundedCornerShape(8.dp)).padding(horizontal = 12.dp, vertical = 4.dp)) {
                                Text(compatText, color = compatColor, fontWeight = FontWeight.Bold, fontSize = 13.sp)
                            }
                        }
                        Spacer(Modifier.height(14.dp))
                        DialogButtons(patch, viewModel, showRemoveConfirm, { showRemoveConfirm = it }, onDismiss)
                    }
                }
            } // end portrait
        }
    }
}

@Composable
fun DialogButtons(
    patch: Patch,
    viewModel: AppViewModel,
    showRemoveConfirm: Boolean,
    onShowRemoveConfirm: (Boolean) -> Unit,
    onDismiss: () -> Unit
) {
    if (patch.isInstalled) {
        if (showRemoveConfirm) {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    "Yamayı kaldırmak istediğinize emin misiniz?",
                    color = TextPrimary, fontSize = 13.sp,
                    textAlign = TextAlign.Center, modifier = Modifier.fillMaxWidth()
                )
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    OutlinedButton(
                        onClick = { onShowRemoveConfirm(false) },
                        modifier = Modifier.weight(1f), shape = RoundedCornerShape(10.dp)
                    ) { Text("İptal", color = TextSecondary) }
                    Button(
                        onClick = {
                            viewModel.removePatch(patch)
                            onShowRemoveConfirm(false)
                            onDismiss()
                        },
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = CompatRed),
                        shape = RoundedCornerShape(10.dp)
                    ) { Text("Kaldır", fontWeight = FontWeight.Bold) }
                }
            }
        } else {
            Button(
                onClick = { onShowRemoveConfirm(true) },
                modifier = Modifier.fillMaxWidth().height(50.dp),
                shape = RoundedCornerShape(12.dp),
                colors = ButtonDefaults.buttonColors(containerColor = CompatRed)
            ) {
                Icon(Icons.Default.Delete, null, modifier = Modifier.size(20.dp))
                Spacer(Modifier.width(8.dp))
                Text("Yamayı Kaldır", fontWeight = FontWeight.Bold, fontSize = 15.sp)
            }
        }
    } else {
        Button(
            onClick = {
                if (viewModel.isDownloading.value) return@Button
                viewModel.downloadAndInstallPatch(patch)
                onDismiss()
            },
            modifier = Modifier.fillMaxWidth().height(50.dp),
            shape = RoundedCornerShape(12.dp),
            colors = ButtonDefaults.buttonColors(containerColor = NintendoRed),
            enabled = !viewModel.isDownloading.value
        ) {
            Icon(Icons.Default.Download, null, modifier = Modifier.size(20.dp))
            Spacer(Modifier.width(8.dp))
            Text("İndir ve Kur", fontWeight = FontWeight.Bold, fontSize = 15.sp)
        }
    }
    TextButton(onClick = onDismiss, modifier = Modifier.fillMaxWidth()) {
        Text("Kapat", color = TextSecondary)
    }
}

@Composable
fun InfoRow(icon: androidx.compose.ui.graphics.vector.ImageVector, label: String, value: String) {
    Row(modifier = Modifier.padding(vertical = 2.dp), verticalAlignment = Alignment.Top) {
        Icon(icon, null, tint = TextSecondary, modifier = Modifier.size(15.dp).padding(top = 1.dp))
        Spacer(Modifier.width(5.dp))
        Text("$label: ", color = TextSecondary, fontSize = 12.sp)
        Text(value, color = TextPrimary, fontSize = 12.sp, modifier = Modifier.weight(1f))
    }
}

// ─── Installed Content Screen ─────────────────────────────────────────────────
@Composable
fun InstalledContentScreen(viewModel: AppViewModel, openDrawer: () -> Unit) {
    val context = LocalContext.current
    var selectedPatch by remember { mutableStateOf<Patch?>(null) }
    val configuration = LocalConfiguration.current
    val isWideScreen = configuration.screenWidthDp >= 600

    val gameFolderLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) {
            val flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION
            context.contentResolver.takePersistableUriPermission(uri, flags)
            viewModel.setGameFolderUri(uri)
        }
    }

    val installedPatches = viewModel.allPatches.filter { it.isInstalled || it.gameInstalled }

    Column(modifier = Modifier.fillMaxSize().background(DarkBg)) {
        YamaTopBar("Yüklü İçerikler", openDrawer, isWideScreen)

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(14.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            // Oyun Klasörü
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp),
                    colors = CardDefaults.cardColors(containerColor = CardBg),
                    border = BorderStroke(
                        1.dp,
                        if (viewModel.gameFolderUri.value != null) CompatGreen.copy(alpha = 0.4f) else Color(0xFF252535)
                    )
                ) {
                    Column(modifier = Modifier.padding(14.dp)) {
                        Text("Oyun Klasörü", color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 14.sp)
                        Spacer(Modifier.height(5.dp))
                        Text(
                            if (viewModel.gameFolderUri.value != null)
                                "✅ Oyun klasörü seçildi. Yaması olan oyunlar tespit edilecek."
                            else
                                "Oyun klasörünü seçerek, yaması bulunan oyunları otomatik olarak görüntüleyebilirsiniz.",
                            color = TextSecondary,
                            fontSize = 12.sp,
                            lineHeight = 17.sp
                        )
                        Spacer(Modifier.height(10.dp))
                        Button(
                            onClick = { gameFolderLauncher.launch(null) },
                            modifier = Modifier.fillMaxWidth().height(42.dp),
                            shape = RoundedCornerShape(10.dp),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = if (viewModel.gameFolderUri.value != null) Color(0xFF1A3020) else NintendoRed
                            )
                        ) {
                            Icon(Icons.Default.SportsEsports, null, modifier = Modifier.size(16.dp))
                            Spacer(Modifier.width(8.dp))
                            Text(
                                if (viewModel.gameFolderUri.value != null) "Değiştir" else "Oyun Klasörünü Seç",
                                fontWeight = FontWeight.SemiBold,
                                fontSize = 13.sp
                            )
                        }
                    }
                }
            }

            if (installedPatches.isNotEmpty()) {
                item {
                    Text(
                        "Tespit Edilen (${installedPatches.size})",
                        color = TextSecondary,
                        fontSize = 12.sp,
                        modifier = Modifier.padding(top = 4.dp)
                    )
                }
                items(installedPatches.size) { i ->
                    InstalledPatchRow(
                        patch = installedPatches[i],
                        imageLoader = viewModel.imageLoader,
                        onClick = { selectedPatch = installedPatches[i] }
                    )
                }
            } else {
                item {
                    Box(
                        Modifier.fillMaxWidth().padding(top = 28.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(
                            horizontalAlignment = Alignment.CenterHorizontally,
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                        ) {
                            Icon(Icons.Default.SearchOff, null, tint = TextSecondary, modifier = Modifier.size(44.dp))
                            Text(
                                "Tespit edilen oyun veya yama yok.",
                                color = TextSecondary,
                                textAlign = TextAlign.Center,
                                fontSize = 13.sp
                            )
                        }
                    }
                }
            }
        }
    }

    selectedPatch?.let { patch ->
        PatchDetailDialog(patch = patch, viewModel = viewModel, onDismiss = { selectedPatch = null })
    }
}

@Composable
fun InstalledPatchRow(patch: Patch, imageLoader: coil.ImageLoader, onClick: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth().clickable { onClick() },
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg)
    ) {
        Row(modifier = Modifier.padding(10.dp), verticalAlignment = Alignment.CenterVertically) {
            AsyncImage(
                model = ImageRequest.Builder(LocalContext.current)
                    .data("https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.titleId}.jpg")
                    .crossfade(true).build(),
                imageLoader = imageLoader,
                contentDescription = patch.name,
                contentScale = ContentScale.Crop,
                modifier = Modifier.size(52.dp).clip(RoundedCornerShape(8.dp)).background(Color(0xFF252535)),
                error = painterResource(id = R.drawable.splash)
            )
            Spacer(Modifier.width(10.dp))
            Column(Modifier.weight(1f)) {
                Text(
                    patch.name,
                    color = TextPrimary,
                    fontWeight = FontWeight.SemiBold,
                    fontSize = 13.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )
                Text(patch.yapimci.ifBlank { "Bilinmiyor" }, color = TextSecondary, fontSize = 11.sp)
            }
            when {
                patch.isInstalled   -> StatusChip("Yüklü", CompatGreen)
                patch.gameInstalled -> StatusChip("Yüklü Değil", CompatOrange)
            }
        }
    }
}

// ─── How To Install Screen ────────────────────────────────────────────────────
@Composable
fun HowToInstallScreen(openDrawer: () -> Unit) {
    val configuration = LocalConfiguration.current
    val isWideScreen = configuration.screenWidthDp >= 600

    Column(modifier = Modifier.fillMaxSize().background(DarkBg)) {
        YamaTopBar("Nasıl Kurulur", openDrawer, isWideScreen)
        LazyColumn(
            modifier = Modifier.fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            item {
                Text(
                    "Yama Nasıl Kurulur?",
                    color = TextPrimary,
                    fontWeight = FontWeight.Bold,
                    fontSize = 20.sp,
                    modifier = Modifier.padding(bottom = 8.dp)
                )
            }
            item { StepCard(1, "Oyun Klasörünü Seçin", "Soldaki menüden 'Yüklü' sekmesine gidin ve cihazınızdaki Oyun Klasörünü seçin.") }
            item { StepCard(2, "Yamayı İndirin", "'Yüklü' menüsünden yaması olan oyunların üstüne tıklayıp indirme işlemini başlatın.") }
            item { StepCard(3, "Eden Emülatörüne Geçin", "Eden emülatörünü açın. Yamasını kurduğunuz oyunun üstüne basılı tutun ve 'Add-ons' seçeneğine dokunun.") }
            item { StepCard(4, "Mod Kurulum Menüsü", "Açılan pencerede 'Install' butonuna, ardından 'Mods and cheats' seçeneğine tıklayın.") }
            item { StepCard(5, "Yamayı Seçin", "Cihazınızın ana dosya dizininden 'YamaNX / [Oyun Adı] / [Title ID]' klasörünü bulun ve içine girin.") }
            item { StepCard(6, "Kurulumu Tamamlayın", "İçeride 'romfs' klasörünü (ve varsa diğer dosyaları) gördüğünüzde, alt kısımdaki 'Bu klasörü kullan' butonuna basarak işlemi tamamlayın.") }
        }
    }
}

@Composable
fun StepCard(step: Int, title: String, desc: String) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(12.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg)
    ) {
        Row(modifier = Modifier.padding(16.dp)) {
            Box(
                modifier = Modifier
                    .size(32.dp)
                    .background(CompatGreen, CircleShape),
                contentAlignment = Alignment.Center
            ) {
                Text(step.toString(), color = Color.White, fontWeight = FontWeight.Bold)
            }
            Spacer(modifier = Modifier.width(16.dp))
            Column {
                Text(title, color = TextPrimary, fontWeight = FontWeight.SemiBold, fontSize = 16.sp)
                Spacer(modifier = Modifier.height(4.dp))
                Text(desc, color = TextSecondary, fontSize = 14.sp, lineHeight = 20.sp)
            }
        }
    }
}

// ─── About Screen ─────────────────────────────────────────────────────────────
@Composable
fun AboutScreen(openDrawer: () -> Unit) {
    val context = LocalContext.current
    val configuration = LocalConfiguration.current
    val isWideScreen = configuration.screenWidthDp >= 600
    fun openUrl(url: String) { context.startActivity(Intent(Intent.ACTION_VIEW, Uri.parse(url))) }

    val aboutText = """Merhaba, ben SertAy.

Hiçbir yazılım tecrübem olmadan, tamamen yapay zeka desteği ve büyük bir emekle geliştirdiğim YamaNX'in hatalarını çözerek sorunsuz bir sürüme ulaştırdım. Hata bildirimleri ve önerileriniz için bana her zaman ulaşabilirsiniz.

Yamaların yapımcısı ben değilim. Arşivde; Swatalk'ın 470'ten fazla ücretsiz yaması ve 200'den fazla Soner Çakır, Sinnerclown, Profesör Pikachu, Dede00, emre, davetsiz57 gibi pek çok çevirmen arkadaşın internetten bulduğum yamaları yer alıyor. Tespit edebildiğim tüm isimleri oyun seçim ekranına ekledim.

Sürekli güncellenen arşivimize eksik yamaların eklenmesi için bana, sıfırdan çeviri istekleriniz için ise doğrudan yapımcılara yazabilirsiniz.

Uygulamanın gelişimine destek olmak ve beni motive etmek isterseniz bağış yapabilirsiniz."""

    Column(modifier = Modifier.fillMaxSize().background(DarkBg)) {
        YamaTopBar("Hakkında", openDrawer, isWideScreen)
        LazyColumn(
            modifier = Modifier.fillMaxSize().background(DarkBg),
            contentPadding = PaddingValues(14.dp),
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(20.dp),
                    colors = CardDefaults.cardColors(containerColor = CardBg)
                ) {
                    Column(
                        modifier = Modifier.padding(20.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Image(
                            painter = painterResource(id = R.drawable.logo),
                            contentDescription = "Logo",
                            modifier = Modifier.fillMaxWidth(0.95f).height(130.dp),
                            contentScale = ContentScale.Fit
                        )
                        Spacer(Modifier.height(14.dp))
                        HorizontalDivider(color = Color(0xFF2A2A3A))
                        Spacer(Modifier.height(14.dp))
                        Text(aboutText, color = TextPrimary, fontSize = 13.sp, lineHeight = 20.sp)
                    }
                }
            }

            item {
                Text(
                    "Bağlantılar",
                    color = TextSecondary,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(horizontal = 2.dp)
                )
            }

            item {
                AboutPersonCard(
                    name = "SertAy",
                    role = "Geliştirici",
                    imageResId = R.drawable.photo_sertay,
                    url = "https://linktr.ee/yamanx",
                    onOpen = ::openUrl,
                    cropImage = false
                )
            }

            item {
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    AboutPersonCard(
                        name = "Swatalk",
                        role = "Çevirmen",
                        imageResId = R.drawable.photo_swatalk,
                        url = "https://discord.com/invite/xshWw2jBK6",
                        onOpen = ::openUrl,
                        modifier = Modifier.weight(1f)
                    )
                    AboutPersonCard(
                        name = "Soner Çakır",
                        role = "Çevirmen",
                        imageResId = R.drawable.photo_sonercakir,
                        url = "https://discord.com/invite/tB93ZUsRYE",
                        onOpen = ::openUrl,
                        modifier = Modifier.weight(1f)
                    )
                }
            }

            item {
                AboutPersonCard(
                    name = "SinnerClown",
                    role = "Çevirmen",
                    imageResId = R.drawable.photo_sinnerclown,
                    url = "https://sinnerclownceviri.net/forumlar/nintendo-tuerkce-yama.74/",
                    onOpen = ::openUrl,
                    imageHeight = 100
                )
            }

            item { Spacer(Modifier.height(8.dp)) }
        }
    }
}

@Composable
fun AboutPersonCard(
    name: String,
    role: String,
    imageResId: Int,
    url: String,
    onOpen: (String) -> Unit,
    modifier: Modifier = Modifier,
    cropImage: Boolean = true,
    imageHeight: Int = 130
) {
    Card(
        modifier = modifier.fillMaxWidth().clickable { onOpen(url) },
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = CardBg),
        border = BorderStroke(1.dp, Color(0xFF252535))
    ) {
        Column(modifier = Modifier.padding(12.dp), horizontalAlignment = Alignment.CenterHorizontally) {
            Image(
                painter = painterResource(id = imageResId),
                contentDescription = name,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(imageHeight.dp)
                    .clip(RoundedCornerShape(10.dp)),
                contentScale = if (cropImage) ContentScale.Crop else ContentScale.Fit
            )
            Spacer(Modifier.height(8.dp))
            Text(name, color = TextPrimary, fontWeight = FontWeight.Bold, fontSize = 14.sp, textAlign = TextAlign.Center)
            Text(role, color = TextSecondary, fontSize = 11.sp)
            Spacer(Modifier.height(6.dp))
            Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center) {
                Icon(Icons.Default.Link, null, tint = AccentBlue, modifier = Modifier.size(12.dp))
                Spacer(Modifier.width(4.dp))
                Text("Linke Git", color = AccentBlue, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            }
        }
    }
}

// ─── Download Dialog ──────────────────────────────────────────────────────────
@Composable
fun DownloadDialog(viewModel: AppViewModel) {
    val isFinished = viewModel.downloadFinished.value

    Dialog(
        onDismissRequest = { if (isFinished) viewModel.dismissDownload() },
        properties = DialogProperties(
            dismissOnBackPress = isFinished,
            dismissOnClickOutside = isFinished
        )
    ) {
        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = CardBg)
        ) {
            Column(
                modifier = Modifier.padding(24.dp),
                horizontalAlignment = Alignment.CenterHorizontally
            ) {
                if (!isFinished) {
                    val infiniteTransition = rememberInfiniteTransition(label = "spin")
                    val rotation by infiniteTransition.animateFloat(
                        initialValue = 0f,
                        targetValue = 360f,
                        animationSpec = infiniteRepeatable(tween(1000, easing = LinearEasing)),
                        label = "r"
                    )
                    Box(
                        Modifier.size(56.dp).border(3.dp, NintendoRed, CircleShape),
                        contentAlignment = Alignment.Center
                    ) {
                        Icon(Icons.Default.Download, null, tint = NintendoRed, modifier = Modifier.size(28.dp))
                    }
                } else {
                    val isSuccess = viewModel.downloadStatus.value.startsWith("✅")
                    Icon(
                        if (isSuccess) Icons.Default.CheckCircle else Icons.Default.Error,
                        null,
                        tint = if (isSuccess) CompatGreen else CompatRed,
                        modifier = Modifier.size(56.dp)
                    )
                }

                Spacer(Modifier.height(14.dp))
                Text(
                    viewModel.downloadStatus.value,
                    color = TextPrimary,
                    fontSize = 14.sp,
                    textAlign = TextAlign.Center,
                    lineHeight = 20.sp
                )
                Spacer(Modifier.height(14.dp))

                if (!isFinished) {
                    LinearProgressIndicator(
                        progress = { viewModel.downloadProgress.value },
                        modifier = Modifier.fillMaxWidth().height(7.dp).clip(RoundedCornerShape(4.dp)),
                        color = NintendoRed,
                        trackColor = Color(0xFF252535)
                    )
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "${(viewModel.downloadProgress.value * 100).toInt()}%",
                        color = TextSecondary,
                        fontSize = 12.sp
                    )
                } else {
                    Button(
                        onClick = { viewModel.dismissDownload() },
                        modifier = Modifier.fillMaxWidth().height(46.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = NintendoRed),
                        shape = RoundedCornerShape(12.dp)
                    ) {
                        Text("Kapat", fontWeight = FontWeight.Bold, fontSize = 15.sp)
                    }
                }
            }
        }
    }
}
