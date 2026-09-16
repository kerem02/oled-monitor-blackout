$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$version = '2.1.0'
$buildDirectory = Join-Path $project 'build-msvc'
$releaseDirectory = Join-Path $project 'release'
$packageName = "OLED-Blackout-$version-win-x64"
$packageRoot = Join-Path $releaseDirectory "OLED-Blackout-$version"
$archive = Join-Path $releaseDirectory "$packageName.zip"

Push-Location $project
try {
    cmake -S . -B $buildDirectory -G 'Visual Studio 17 2022' -A x64
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build $buildDirectory --config Release --clean-first --parallel
    if ($LASTEXITCODE) { throw 'Release build failed' }
    ctest --test-dir $buildDirectory -C Release --verbose --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }

    New-Item -ItemType Directory -Force $releaseDirectory | Out-Null
    if (Test-Path $packageRoot) { Remove-Item $packageRoot -Recurse -Force }
    if (Test-Path $archive) { Remove-Item $archive -Force }
    cmake --install $buildDirectory --config Release --prefix $packageRoot
    if ($LASTEXITCODE) { throw 'Staging failed' }

    $exeHash = (Get-FileHash (Join-Path $packageRoot 'OLEDBlackout.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
    "$exeHash  OLEDBlackout.exe" | Set-Content (Join-Path $packageRoot 'SHA256SUMS.txt') -Encoding ascii
    Compress-Archive -Path $packageRoot -DestinationPath $archive -CompressionLevel Optimal
    $archiveHash = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLowerInvariant()
    "$archiveHash  $packageName.zip" | Set-Content "$archive.sha256" -Encoding ascii

    Write-Host "Release created: $archive"
    Write-Host "SHA-256: $archiveHash"
} finally {
    Pop-Location
}
