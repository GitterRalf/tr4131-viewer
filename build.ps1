param(
    [string]$QtKit,
    [string]$CompilerBin,
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64'
)

$ErrorActionPreference = 'Stop'
if ($Architecture -eq 'x86') {
    if (!$QtKit) { $QtKit = 'C:/Qt/Qt5.15.19/mingw81_32' }
    if (!$CompilerBin) { $CompilerBin = 'C:/Qt/Qt5.13.2/Tools/mingw810_32/bin' }
    $qtMajor = 5
    $triple = 'i686-w64-mingw32'
    $buildName = 'build-x86'
} else {
    if (!$QtKit) { $QtKit = 'C:/Qt/Qt6/6.11.0/mingw_64' }
    if (!$CompilerBin) { $CompilerBin = 'C:/Qt/Tools/mingw13_1_0/mingw64/bin' }
    $qtMajor = 6
    $triple = 'x86_64-w64-mingw32'
    $buildName = 'build-spectrogram'
}
$compiler = "$CompilerBin/g++.exe"
if ((& $compiler -dumpmachine) -ne $triple) {
    throw "Für $Architecture wird ein MinGW-Compiler für $triple benötigt."
}
$previousPath = $env:Path
$sourceDirectory = $PSScriptRoot
$subst = Join-Path $env:SystemRoot 'System32\subst.exe'
$mappedDrive = $null

try {
    if ($sourceDirectory -match '[^\x00-\x7F]') {
        # Keep native MinGW and Qt generator arguments ASCII on Windows.
        $taken = @(Get-PSDrive -PSProvider FileSystem | Select-Object -ExpandProperty Name)
        $letter = 'V','W','P','N','O','I','H','G','B' | Where-Object { $_ -notin $taken } | Select-Object -First 1
        if (!$letter) {
            throw 'Kein freier Laufwerksbuchstabe für den MinGW-Build.'
        }
        $drive = "${letter}:"
        & $subst $drive $sourceDirectory
        if ($LASTEXITCODE -ne 0) {
            throw 'Der temporäre Laufwerksalias für den Build konnte nicht eingerichtet werden.'
        }
        $mappedDrive = $drive
        $sourceDirectory = "$drive/"
    }

    $env:Path = "$CompilerBin;$QtKit/bin;$previousPath"
    $buildDirectory = Join-Path $sourceDirectory $buildName
    $cache = Join-Path $buildDirectory 'CMakeCache.txt'
    $configure = @('-S', $sourceDirectory, '-B', $buildDirectory, '-G', 'Ninja',
                   "-DCMAKE_PREFIX_PATH=$QtKit", "-DCMAKE_CXX_COMPILER=$compiler", "-DTR4131_QT_MAJOR=$qtMajor",
                   '-DCMAKE_BUILD_TYPE=Release')
    if (Test-Path -LiteralPath $cache) {
        $entry = Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$'
        $expected = $sourceDirectory.Replace('\', '/').TrimEnd('/')
        if (!$entry -or $entry.Matches[0].Groups[1].Value.TrimEnd('/') -ne $expected) {
            $configure += '--fresh'
        }
    }
    & cmake @configure
    if ($LASTEXITCODE -ne 0) { throw "CMake-Konfiguration fehlgeschlagen ($LASTEXITCODE)." }

    & cmake --build $buildDirectory --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen ($LASTEXITCODE)." }

    $executable = Join-Path $buildDirectory 'TR4131-Viewer.exe'
    if ($qtMajor -eq 6) {
        & "$QtKit/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw $executable
        if ($LASTEXITCODE -ne 0) { throw "Bereitstellen der Qt-Bibliotheken fehlgeschlagen ($LASTEXITCODE)." }
    } else {
        # Qt 5 windeployqt mistakes this MinGW kit's release plugins with debug info
        # for debug builds. Deploy the matching runtime and plugins directly.
        $qtModules = 'Core', 'Gui', 'Network', 'OpenGL', 'SerialPort', 'Svg', 'Widgets'
        foreach ($module in $qtModules) {
            Copy-Item -LiteralPath "$QtKit/bin/Qt5$module.dll" -Destination $buildDirectory
        }
        foreach ($dll in 'libgcc_s_dw2-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') {
            Copy-Item -LiteralPath "$CompilerBin/$dll" -Destination $buildDirectory
        }
        foreach ($dll in 'libEGL.dll', 'libGLESv2.dll') {
            if (Test-Path -LiteralPath "$QtKit/bin/$dll") {
                Copy-Item -LiteralPath "$QtKit/bin/$dll" -Destination $buildDirectory
            }
        }
        Set-Content -LiteralPath (Join-Path $buildDirectory 'qt.conf') -Encoding ascii -Value '[Paths]', 'Prefix=.', 'Plugins=.'
        $pluginRoot = Join-Path $QtKit 'plugins'
        foreach ($group in 'bearer', 'iconengines', 'imageformats', 'styles') {
            $pluginDir = Join-Path $pluginRoot $group
            if (Test-Path -LiteralPath $pluginDir -PathType Container) {
                $target = New-Item -ItemType Directory -Path (Join-Path $buildDirectory $group) -Force
                Get-ChildItem -LiteralPath $pluginDir -File -Filter '*.dll' |
                    Copy-Item -Destination $target.FullName
            }
        }
        $platforms = New-Item -ItemType Directory -Path (Join-Path $buildDirectory 'platforms') -Force
        Copy-Item -LiteralPath (Join-Path $pluginRoot 'platforms/qwindows.dll') -Destination $platforms.FullName
    }
}
finally {
    $env:Path = $previousPath
    if ($mappedDrive) {
        & $subst $mappedDrive /d
    }
}
