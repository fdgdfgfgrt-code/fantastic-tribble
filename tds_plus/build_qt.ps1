# language: PowerShell, file: build_qt.ps1, runtime: PowerShell 5.1+, target: Windows 11/MSVC/Qt 6.10
param(
    [string]$QtSdk = (Join-Path $PSScriptRoot '..\.deps\Qt\6.10.3\msvc2022_64'),
    [switch]$SmokeTest
)
$ErrorActionPreference = 'Stop'
$source_dir = $PSScriptRoot
$build_dir = Join-Path $source_dir '.qt-build'
$deploy_dir = Join-Path $source_dir 'qt_app'
$QtSdk = [IO.Path]::GetFullPath($QtSdk)
if (-not (Test-Path -LiteralPath (Join-Path $QtSdk 'bin\windeployqt.exe'))) {
    throw "Qt SDK missing: $QtSdk"
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs_install = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs_install 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw 'MSVC x64 toolchain missing' }
$test_option = if ($SmokeTest) { 'ON' } else { 'OFF' }
$configure_cmd = 'call "{0}" >nul && set "VSLANG=1033" && chcp 65001 >nul && cmake -S "{1}" -B "{2}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="{3}" -DTDS_BUILD_UI_TEST={4} && cmake --build "{2}" --parallel 4' -f $vcvars, $source_dir, $build_dir, $QtSdk, $test_option
& $env:ComSpec /d /c $configure_cmd
if ($LASTEXITCODE -ne 0) { throw "Build failed: $LASTEXITCODE" }
New-Item -ItemType Directory -Path $deploy_dir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build_dir 'tds_plus_qt.exe') -Destination $deploy_dir -Force
& (Join-Path $QtSdk 'bin\windeployqt.exe') --release --verbose 0 --qmldir (Join-Path $source_dir 'qml') --no-translations --no-compiler-runtime --no-opengl-sw (Join-Path $deploy_dir 'tds_plus_qt.exe')
if ($LASTEXITCODE -ne 0) { throw "Deployment failed: $LASTEXITCODE" }
$license_dir = Join-Path $deploy_dir 'licenses'
New-Item -ItemType Directory -Path $license_dir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $source_dir 'assets\fonts\OFL.txt') -Destination (Join-Path $license_dir 'Geist-OFL.txt') -Force
Copy-Item -LiteralPath (Join-Path $source_dir 'assets\fonts\SOURCE.txt') -Destination (Join-Path $license_dir 'Geist-source.txt') -Force
$crt_dir = Get-ChildItem -LiteralPath (Join-Path $vs_install 'VC\Redist\MSVC') -Directory |
    Where-Object Name -Match '^\d' | Sort-Object Name -Descending | Select-Object -First 1
$crt_path = Join-Path $crt_dir.FullName 'x64\Microsoft.VC143.CRT'
if (-not (Test-Path -LiteralPath $crt_path)) { $crt_path = Join-Path $crt_dir.FullName 'x64\Microsoft.VC145.CRT' }
Get-ChildItem -LiteralPath $crt_path -Filter '*.dll' | Copy-Item -Destination $deploy_dir -Force
$shortcut_path = Join-Path $source_dir 'tds+ Qt.lnk'
$shortcut_shell = New-Object -ComObject WScript.Shell
$shortcut = $shortcut_shell.CreateShortcut($shortcut_path)
$shortcut.TargetPath = Join-Path $deploy_dir 'tds_plus_qt.exe'
$shortcut.WorkingDirectory = $source_dir
$shortcut.IconLocation = $shortcut.TargetPath + ',0'
$shortcut.Save()
Write-Output "Ready: $deploy_dir\tds_plus_qt.exe"
