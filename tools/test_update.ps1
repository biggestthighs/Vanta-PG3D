$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\update.ps1"
function Assert($value, $message) { if (-not $value) { throw $message } }
$root = Join-Path ([IO.Path]::GetTempPath()) ('Vanta-test-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $root | Out-Null
$escaped = $false
try { $null = Get-SafeUpdatePath $root '..\outside.exe' } catch { $escaped = $true }
Assert $escaped 'Traversal should be rejected'
$package = Join-Path $root 'Vanta-v0.1.7'
New-Item -ItemType Directory -Path "$package\sounds","$package\configs" -Force | Out-Null
foreach ($file in @('Vanta.exe','pg3d_core.dll','offsets.json','update.ps1','version.txt')) {
    [IO.File]::WriteAllText((Join-Path $package $file), 'new-content')
}
[IO.File]::WriteAllText("$package\sounds\bell.wav", 'bundled-sound')
[IO.File]::WriteAllText("$package\sounds\new.wav", 'new-sound')
[IO.File]::WriteAllText("$package\config.json", 'should-not-copy')
[IO.File]::WriteAllText("$package\configs\mine.json", 'should-not-copy')
$sums = foreach ($file in @('Vanta.exe','pg3d_core.dll')) { (Get-FileHash "$package\$file").Hash.ToLowerInvariant() + '  ' + $file }
$sums | Set-Content "$package\SHA256SUMS.txt" -Encoding ascii
$zip = Join-Path $root 'release.zip'
Compress-Archive -LiteralPath $package -DestinationPath $zip
$verified = Expand-VerifiedPackage $zip (Join-Path $root 'expanded')
Assert (Test-Path "$verified\Vanta.exe") 'Verified extraction failed'
$target = Join-Path $root 'installed'
New-Item -ItemType Directory -Path "$target\sounds","$target\configs" -Force | Out-Null
[IO.File]::WriteAllText("$target\Vanta.exe", 'old-content')
[IO.File]::WriteAllText("$target\pg3d_core.dll", 'old-dll')
[IO.File]::WriteAllText("$target\config.json", 'my-settings')
[IO.File]::WriteAllText("$target\configs\mine.json", 'my-profile')
[IO.File]::WriteAllText("$target\sounds\bell.wav", 'my-sound')
Install-VantaPackage $verified $target (Join-Path $root 'backup')
Assert ([IO.File]::ReadAllText("$target\Vanta.exe") -eq 'new-content') 'Exe was not replaced'
Assert ([IO.File]::ReadAllText("$target\config.json") -eq 'my-settings') 'Config overwritten'
Assert ([IO.File]::ReadAllText("$target\configs\mine.json") -eq 'my-profile') 'Preset overwritten'
Assert ([IO.File]::ReadAllText("$target\sounds\bell.wav") -eq 'my-sound') 'Existing sound overwritten'
Assert (Test-Path "$target\sounds\new.wav") 'New sound not installed'
[IO.File]::WriteAllText("$target\Vanta.exe", 'before-failed-update')
[IO.File]::WriteAllText("$target\offsets.json", 'before-failed-offsets')
$locked = [IO.File]::Open("$target\pg3d_core.dll", 'Open', 'Read', 'None')
$failed = $false
try { Install-VantaPackage $verified $target (Join-Path $root 'rollback') } catch { $failed = $true } finally { $locked.Dispose() }
Assert $failed 'Locked DLL did not fail'
Assert ([IO.File]::ReadAllText("$target\offsets.json") -eq 'before-failed-offsets') 'Rollback did not restore earlier replacements'
Assert ([IO.File]::ReadAllText("$target\Vanta.exe") -eq 'before-failed-update') 'Failed update did not restore exe'
[IO.File]::WriteAllText("$package\Vanta.exe", 'corrupted')
Compress-Archive -LiteralPath $package -DestinationPath "$root\corrupt.zip"
$failed = $false
try { $null = Expand-VerifiedPackage "$root\corrupt.zip" "$root\corrupt" } catch { $failed = $true }
Assert $failed 'Corrupt archive was accepted'
Add-Type -AssemblyName System.IO.Compression.FileSystem
$bad = [IO.Compression.ZipFile]::Open("$root\unsafe.zip", 'Create')
$null = $bad.CreateEntry('../escape.exe'); $bad.Dispose()
$failed = $false
try { $null = Expand-VerifiedPackage "$root\unsafe.zip" "$root\unsafe" } catch { $failed = $true }
Assert $failed 'Unsafe archive was accepted'
Write-Output 'PASS: checksums, traversal rejection, in-place install, personal files and rollback'
Write-Output "Test fixtures: $root"
