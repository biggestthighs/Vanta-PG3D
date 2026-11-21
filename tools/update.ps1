param(
    [string]$InstallDir = $PSScriptRoot,
    [string]$CurrentVersion = '',
    [int]$LauncherPid = 0,
    [switch]$Quiet,
    [switch]$NoRestart
)
$ErrorActionPreference = 'Stop'

function Get-SafeUpdatePath([string]$Root, [string]$Relative) {
    $base = [IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    $path = [IO.Path]::GetFullPath((Join-Path $base $Relative))
    if (-not $path.StartsWith($base, [StringComparison]::OrdinalIgnoreCase)) { throw 'Update path escapes its folder.' }
    return $path
}
function Test-VantaLoaded {
    foreach ($game in @(Get-Process -Name 'Pixel Gun 3D','PixelGun3D' -ErrorAction SilentlyContinue)) {
        try {
            if (@($game.Modules | Where-Object ModuleName -eq 'pg3d_core.dll').Count) { return $true }
        } catch { throw 'Cannot inspect the running game. Close it before updating.' }
    }
    return $false
}
function Expand-VerifiedPackage([string]$Zip, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName -match '(^[/\\]|:|(^|[/\\])\.\.([/\\]|$))') { throw 'Unsafe archive entry.' }
            $null = Get-SafeUpdatePath $Destination $entry.FullName
        }
    } finally { $archive.Dispose() }
    Expand-Archive -LiteralPath $Zip -DestinationPath $Destination
    $roots = @(Get-ChildItem -LiteralPath $Destination -Directory)
    if ($roots.Count -ne 1) { throw 'Unexpected release archive layout.' }
    $root = $roots[0].FullName
    foreach ($file in @('Vanta.exe','pg3d_core.dll','offsets.json','SHA256SUMS.txt','update.ps1','version.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)) { throw "Release is missing $file" }
    }
    $verified = @{}
    foreach ($line in [IO.File]::ReadAllLines((Join-Path $root 'SHA256SUMS.txt'))) {
        if ($line -notmatch '^([0-9a-fA-F]{64})\s+(.+)$') { throw 'Invalid package checksum list.' }
        $expected = $Matches[1]; $name = $Matches[2]
        $path = Get-SafeUpdatePath $root $name
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $expected) { throw "Checksum mismatch: $name" }
        $verified[$name] = $true
    }
    foreach ($name in @('Vanta.exe','pg3d_core.dll')) {
        if (-not $verified.ContainsKey($name)) { throw "Missing checksum for $name" }
    }
    return $root
}
function Install-VantaPackage([string]$Package, [string]$Target, [string]$Backup) {
    # Keep personal files and sounds. Journal every replacement for rollback.
    $changes = [Collections.Generic.List[object]]::new()
    try {
        foreach ($file in Get-ChildItem -LiteralPath $Package -Recurse -File) {
            $relative = $file.FullName.Substring($Package.TrimEnd('\').Length + 1)
            if ($relative -eq 'config.json' -or $relative.StartsWith('configs\', [StringComparison]::OrdinalIgnoreCase)) { continue }
            $destination = Get-SafeUpdatePath $Target $relative
            if ($relative.StartsWith('sounds\', [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $destination)) { continue }
            $existed = Test-Path -LiteralPath $destination
            $saved = Get-SafeUpdatePath $Backup $relative
            if ($existed) {
                # An open DLL/executable must fail before its replacement.
                $lock = [IO.File]::Open($destination, 'Open', 'ReadWrite', 'None'); $lock.Dispose()
                New-Item -ItemType Directory -Path (Split-Path -Parent $saved) -Force | Out-Null
                Copy-Item -LiteralPath $destination -Destination $saved
            }
            $changes.Add([pscustomobject]@{ Destination=$destination; Saved=$saved; Existed=$existed })
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $destination -Force
        }
    } catch {
        $reason = $_
        for ($i=$changes.Count-1; $i -ge 0; $i--) {
            $change = $changes[$i]
            if ($change.Existed) { Copy-Item -LiteralPath $change.Saved -Destination $change.Destination -Force }
            elseif (Test-Path -LiteralPath $change.Destination) { Remove-Item -LiteralPath $change.Destination -Force }
        }
        throw $reason
    }
}
function Show-UpdateMessage([string]$Text) {
    if ($Quiet) { Write-Output $Text; return }
    Add-Type -AssemblyName System.Windows.Forms
    [Windows.Forms.MessageBox]::Show($Text, 'Vanta updater') | Out-Null
}
function Invoke-VantaUpdate {
    $target = [IO.Path]::GetFullPath($InstallDir)
    if (-not $CurrentVersion) { $CurrentVersion = [IO.File]::ReadAllText((Join-Path $target 'version.txt')).Trim() }
    if (-not (Test-Path -LiteralPath (Join-Path $target 'Vanta.exe'))) { throw 'Install folder does not contain Vanta.exe.' }
    $stage = Join-Path ([IO.Path]::GetTempPath()) ('Vanta-update-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        if ($LauncherPid -gt 0) { Wait-Process -Id $LauncherPid -Timeout 30 -ErrorAction SilentlyContinue }
        if (Test-VantaLoaded) { throw 'Unload Vanta with END, or close the game, before updating.' }
        [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
        $headers = @{ 'User-Agent'='Vanta-Updater'; 'Accept'='application/vnd.github+json' }
        $releases = Invoke-RestMethod -Uri 'https://api.github.com/repos/biggestthighs/Vanta-PG3D/releases?per_page=30' -Headers $headers -TimeoutSec 30
        $release = $releases | Where-Object { -not $_.draft -and $_.tag_name -match '^v\d+\.\d+\.\d+$' } |
            Sort-Object { [version]($_.tag_name.TrimStart('v')) } -Descending | Select-Object -First 1
        if (-not $release) { throw 'No supported GitHub release is available.' }
        if ([version]($release.tag_name.TrimStart('v')) -le [version]$CurrentVersion) {
            Show-UpdateMessage "Vanta $CurrentVersion is up to date."
            return
        }
        $asset = $release.assets | Where-Object name -eq ('Vanta-' + $release.tag_name + '-win64-with-source.zip') | Select-Object -First 1
        $sums = $release.assets | Where-Object name -eq 'SHA256SUMS.txt' | Select-Object -First 1
        if (-not $asset -or -not $sums) { throw 'Release is missing its ZIP or download checksum. No files changed.' }
        foreach ($url in @($asset.browser_download_url, $sums.browser_download_url)) {
            if (-not $url.StartsWith('https://github.com/biggestthighs/Vanta-PG3D/releases/download/')) { throw 'Unexpected update download URL.' }
        }
        $zip = Join-Path $stage 'release.zip'; $sumFile = Join-Path $stage 'SHA256SUMS.txt'
        Invoke-WebRequest -UseBasicParsing -Uri $asset.browser_download_url -OutFile $zip -TimeoutSec 180
        Invoke-WebRequest -UseBasicParsing -Uri $sums.browser_download_url -OutFile $sumFile -TimeoutSec 30
        $expected = $null
        foreach ($line in [IO.File]::ReadAllLines($sumFile)) {
            if ($line -match '^([0-9a-fA-F]{64})\s+(.+)$' -and $Matches[2] -eq $asset.name) { $expected = $Matches[1] }
        }
        if (-not $expected -or (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $expected) { throw 'Download checksum mismatch. No files changed.' }
        $package = Expand-VerifiedPackage $zip (Join-Path $stage 'extracted')
        if ([IO.File]::ReadAllText((Join-Path $package 'version.txt')).Trim() -ne $release.tag_name.TrimStart('v')) { throw 'Package version does not match release.' }
        if (Test-VantaLoaded) { throw 'Vanta was loaded while downloading. Unload it before updating.' }
        Install-VantaPackage $package $target (Join-Path $stage 'backup')
        # Keep one ZIP beside the one installation; never create version folders.
        try { Copy-Item -LiteralPath $zip -Destination (Join-Path (Split-Path -Parent $target) 'Vanta.zip') -Force }
        catch { Show-UpdateMessage 'Vanta updated, but the sibling Vanta.zip could not be refreshed.' }
        Show-UpdateMessage ('Updated to ' + $release.tag_name + '. Your configs and existing sounds were preserved.')
    } catch { Show-UpdateMessage ('Update failed: ' + $_.Exception.Message) }
    finally {
        $safeStage = [IO.Path]::GetFullPath($stage)
        $tempRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\') + '\'
        if ($safeStage.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -and
            (Split-Path -Leaf $safeStage) -match '^Vanta-update-[0-9a-f]{32}$') { Remove-Item -LiteralPath $safeStage -Recurse -Force }
        if (-not $NoRestart) { Start-Process -FilePath (Join-Path $target 'Vanta.exe') -WorkingDirectory $target }
    }
}
if ($MyInvocation.InvocationName -ne '.') { Invoke-VantaUpdate }
