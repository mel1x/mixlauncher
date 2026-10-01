# Downloads the pinned Everything build into third_party\everything (it ships inside the installer).
# The zip is checked against its SHA-256, and Everything.exe against voidtools' code signature.
param([switch]$force)
$ErrorActionPreference = 'Stop'
$version = '1.5.0.1423b'
$sha256 = 'A218CEBFFCCFD9DFAA5AA4BBB665D6E4B5533049B1B9CA7E3828385B5CB8142C'
$dest = Join-Path $PSScriptRoot '..\third_party\everything'
$exe = Join-Path $dest 'Everything.exe'
if ((Test-Path $exe) -and -not $force -and (Get-Item $exe).VersionInfo.FileVersion -eq $version) { exit 0 }

$tmp = Join-Path ([IO.Path]::GetTempPath()) "mixlauncher-everything-$version"
New-Item -ItemType Directory -Force $tmp | Out-Null
$zip = Join-Path $tmp 'Everything.zip'
$ProgressPreference = 'SilentlyContinue'
Write-Host "Downloading Everything $version..."
Invoke-WebRequest "https://www.voidtools.com/Everything-$version.x64.zip" -OutFile $zip -UseBasicParsing
if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $sha256) { throw "Everything-$version.x64.zip: SHA-256 mismatch" }
Expand-Archive $zip (Join-Path $tmp 'x') -Force
$sig = Get-AuthenticodeSignature (Join-Path $tmp 'x\Everything.exe')
if ($sig.Status -ne 'Valid' -or $sig.SignerCertificate.Subject -notmatch 'CN=voidtools') { throw 'Everything.exe: bad signature' }
New-Item -ItemType Directory -Force $dest | Out-Null
Copy-Item (Join-Path $tmp 'x\Everything.exe') $exe -Force
Remove-Item $tmp -Recurse -Force
Write-Host "third_party\everything\Everything.exe $version"
