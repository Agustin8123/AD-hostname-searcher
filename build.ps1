param([string]$Toolchain = "$PSScriptRoot\.tools\w64devkit\bin")
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build, dist | Out-Null
$compiler = Join-Path $Toolchain 'g++.exe'
$resources = Join-Path $Toolchain 'windres.exe'
if (!(Test-Path $compiler)) { throw 'No se encontro g++. Pase -Toolchain C:\ruta\mingw\bin o compile con CMake y Visual Studio.' }
& $resources -I src src/app.rc -O coff -o build/app.res
if ($LASTEXITCODE) { throw 'Fallo la compilacion de recursos.' }
$flags = @('-std=c++17','-O2','-Wall','-Wextra','-Wpedantic','-DUNICODE','-D_UNICODE','-DWIN32_LEAN_AND_MEAN','-DNOMINMAX','-D_WIN32_WINNT=0x0A00','-static','-static-libgcc','-static-libstdc++')
& $compiler @flags tests/core_tests.cpp -o build/core_tests.exe
if ($LASTEXITCODE) { throw 'Fallo la compilacion de pruebas.' }
& ./build/core_tests.exe
if ($LASTEXITCODE) { throw 'Fallaron las pruebas.' }
& $compiler @flags tests/network_tests.cpp src/windows_services.cpp -o build/network_tests.exe -ladsiid -lactiveds -lole32 -loleaut32 -lws2_32 -liphlpapi
if ($LASTEXITCODE) { throw 'Fallo la compilacion de pruebas de red.' }
& ./build/network_tests.exe
if ($LASTEXITCODE) { throw 'Fallo la prueba de ping e IP local.' }
& $compiler @flags -municode -mwindows src/main.cpp src/windows_services.cpp build/app.res -o dist/ADHostnameSearcher.exe -lcomctl32 -luxtheme -lgdi32 -luser32 -ladsiid -lactiveds -lole32 -loleaut32 -lws2_32 -liphlpapi
if ($LASTEXITCODE) { throw 'Fallo la compilacion de la aplicacion.' }
Copy-Item readme.md dist/LEEME.md
Copy-Item LICENSE dist/LICENSE
Write-Output "Aplicacion lista: $PSScriptRoot\dist\ADHostnameSearcher.exe"
