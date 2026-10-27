// YamaNX PC - Modern Desktop Edition
// Created by SertAy with love for the Turkish Switch Community

let allPatches = [];
let favoritesSet = new Set();
let installedSet = new Set();
let activeTab = 'yamalar';
let activeEmulator = 'Ryujinx';
let emulatorsData = {};
let searchQuery = '';
let sortMode = 'az';
let activePatch = null;
let isDownloading = false;
let updateDownloadUrl = '';

const STAR_ICON_HTML = `<svg class="star-icon" viewBox="0 0 24 24"><polygon points="12 2 15.09 8.26 22 9.27 17 14.14 18.18 21.02 12 17.77 5.82 21.02 7 14.14 2 9.27 8.91 8.26 12 2"/></svg>`;

// DOM Elements
const topbarTitle = document.getElementById('topbar-title');
const topbarSub = document.getElementById('topbar-sub');
const topbarControls = document.getElementById('topbar-controls');
const searchInput = document.getElementById('search-input');
const searchClearBtn = document.getElementById('search-clear');
const sortSelect = document.getElementById('sort-select');

const badgeAllCount = document.getElementById('badge-all-count');
const badgeFavCount = document.getElementById('badge-fav-count');
const badgeInstalledCount = document.getElementById('badge-installed-count');

const gridYamalar = document.getElementById('grid-yamalar');
const gridFavoriler = document.getElementById('grid-favoriler');
const gridYuklu = document.getElementById('grid-yuklu');
const settingsContainer = document.getElementById('settings-container');

// Modals
const modalGame = document.getElementById('modal-game');
const modalDownload = document.getElementById('modal-download');
const modalUpdate = document.getElementById('modal-update');

// Initialize on Load
document.addEventListener('DOMContentLoaded', () => {
  setupEventListeners();
  fetchInitialData();
});

function setupEventListeners() {
  // Tab Navigation
  document.querySelectorAll('.nav-item').forEach(item => {
    item.addEventListener('click', () => {
      const tab = item.dataset.tab;
      switchTab(tab);
    });
  });

  // Emulator Switching
  document.querySelectorAll('.emu-chip').forEach(chip => {
    chip.addEventListener('click', () => {
      const emu = chip.dataset.emu;
      selectEmulator(emu);
    });
  });

  // Search Input with 50ms micro-debounce
  let searchTimer = null;
  searchInput.addEventListener('input', (e) => {
    searchQuery = e.target.value.trim().toLowerCase();
    searchClearBtn.style.display = searchQuery ? 'flex' : 'none';
    clearTimeout(searchTimer);
    searchTimer = setTimeout(() => {
      filterAndRender();
    }, 50);
  });

  searchClearBtn.addEventListener('click', () => {
    searchInput.value = '';
    searchQuery = '';
    searchClearBtn.style.display = 'none';
    searchInput.focus();
    filterAndRender();
  });

  // Sort Select
  sortSelect.addEventListener('change', (e) => {
    sortMode = e.target.value;
    filterAndRender();
    const contentArea = document.querySelector('.content-area') || window;
    if (contentArea.scrollTo) contentArea.scrollTo({ top: 0, behavior: 'smooth' });
    else contentArea.scrollTop = 0;
  });

  // Keyboard Shortcuts
  window.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      closeGameModal();
      closeUpdateModal();
      if (!isDownloading) closeDownloadModal();
    } else if ((e.ctrlKey && e.key === 'f') || e.key === '/') {
      if (document.activeElement !== searchInput) {
        e.preventDefault();
        searchInput.focus();
      }
    }
  });

  // Close modals on clicking overlay backdrop
  modalGame.addEventListener('click', (e) => {
    if (e.target === modalGame) closeGameModal();
  });
  modalUpdate.addEventListener('click', (e) => {
    if (e.target === modalUpdate) closeUpdateModal();
  });

  // Mouse Wheel & Drag Isolation on Featured Section
  // When cursor is over featured section, wheel scrolls ONLY the carousel horizontally and NEVER the main page
  const featuredTrack = document.getElementById('featured-track');
  const featuredSection = document.getElementById('featured-section');

  if (featuredSection && featuredTrack) {
    const handleCarouselWheel = (e) => {
      e.preventDefault();
      e.stopPropagation();
      const delta = (Math.abs(e.deltaY) > Math.abs(e.deltaX)) ? e.deltaY : e.deltaX;
      if (Math.abs(delta) > 0) {
        featuredTrack.scrollBy({
          left: delta > 0 ? 326 : -326,
          behavior: 'smooth'
        });
      }
    };

    featuredSection.addEventListener('wheel', handleCarouselWheel, { passive: false });
    featuredTrack.addEventListener('wheel', handleCarouselWheel, { passive: false });

    // Drag-to-scroll with mouse
    let isDragging = false;
    let startX = 0;
    let initialScrollLeft = 0;
    let dragMoved = false;

    featuredTrack.addEventListener('mousedown', (e) => {
      if (e.button !== 0) return;
      isDragging = true;
      dragMoved = false;
      startX = e.pageX;
      initialScrollLeft = featuredTrack.scrollLeft;
      featuredTrack.classList.add('is-dragging');
    });

    window.addEventListener('mousemove', (e) => {
      if (!isDragging) return;
      const dx = e.pageX - startX;
      if (Math.abs(dx) > 4) {
        dragMoved = true;
      }
      featuredTrack.scrollLeft = initialScrollLeft - dx;
    });

    window.addEventListener('mouseup', () => {
      if (isDragging) {
        isDragging = false;
        featuredTrack.classList.remove('is-dragging');
      }
    });

    featuredTrack.addEventListener('click', (e) => {
      if (dragMoved) {
        e.preventDefault();
        e.stopPropagation();
        dragMoved = false;
      }
    }, true);
  }

  // Event Delegation for Instant Clicks (No Freeze)
  ['grid-yamalar', 'grid-favoriler', 'grid-yuklu'].forEach(id => {
    const grid = document.getElementById(id);
    if (grid) {
      grid.addEventListener('click', (e) => {
        const favBtn = e.target.closest('.card-fav-btn');
        if (favBtn) {
          e.stopPropagation();
          const tid = favBtn.dataset.titleId;
          toggleFavorite(tid, favBtn);
          return;
        }
        const card = e.target.closest('.game-card');
        if (card) {
          const tid = card.dataset.titleId;
          const patch = allPatches.find(p => p.title_id === tid);
          if (patch) openGameModal(patch);
        }
      });
    }
  });

  if (featuredTrack) {
    featuredTrack.addEventListener('click', (e) => {
      const favBtn = e.target.closest('.card-fav-btn');
      if (favBtn) {
        e.stopPropagation();
        const tid = favBtn.dataset.titleId;
        toggleFavorite(tid, favBtn);
        return;
      }
      const card = e.target.closest('.featured-card');
      if (card) {
        const tid = card.dataset.titleId;
        const patch = allPatches.find(p => p.title_id === tid);
        if (patch) openGameModal(patch);
      }
    });
  }

  // Setup Frameless Window Controls
  setupWindowControls();
}

// Fetch Initial Data from Python
async function fetchInitialData() {
  try {
    if (window.eel) {
      const data = await eel.get_initial_data()();
      allPatches = data.patches || [];
      allPatches.forEach(p => {
        p._searchStr = `${p.name} ${p.title_id} ${p.yapimci || ''}`.toLowerCase();
      });
      favoritesSet = new Set(data.favorites || []);
      emulatorsData = data.emulators || {};
      activeEmulator = data.active_emulator || 'Ryujinx';
      
      updateInstalledSet(data.installed_ids || []);
      updateEmulatorUI();
      updateBadges();
      renderFeaturedCarousel();
      filterAndRender();

      // Check for updates automatically in the background
      setTimeout(() => {
        if (window.eel) {
          eel.check_for_updates(false)();
        }
      }, 1500);

    } else {
      console.warn("Eel bridge not detected. Running in mock/preview mode.");
    }
  } catch (err) {
    console.error("Error fetching initial data:", err);
  }
}

function updateInstalledSet(ids) {
  installedSet = new Set(ids);
  allPatches.forEach(p => {
    p.is_installed = installedSet.has(p.title_id);
  });
}

function updateBadges() {
  badgeAllCount.textContent = allPatches.length;
  badgeFavCount.textContent = favoritesSet.size;
  badgeInstalledCount.textContent = installedSet.size;
}

function updateEmulatorUI() {
  document.querySelectorAll('.emu-chip').forEach(chip => {
    const emu = chip.dataset.emu;
    chip.classList.toggle('active', emu === activeEmulator);
    const dot = document.getElementById(`dot-${emu.toLowerCase()}`);
    if (dot && emulatorsData[emu]) {
      dot.classList.toggle('detected', emulatorsData[emu].is_detected);
    }
  });
}

// Switch Active Tab
function switchTab(tabId) {
  if (activeTab === tabId) return;
  activeTab = tabId;

  // Update nav item active states
  document.querySelectorAll('.nav-item').forEach(item => {
    item.classList.toggle('active', item.dataset.tab === tabId);
  });

  // Update tab views
  document.querySelectorAll('.tab-view').forEach(view => {
    view.classList.toggle('active', view.id === `view-${tabId}`);
  });

  // Update Topbar
  const titles = {
    'yamalar': ['Tüm Yamalar', `${allPatches.length} yama listeleniyor`],
    'favoriler': ['Favoriler', `${favoritesSet.size} favori yama`],
    'yuklu': ['Yüklü İçerikler', `${installedSet.size} kurulu yama (${activeEmulator})`],
    'ayarlar': ['Ayarlar', 'Emülatör mod klasörleri ve uygulama güncellemeleri'],
    'hakkinda': ['Hakkında', 'YamaNX geliştiricisi ve topluluk bağlantıları']
  };

  const [title, sub] = titles[tabId] || ['YamaNX', ''];
  topbarTitle.textContent = title;
  topbarSub.textContent = sub;

  // Show/Hide search and sort in non-patch tabs
  const isPatchTab = ['yamalar', 'favoriler', 'yuklu'].includes(tabId);
  topbarControls.style.display = isPatchTab ? 'flex' : 'none';

  if (isPatchTab) {
    filterAndRender();
  } else if (tabId === 'ayarlar') {
    renderSettings();
  }
}

// Switch Emulator
async function selectEmulator(emuName) {
  activeEmulator = emuName;
  updateEmulatorUI();

  if (window.eel) {
    const res = await eel.set_active_emulator(emuName)();
    updateInstalledSet(res.installed_ids || []);
    updateBadges();
    filterAndRender();
  }
}

// Render Featured Carousel (Yeni Eklenenler)
function renderFeaturedCarousel() {
  const track = document.getElementById('featured-track');
  if (!track || allPatches.length === 0) return;

  const top15 = allPatches
    .slice()
    .sort((a, b) => (b.original_index || 0) - (a.original_index || 0))
    .slice(0, 15);

  let html = '';
  top15.forEach(patch => {
    const isFav = favoritesSet.has(patch.title_id);
    const coverUrl = `/covers/${patch.title_id}.jpg`;
    const fallbackCover = `https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.title_id}.jpg`;
    html += `
      <div class="featured-card" data-title-id="${patch.title_id}">
        <img class="featured-card-cover" src="${coverUrl}" decoding="async" onerror="if (this.src.indexOf('github') === -1) { this.src='${fallbackCover}'; } else { this.style.display='none'; }">
        <div class="featured-card-overlay">
          <div class="featured-card-top">
            <span class="featured-card-badge">✨ YENİ</span>
            <div class="card-fav-btn ${isFav ? 'active' : ''}" data-title-id="${patch.title_id}" title="Favori">${STAR_ICON_HTML}</div>
          </div>
          <div class="featured-card-bottom">
            <div class="featured-game-title" title="${escapeHtml(patch.name)}">${escapeHtml(patch.name)}</div>
            <div class="featured-game-sub">
              <span>✍️ ${escapeHtml(patch.yapimci || 'Bilinmiyor')}</span>
              <span class="game-size-pill">${escapeHtml(patch.size || 'v1.0')}</span>
            </div>
          </div>
        </div>
      </div>
    `;
  });
  track.innerHTML = html;
}

// Filter and Render Cards (Instant - All Patches & Covers Immediately Loaded)
function filterAndRender() {
  let list = allPatches;

  if (activeTab === 'favoriler') {
    list = list.filter(p => favoritesSet.has(p.title_id));
  } else if (activeTab === 'yuklu') {
    list = list.filter(p => installedSet.has(p.title_id));
  }

  // Search Filter (Ultra Fast O(1) Search via precomputed _searchStr)
  if (searchQuery) {
    list = list.filter(p => p._searchStr ? p._searchStr.includes(searchQuery) : (
      p.name.toLowerCase().includes(searchQuery) || 
      p.title_id.toLowerCase().includes(searchQuery) ||
      (p.yapimci && p.yapimci.toLowerCase().includes(searchQuery))
    ));
  }

  // Sorting
  list = sortPatches([...list], sortMode);

  // Show/Hide Featured Section
  const featSec = document.getElementById('featured-section');
  if (featSec) {
    const shouldShow = activeTab === 'yamalar' && !searchQuery;
    featSec.style.display = shouldShow ? 'block' : 'none';
  }

  // Update Section Header Count Badge
  const gridCountBadge = document.getElementById('grid-header-count');
  if (gridCountBadge) {
    gridCountBadge.textContent = `${list.length} yama`;
  }

  // Update Topbar Subtitle
  if (activeTab === 'yamalar') {
    topbarSub.textContent = `${list.length} yama listeleniyor`;
  } else if (activeTab === 'favoriler') {
    topbarSub.textContent = `${list.length} favori yama`;
  } else if (activeTab === 'yuklu') {
    topbarSub.textContent = `${list.length} kurulu yama (${activeEmulator})`;
  }

  const container = activeTab === 'yamalar' ? gridYamalar : 
                    activeTab === 'favoriler' ? gridFavoriler : gridYuklu;
  
  if (!container) return;

  if (list.length === 0) {
    container.innerHTML = `
      <div class="empty-state" style="grid-column: 1 / -1;">
        <div class="empty-state-icon">🔍</div>
        <div class="empty-state-title">Yama Bulunamadı</div>
        <div class="empty-state-desc">Arama kriterlerinize veya bu sekmeye uygun herhangi bir yama bulunamadı.</div>
      </div>
    `;
    return;
  }

  // Render all patches instantly with immediate cover photos (no hover required)
  let html = '';
  for (let i = 0; i < list.length; i++) {
    const patch = list[i];
    const isFav = favoritesSet.has(patch.title_id);
    const isInst = installedSet.has(patch.title_id);
    const isGb = patch.size && patch.size.toUpperCase().includes('GB');
    const coverUrl = `/covers/${patch.title_id}.jpg`;
    const fallbackCover = `https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.title_id}.jpg`;

    html += `
      <div class="game-card" data-title-id="${patch.title_id}">
        <div class="cover-wrapper">
          <div class="cover-placeholder"><span>🎮 ${escapeHtml(patch.name)}</span></div>
          <img class="cover-img" src="${coverUrl}" decoding="async" onerror="if (this.src.indexOf('github') === -1) { this.src='${fallbackCover}'; } else { this.style.display='none'; }">
          <div class="card-fav-btn ${isFav ? 'active' : ''}" data-title-id="${patch.title_id}" title="Favori">${STAR_ICON_HTML}</div>
          ${isInst ? '<div class="card-installed-badge">✓ YÜKLÜ</div>' : ''}
        </div>
        <div class="card-info">
          <div class="game-title" title="${escapeHtml(patch.name)}">${escapeHtml(patch.name)}</div>
          <div class="card-footer">
            <span class="game-author" title="${escapeHtml(patch.yapimci || 'Bilinmiyor')}">${escapeHtml(patch.yapimci || 'Bilinmiyor')}</span>
            <span class="game-size-pill ${isGb ? 'gb' : ''}">${escapeHtml(patch.size || patch.patch_version || 'v1.0')}</span>
          </div>
        </div>
      </div>
    `;
  }

  container.innerHTML = html;
}

function sortPatches(items, mode) {
  switch (mode) {
    case 'az':
      return items.sort((a, b) => a.name.localeCompare(b.name, 'tr'));
    case 'za':
      return items.sort((a, b) => b.name.localeCompare(a.name, 'tr'));
    case 'newest':
      return items.sort((a, b) => (b.original_index || 0) - (a.original_index || 0));
    case 'size_desc':
      return items.sort((a, b) => {
        const diff = (b.size_bytes || 0) - (a.size_bytes || 0);
        return diff !== 0 ? diff : a.name.localeCompare(b.name, 'tr');
      });
    case 'size_asc':
      return items.sort((a, b) => {
        const sa = a.size_bytes || 0;
        const sb = b.size_bytes || 0;
        if (sa === 0 && sb === 0) return a.name.localeCompare(b.name, 'tr');
        if (sa === 0) return 1;
        if (sb === 0) return -1;
        const diff = sa - sb;
        return diff !== 0 ? diff : a.name.localeCompare(b.name, 'tr');
      });
    default:
      return items;
  }
}

// Toggle Favorite (Instant Click - Zero Re-render Lag)
async function toggleFavorite(titleId, favBtn) {
  if (favoritesSet.has(titleId)) {
    favoritesSet.delete(titleId);
    if (favBtn) favBtn.classList.remove('active');
  } else {
    favoritesSet.add(titleId);
    if (favBtn) favBtn.classList.add('active');
  }

  // Update all instances of this star across the UI
  document.querySelectorAll(`.card-fav-btn[data-title-id="${titleId}"]`).forEach(btn => {
    btn.classList.toggle('active', favoritesSet.has(titleId));
  });

  updateBadges();

  if (activeTab === 'favoriler') {
    filterAndRender();
  }

  if (activePatch && activePatch.title_id === titleId) {
    updateModalFavState();
  }

  if (window.eel) {
    await eel.toggle_favorite(titleId)();
  }
}

// Game Detail Modal
function openGameModal(patch) {
  activePatch = patch;
  const isInst = installedSet.has(patch.title_id);
  const coverUrl = `/covers/${patch.title_id}.jpg`;
  const fallbackCover = `https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/${patch.title_id}.jpg`;

  const bannerImg = document.getElementById('modal-cover-img');
  bannerImg.style.display = 'block';
  bannerImg.src = coverUrl;
  bannerImg.onerror = () => {
    if (bannerImg.src.indexOf('github') === -1) {
      bannerImg.src = fallbackCover;
    } else {
      bannerImg.style.display = 'none';
    }
  };

  document.getElementById('modal-game-title').textContent = patch.name;
  document.getElementById('modal-tid').textContent = patch.title_id;
  document.getElementById('modal-yapimci').textContent = patch.yapimci || 'Bilinmiyor';
  document.getElementById('modal-version').textContent = patch.patch_version || 'v1.0';
  document.getElementById('modal-size').textContent = patch.size || 'Bilinmiyor';
  document.getElementById('modal-target-emu').textContent = activeEmulator;

  const installStatusElem = document.getElementById('modal-install-status');
  if (isInst) {
    installStatusElem.innerHTML = `<span style="color: var(--green);">● Bu Emülatörde Kurulu</span>`;
  } else {
    installStatusElem.innerHTML = `<span style="color: var(--text-faint);">Kurulu Değil</span>`;
  }

  const actionBtn = document.getElementById('modal-action-btn');
  const openFolderBtn = document.getElementById('modal-open-folder-btn');

  if (isInst) {
    actionBtn.textContent = 'YAMAYI KALDIR';
    actionBtn.className = 'btn btn-danger';
    openFolderBtn.style.display = 'inline-flex';
  } else {
    actionBtn.textContent = 'İNDİR VE KUR';
    actionBtn.className = 'btn btn-primary';
    openFolderBtn.style.display = 'none';
  }

  updateModalFavState();
  modalGame.classList.add('open');
}

function updateModalFavState() {
  if (!activePatch) return;
  const isFav = favoritesSet.has(activePatch.title_id);
  const favBtn = document.getElementById('modal-fav-btn');
  const favLabel = document.getElementById('modal-fav-label');

  favBtn.classList.toggle('active', isFav);
  favLabel.textContent = isFav ? 'Favorilerde' : 'Favorilere Ekle';
}

function toggleModalFavorite() {
  if (activePatch) {
    toggleFavorite(activePatch.title_id);
  }
}

function closeGameModal() {
  modalGame.classList.remove('open');
}

function copyModalTitleId() {
  if (!activePatch) return;
  navigator.clipboard.writeText(activePatch.title_id);
  const btn = document.getElementById('modal-copy-btn');
  const oldText = btn.textContent;
  btn.textContent = 'Kopyalandı!';
  btn.style.background = 'var(--green)';
  btn.style.color = '#000';
  setTimeout(() => {
    btn.textContent = oldText;
    btn.style.background = '';
    btn.style.color = '';
  }, 1800);
}

// Handle Modal Action (Install / Uninstall)
async function handleModalAction() {
  if (!activePatch) return;
  const isInst = installedSet.has(activePatch.title_id);

  if (isInst) {
    if (window.eel) {
      const res = await eel.uninstall_patch(activePatch.title_id, activeEmulator)();
      if (res.success) {
        installedSet.delete(activePatch.title_id);
        updateBadges();
        filterAndRender();
        closeGameModal();
      } else {
        alert(res.error || "Kaldırma işlemi başarısız oldu!");
      }
    }
  } else {
    closeGameModal();
    startDownload(activePatch);
  }
}

function openCurrentModFolder() {
  if (!activePatch || !window.eel) return;
  eel.open_mod_folder(activeEmulator, activePatch.title_id)();
}

// Download & Install Modal
function startDownload(patch) {
  isDownloading = true;
  document.getElementById('dl-title').textContent = `${patch.name} Kuruluyor`;
  document.getElementById('dl-status').textContent = 'İndirme başlatılıyor...';
  document.getElementById('dl-fill').style.width = '0%';
  document.getElementById('dl-fill').classList.remove('completed');
  document.getElementById('dl-pct-label').textContent = '%0';
  document.getElementById('dl-post-box').style.display = 'none';
  document.getElementById('dl-btn-cancel').style.display = 'inline-block';
  document.getElementById('dl-btn-close').style.display = 'none';

  modalDownload.classList.add('open');

  if (window.eel) {
    eel.start_install(patch.title_id, activeEmulator)();
  }
}

function cancelDownload() {
  if (window.eel && isDownloading) {
    eel.cancel_download()();
  }
  closeDownloadModal();
}

function closeDownloadModal() {
  isDownloading = false;
  modalDownload.classList.remove('open');
}

// Callbacks from Python for Install Progress
function on_install_progress(pct, statusText) {
  const fill = document.getElementById('dl-fill');
  const label = document.getElementById('dl-pct-label');
  const statusElem = document.getElementById('dl-status');

  if (fill) fill.style.width = `${pct}%`;
  if (label) label.textContent = `%${pct}`;
  if (statusElem) statusElem.textContent = statusText;
}

function on_install_complete(result) {
  isDownloading = false;
  const fill = document.getElementById('dl-fill');
  const label = document.getElementById('dl-pct-label');
  const statusElem = document.getElementById('dl-status');
  const postBox = document.getElementById('dl-post-box');
  const cancelBtn = document.getElementById('dl-btn-cancel');
  const closeBtn = document.getElementById('dl-btn-close');

  if (fill) {
    fill.style.width = '100%';
    fill.classList.add('completed');
  }
  if (label) label.textContent = '%100';
  if (statusElem) statusElem.textContent = `Kurulum Tamamlandı! (${result.files_copied || 0} dosya kopyalandı)`;

  if (result.post_install_hint) {
    const hint = document.getElementById('dl-post-hint');
    if (hint) hint.textContent = result.post_install_hint;
  }

  if (postBox) postBox.style.display = 'flex';
  if (cancelBtn) cancelBtn.style.display = 'none';
  if (closeBtn) closeBtn.style.display = 'inline-block';

  if (result.title_id) {
    installedSet.add(result.title_id);
    updateBadges();
    filterAndRender();
  }
}

function on_install_error(errorMsg) {
  isDownloading = false;
  const statusElem = document.getElementById('dl-status');
  if (statusElem) {
    statusElem.textContent = `HATA: ${errorMsg}`;
    statusElem.style.color = 'var(--red-bright)';
  }
  const cancelBtn = document.getElementById('dl-btn-cancel');
  if (cancelBtn) cancelBtn.textContent = 'Kapat';
}

// Callback for Update Check
function on_update_check_result(res) {
  const checkStatusElem = document.getElementById('update-check-status');

  if (res.has_update) {
    updateDownloadUrl = res.download_url;
    document.getElementById('update-modal-ver').textContent = `Yeni Sürüm: ${res.tag_name} (Mevcut: v${res.current_version})`;
    document.getElementById('update-modal-notes').textContent = res.release_notes || "Bu sürüm için detaylı not bulunmuyor.";
    modalUpdate.classList.add('open');

    if (checkStatusElem) {
      checkStatusElem.textContent = `Yeni sürüm mevcut: ${res.tag_name}`;
      checkStatusElem.style.color = 'var(--gold)';
    }
  } else if (res.is_manual) {
    if (res.error) {
      if (checkStatusElem) {
        checkStatusElem.textContent = `Kontrol başarısız: ${res.error}`;
        checkStatusElem.style.color = 'var(--red-bright)';
      }
    } else {
      if (checkStatusElem) {
        checkStatusElem.textContent = `Uygulamanız güncel! (v${res.current_version} PC)`;
        checkStatusElem.style.color = 'var(--green)';
      }
    }
  }
}

function closeUpdateModal() {
  modalUpdate.classList.remove('open');
}

function openUpdateDownload() {
  if (updateDownloadUrl) {
    openUrl(updateDownloadUrl);
  } else {
    openUrl("https://github.com/sertay1/YamaNX/releases");
  }
  closeUpdateModal();
}

// Render Settings View (Minimal: only Klasör Yolu)
function renderSettings() {
  settingsContainer.innerHTML = '';

  const emus = ['Ryujinx', 'Yuzu', 'Citron', 'Eden'];
  emus.forEach(emu => {
    const info = emulatorsData[emu] || { is_detected: false, mods_root: 'Seçilmedi / Bulunamadı' };
    const card = document.createElement('div');
    card.className = 'emu-card';

    card.innerHTML = `
      <div class="emu-card-left">
        <div class="emu-card-header">
          <span class="emu-card-name">${emu}</span>
          <span class="emu-status-chip ${info.is_detected ? 'ready' : 'missing'}">
            ${info.is_detected ? '🟢 Hazır & Bulundu' : '🔴 Klasör Bulunamadı (Manuel Seçim Yapın)'}
          </span>
        </div>
        <div class="emu-path-row">
          <span class="emu-path-label">Klasör Yolu:</span>
          <span class="emu-path-val">${escapeHtml(info.mods_root || 'Seçilmedi')}</span>
        </div>
      </div>
      <div class="emu-card-actions">
        <button class="btn btn-secondary" onclick="changeEmulatorFolder('${emu}')">Klasörü Değiştir</button>
        <button class="btn btn-green" onclick="openEmulatorFolder('${emu}')" ${!info.is_detected ? 'disabled' : ''}>Klasörü Aç</button>
      </div>
    `;

    settingsContainer.appendChild(card);
  });

  // Application Updates Card
  const updateCard = document.createElement('div');
  updateCard.className = 'update-card';
  updateCard.innerHTML = `
    <div class="update-card-left">
      <h3>YamaNX PC Güncellemeleri</h3>
      <p>Mevcut Sürüm: <strong>v2.1.0 PC</strong></p>
      <div id="update-check-status" class="update-status-msg" style="color: var(--text-muted);">
        Açılışta otomatik denetlenir.
      </div>
    </div>
    <button class="btn btn-primary" id="btn-check-updates" onclick="triggerManualUpdateCheck()">
      Güncellemeleri Denetle
    </button>
  `;
  settingsContainer.appendChild(updateCard);
}

function triggerManualUpdateCheck() {
  const statusElem = document.getElementById('update-check-status');
  if (statusElem) {
    statusElem.textContent = 'GitHub üzerinden denetleniyor...';
    statusElem.style.color = 'var(--blue)';
  }
  if (window.eel) {
    eel.check_for_updates(true)();
  }
}

async function changeEmulatorFolder(emu) {
  if (window.eel) {
    const res = await eel.select_emulator_folder(emu)();
    if (res.chosen) {
      emulatorsData[emu] = res.info;
      renderSettings();
      updateEmulatorUI();
      updateInstalledSet(res.installed_ids || []);
      updateBadges();
      filterAndRender();
    }
  }
}

function openEmulatorFolder(emu) {
  if (window.eel) {
    eel.open_emulator_folder(emu)();
  }
}

function openUrl(url) {
  if (window.eel) {
    eel.open_external_url(url)();
  } else {
    window.open(url, '_blank');
  }
}

function escapeHtml(str) {
  return String(str || '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function setupWindowControls() {
  const minBtn = document.getElementById('btn-win-min');
  const maxBtn = document.getElementById('btn-win-max');
  const closeBtn = document.getElementById('btn-win-close');
  const titlebar = document.getElementById('custom-titlebar');

  if (minBtn) {
    minBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (window.eel && eel.window_minimize) eel.window_minimize()();
    });
  }

  if (maxBtn) {
    maxBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (window.eel && eel.window_toggle_maximize) eel.window_toggle_maximize()();
    });
  }

  if (closeBtn) {
    closeBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (window.eel && eel.window_close) eel.window_close()();
    });
  }

  if (titlebar) {
    titlebar.addEventListener('mousedown', (e) => {
      if (e.target.closest('.titlebar-controls')) return;
      if (e.button === 0) {
        if (window.eel && eel.window_start_drag) eel.window_start_drag()();
      }
    });

    titlebar.addEventListener('dblclick', (e) => {
      if (e.target.closest('.titlebar-controls')) return;
      if (window.eel && eel.window_toggle_maximize) eel.window_toggle_maximize()();
    });
  }

  // Also support dragging from topbar & sidebar brand section
  const topbar = document.querySelector('.topbar');
  if (topbar) {
    topbar.addEventListener('mousedown', (e) => {
      if (e.target.closest('.search-box') || e.target.closest('.sort-select') || e.target.closest('button')) return;
      if (e.button === 0) {
        if (window.eel && eel.window_start_drag) eel.window_start_drag()();
      }
    });
    topbar.addEventListener('dblclick', (e) => {
      if (e.target.closest('.search-box') || e.target.closest('.sort-select') || e.target.closest('button')) return;
      if (window.eel && eel.window_toggle_maximize) eel.window_toggle_maximize()();
    });
  }

  const brand = document.querySelector('.brand-section');
  if (brand) {
    brand.addEventListener('mousedown', (e) => {
      if (e.button === 0) {
        if (window.eel && eel.window_start_drag) eel.window_start_drag()();
      }
    });
  }
}

function on_window_state_changed(isMax) {
  const iconMax = document.getElementById('icon-win-max');
  if (!iconMax) return;
  if (isMax) {
    iconMax.innerHTML = `<rect x="2.5" y="0.5" width="7" height="7" fill="none" stroke="currentColor"/><path d="M0.5 2.5v7h7" fill="none" stroke="currentColor"/>`;
  } else {
    iconMax.innerHTML = `<rect x="0.5" y="0.5" width="9" height="9" fill="none" stroke="currentColor"/>`;
  }
}

window.on_install_progress = on_install_progress;
window.on_install_complete = on_install_complete;
window.on_install_error = on_install_error;
window.on_update_check_result = on_update_check_result;
window.on_window_state_changed = on_window_state_changed;

if (window.eel) {
  eel.expose(on_install_progress);
  eel.expose(on_install_complete);
  eel.expose(on_install_error);
  eel.expose(on_update_check_result);
  eel.expose(on_window_state_changed);
}
