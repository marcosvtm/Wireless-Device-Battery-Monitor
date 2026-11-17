# Run with the x64 llvm-mingw bin directory on PATH.
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    & x86_64-w64-mingw32-windres --codepage=65001 dialog.rc -O coff -o dialog.o
    if ($LASTEXITCODE) { throw 'Resource compilation failed.' }
    & x86_64-w64-mingw32-clang++ -std=c++17 -Os -Wall -municode -mwindows -static -s '-Wl,--gc-sections' -ffunction-sections -fdata-sections -I../../src capture.cpp reports.cpp ../../src/hid.cpp dialog.o -lhid -lsetupapi -lshell32 -lole32 -luuid -o Teste-JBL-Quantum-350-v2.exe
    if ($LASTEXITCODE) { throw 'Compilation failed.' }
} finally { Pop-Location }
