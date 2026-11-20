param(
    [string]$QtKit = 'C:/Qt/Qt6/6.11.0/mingw_64',
    [string]$CompilerBin = 'C:/Qt/Tools/mingw13_1_0/mingw64/bin'
)

$ErrorActionPreference = 'Stop'
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
    $buildDirectory = Join-Path $sourceDirectory 'build-spectrogram'
    $cache = Join-Path $buildDirectory 'CMakeCache.txt'
    $configure = @('-S', $sourceDirectory, '-B', $buildDirectory, '-G', 'Ninja',
                   "-DCMAKE_PREFIX_PATH=$QtKit", "-DCMAKE_CXX_COMPILER=$CompilerBin/g++.exe",
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
    & "$QtKit/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw $executable
    if ($LASTEXITCODE -ne 0) { throw "Bereitstellen der Qt-Bibliotheken fehlgeschlagen ($LASTEXITCODE)." }
}
finally {
    $env:Path = $previousPath
    if ($mappedDrive) {
        & $subst $mappedDrive /d
    }
}
