# Builds dist\WirelessBattery-Setup-<version>.exe and dist\WirelessBattery-<version>-portable.zip.
# With -PortableOnly it skips the installer entirely (no Inno Setup needed) and only builds the zip.
# Needs: WirelessBattery.exe already built (make / build.bat); installer mode also needs Inno Setup 6 or 7.
# HeadsetControl is compiled into WirelessBattery.exe (see third_party/HeadsetControl), nothing to download here.
param([string]$Version = "1.0.0", [switch]$PortableOnly)
$Version = $Version -replace '^v', ''
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
Set-Location $root

if (-not (Test-Path (Join-Path $root "WirelessBattery.exe"))) { throw "Build WirelessBattery.exe first (make or build.bat)." }

if (-not $PortableOnly) {
    $iscc = foreach ($v in 7, 6) {
        foreach ($base in ${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA\Programs") { "$base\Inno Setup $v\ISCC.exe" }
    }
    $iscc = $iscc | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $iscc) { $iscc = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source }
    if (-not $iscc) { throw "Inno Setup not found (https://jrsoftware.org/isdl.php)." }
    & $iscc "/DAppVersion=$Version" (Join-Path $PSScriptRoot "WirelessBattery.iss")
    if ($LASTEXITCODE -ne 0) { throw "ISCC failed." }
}

# Portable zip: same files, no installer.
$stage = Join-Path $root "build\portable"
Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item WirelessBattery.exe, installer\THIRD_PARTY_NOTICES.txt $stage
Copy-Item LICENSE (Join-Path $stage "LICENSE.txt")
New-Item -ItemType Directory -Force dist | Out-Null
Compress-Archive -Path "$stage\*" -DestinationPath "dist\WirelessBattery-$Version-portable.zip" -Force
if ($PortableOnly) {
    Write-Host "Done: dist\WirelessBattery-$Version-portable.zip"
} else {
    Write-Host "Done: dist\WirelessBattery-Setup-$Version.exe and dist\WirelessBattery-$Version-portable.zip"
}
