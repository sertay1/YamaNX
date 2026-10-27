# -*- mode: python ; coding: utf-8 -*-

a = Analysis(
    ['yamanx_pc.py'],
    pathex=[],
    binaries=[],
    datas=[('web', 'web'), ('romfs', 'romfs'), ('yamalar.txt', '.'), ('icon.ico', '.')],
    hiddenimports=['bottle', 'bottle_websocket', 'gevent', 'geventwebsocket', 'webview', 'clr_loader', 'pythonnet'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['pygame'],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='yamanx_pc',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=['icon.ico'],
)
