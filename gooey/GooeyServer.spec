# -*- mode: python ; coding: utf-8 -*-
import os

a = Analysis(
    ['run_server.py'],
    pathex=[],
    binaries=[],
    datas=[(os.path.join(SPECPATH, 'app'), 'app')],
    hiddenimports=[
        'flask_socketio',
        'pythonosc',
        'engineio.async_drivers.threading',
        'socketio.async_drivers.threading',
        'serial',
        'serial.tools',
        'serial.tools.list_ports',
        'qrcode',
        'qrcode.image.svg',
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=['webview', 'pywebview'],
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
    name='gooey-server',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    console=True,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
# No COLLECT/BUNDLE — output is a single file: dist/gooey-server
