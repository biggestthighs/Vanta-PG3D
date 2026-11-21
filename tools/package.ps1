param(
    [string]$Version = '0.1.0',
    [string]$BuildDir = 'build',
    [string]$OutputDir = 'dist'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$compiled = Join-Path (Join-Path $root $BuildDir) 'Release'
$output = Join-Path $root $OutputDir
foreach ($name in @('Vanta.exe', 'pg3d_core.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $compiled $name))) { throw "Build first: missing $name" }
}
$stage = Join-Path $root ('build\package-' + [guid]::NewGuid().ToString('N'))
$bundle = Join-Path $stage "Vanta-v$Version"
$source = Join-Path $bundle 'source'
New-Item -ItemType Directory -Path $source -Force | Out-Null
New-Item -ItemType Directory -Path $output -Force | Out-Null
foreach ($name in @('Vanta.exe', 'pg3d_core.dll')) {
    Copy-Item -LiteralPath (Join-Path $compiled $name) -Destination $bundle
}
$sounds = Join-Path $root 'assets\sounds'
if (@(Get-ChildItem -LiteralPath $sounds -Filter '*.wav' -File).Count -eq 0) { throw 'Sound assets are missing; refusing to package an empty sounds folder' }
Copy-Item -LiteralPath $sounds -Destination (Join-Path $bundle 'sounds') -Recurse
New-Item -ItemType Directory -Path (Join-Path $bundle 'configs') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'assets\models') -Destination (Join-Path $bundle 'models') -Recurse
foreach ($name in @('offsets.json', 'README.md', 'QUICKSTART.txt')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $bundle
}
Copy-Item -LiteralPath (Join-Path $root 'third_party') -Destination $bundle -Recurse
foreach ($directory in @('src', 'loader', 'dumper', 'tools', 'assets', 'third_party', '.github')) {
    Copy-Item -LiteralPath (Join-Path $root $directory) -Destination (Join-Path $source $directory) -Recurse
}
foreach ($name in @('CMakeLists.txt', 'offsets.json', 'README.md', 'QUICKSTART.txt', '.gitignore')) {
    Copy-Item -LiteralPath (Join-Path $root $name) -Destination $source
}
$sums = foreach ($name in @('Vanta.exe', 'pg3d_core.dll')) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $bundle $name)).Hash.ToLowerInvariant()
    "$hash  $name"
}
$sums | Set-Content -LiteralPath (Join-Path $bundle 'SHA256SUMS.txt') -Encoding ascii
$archive = Join-Path $output "Vanta-v$Version-win64-with-source.zip"
Compress-Archive -LiteralPath $bundle -DestinationPath $archive -CompressionLevel Optimal -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash.ToLowerInvariant()
"$hash  $([IO.Path]::GetFileName($archive))" | Set-Content -LiteralPath (Join-Path $output 'SHA256SUMS.txt') -Encoding ascii
Write-Output $archive
