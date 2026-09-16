#!/usr/bin/env python3
"""Audit the final PE import table. Requires MinGW objdump (no Python packages)."""
import argparse
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('executable', type=Path)
parser.add_argument('--objdump', default='x86_64-w64-mingw32-objdump')
args = parser.parse_args()
if args.executable.stat().st_size < 1024 or args.executable.read_bytes()[:2] != b'MZ':
    raise SystemExit('FAIL: missing or invalid PE executable')
result = subprocess.run([args.objdump, '-p', str(args.executable)], check=True, text=True, capture_output=True)
text = result.stdout
if 'file format pei-x86-64' not in text or not re.search(r'Subsystem\s+00000002\b', text):
    raise SystemExit('FAIL: expected x64 Windows GUI executable')
forbidden = [
    'OpenProcess', 'ReadProcessMemory', 'WriteProcessMemory', 'VirtualAllocEx',
    'CreateRemoteThread', 'SetWindowsHookExA', 'SetWindowsHookExW', 'SendInput',
    'mouse_event', 'keybd_event', 'SetCursorPos', 'ClipCursor',
    'WinHttpOpen', 'InternetOpenA', 'InternetOpenW', 'WSAStartup', 'connect',
]
found = [name for name in forbidden if re.search(r'\b' + re.escape(name) + r'\b', text)]
dlls = re.findall(r'DLL Name:\s*(\S+)', text)
allowed = {'advapi32.dll','comctl32.dll','gdi32.dll','kernel32.dll','msvcrt.dll','ole32.dll','shell32.dll','user32.dll','wtsapi32.dll'}
extra = [dll for dll in dlls if dll.lower() not in allowed]
if not dlls:
    raise SystemExit('FAIL: no import descriptors found')
if found or extra:
    raise SystemExit(f'FAIL: review forbidden imports={found}, unexpected dependencies={extra}')
for flag in ['HIGH_ENTROPY_VA', 'DYNAMIC_BASE', 'NX_COMPAT']:
    if flag not in text:
        raise SystemExit('FAIL: missing PE mitigation ' + flag)
print('PASS import/dependency/mitigation audit')
print('Windows system DLLs: ' + ', '.join(dlls))
# Import absence is one review aid, not proof against dynamically resolved behavior.
# Review application source and build inputs as well; this is not anti-cheat certification.
