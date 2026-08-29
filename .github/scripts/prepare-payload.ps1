$ErrorActionPreference = 'Stop'

function Invoke-VerifiedDownload {
  param(
    [Parameter(Mandatory = $true)][string]$Uri,
    [Parameter(Mandatory = $true)][string]$OutFile,
    [string]$Sha256 = ''
  )

  $lastError = $null
  for ($attempt = 1; $attempt -le 4; $attempt++) {
    try {
      Remove-Item -LiteralPath $OutFile -Force -ErrorAction SilentlyContinue
      Invoke-WebRequest -Uri $Uri -OutFile $OutFile -ConnectionTimeoutSeconds 30 -OperationTimeoutSeconds 300
      if (-not (Test-Path -LiteralPath $OutFile) -or (Get-Item -LiteralPath $OutFile).Length -eq 0) {
        throw "Downloaded file is empty: $Uri"
      }

      if ($Sha256) {
        $actual = (Get-FileHash -LiteralPath $OutFile -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $Sha256.ToLowerInvariant()) {
          throw "SHA-256 mismatch for $Uri"
        }
      }
      return
    } catch {
      $lastError = $_
      if ($attempt -lt 4) {
        Start-Sleep -Seconds ([Math]::Min(5 * $attempt, 15))
      }
    }
  }

  throw "Download failed after 4 attempts: $Uri`n$lastError"
}

New-Item -ItemType Directory -Force payload | Out-Null
$ytDlpVersion = '2026.08.19'
$ytDlpSha256 = '66674953fe251b89f4d08c5f0e35e0728679bd67ab3d7d05c0562af101dd3e7a'
$ejsVersion = '0.8.0'
$ytDlpUrl = "https://github.com/yt-dlp/yt-dlp/releases/download/$ytDlpVersion/yt-dlp.exe"
Invoke-VerifiedDownload -Uri $ytDlpUrl -OutFile 'payload/yt-dlp.exe' -Sha256 $ytDlpSha256

$denoVersion = '2.9.5'
$denoSha256 = '171efab55ac6b9881fd53ee4c20f8bf3bb1340ffc618483746909014db12216a'
$denoUrl = "https://github.com/denoland/deno/releases/download/v$denoVersion/deno-x86_64-pc-windows-msvc.zip"
Invoke-VerifiedDownload -Uri $denoUrl -OutFile 'deno.zip' -Sha256 $denoSha256
Expand-Archive -LiteralPath 'deno.zip' -DestinationPath 'deno-dist' -Force
$deno = Get-ChildItem 'deno-dist' -Recurse -Filter 'deno.exe' | Select-Object -First 1
if (-not $deno) { throw 'Deno executable was not found.' }
Copy-Item $deno.FullName 'payload/deno.exe'

$ffmpegVersion = '9.0.1'
$ffmpegSha256 = 'fec81ae03971d9dd4be3ebe02e263bd2ec1d789483f931bdba5f5715e65da2e9'
$ffmpegUrl = "https://github.com/GyanD/codexffmpeg/releases/download/$ffmpegVersion/ffmpeg-$ffmpegVersion-essentials_build.zip"
Invoke-VerifiedDownload -Uri $ffmpegUrl -OutFile 'ffmpeg.zip' -Sha256 $ffmpegSha256
Expand-Archive -LiteralPath 'ffmpeg.zip' -DestinationPath 'ffmpeg-dist' -Force

$ffmpeg = Get-ChildItem 'ffmpeg-dist' -Recurse -Filter 'ffmpeg.exe' | Select-Object -First 1
$ffprobe = Get-ChildItem 'ffmpeg-dist' -Recurse -Filter 'ffprobe.exe' | Select-Object -First 1
if (-not $ffmpeg -or -not $ffprobe) { throw 'FFmpeg binaries were not found.' }
Copy-Item $ffmpeg.FullName 'payload/ffmpeg.exe'
Copy-Item $ffprobe.FullName 'payload/ffprobe.exe'

$chromaprintVersion = '1.6.1'
$chromaprintSha256 = '735d6182b38e9f364b84ce6f4ccd682c75e2851de89735711d6b762d12b92a4e'
$chromaprintUrl = "https://github.com/acoustid/chromaprint/releases/download/v$chromaprintVersion/chromaprint-fpcalc-$chromaprintVersion-windows-x86_64.zip"
Invoke-VerifiedDownload -Uri $chromaprintUrl -OutFile 'chromaprint.zip' -Sha256 $chromaprintSha256
Expand-Archive -LiteralPath 'chromaprint.zip' -DestinationPath 'chromaprint-dist' -Force
$fpcalc = Get-ChildItem 'chromaprint-dist' -Recurse -Filter 'fpcalc.exe' | Select-Object -First 1
if (-not $fpcalc) { throw 'Chromaprint fpcalc executable was not found.' }
Copy-Item $fpcalc.FullName 'payload/fpcalc.exe'
$fpcalcVersionOutput = & 'payload/fpcalc.exe' -version 2>&1 | Out-String
if ($LASTEXITCODE -ne 0 -or $fpcalcVersionOutput -notmatch [regex]::Escape($chromaprintVersion)) {
  throw 'Chromaprint fpcalc version check failed.'
}

Copy-Item 'LICENSE.txt' 'payload/APPLICATION_LICENSE.txt'

Invoke-VerifiedDownload -Uri 'https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.1/COPYING.GPLv3' -OutFile 'payload/GPL-3.0.txt'
Invoke-VerifiedDownload -Uri "https://raw.githubusercontent.com/yt-dlp/yt-dlp/$ytDlpVersion/LICENSE" -OutFile 'payload/YT-DLP-UNLICENSE.txt'
Invoke-VerifiedDownload -Uri "https://raw.githubusercontent.com/yt-dlp/yt-dlp/$ytDlpVersion/THIRD_PARTY_LICENSES.txt" -OutFile 'payload/YT-DLP-THIRD-PARTY-LICENSES.txt'
Invoke-VerifiedDownload -Uri "https://raw.githubusercontent.com/yt-dlp/ejs/$ejsVersion/LICENSE" -OutFile 'payload/YT-DLP-EJS-UNLICENSE.txt'
Invoke-VerifiedDownload -Uri 'https://raw.githubusercontent.com/davidbonnet/astring/v1.9.0/LICENSE' -OutFile 'payload/ASTRING-MIT-LICENSE.txt'
Invoke-VerifiedDownload -Uri 'https://raw.githubusercontent.com/meriyah/meriyah/v6.1.4/LICENSE.md' -OutFile 'payload/MERIYAH-ISC-LICENSE.txt'
Invoke-VerifiedDownload -Uri "https://raw.githubusercontent.com/denoland/deno/v$denoVersion/LICENSE.md" -OutFile 'payload/DENO-MIT-LICENSE.txt'
Invoke-VerifiedDownload -Uri "https://raw.githubusercontent.com/acoustid/chromaprint/v$chromaprintVersion/LICENSE.md" -OutFile 'payload/CHROMAPRINT-LICENSE.md'

$ffmpegLicense = & 'payload/ffmpeg.exe' -hide_banner -L 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Could not read the FFmpeg license information.' }
$ffmpegLicense | Set-Content -Encoding UTF8 'payload/FFMPEG-LICENSE.txt'

@(
  'Febius Downrush - Third-Party Notices',
  '',
  'The following command-line programs are distributed alongside the application',
  'and are launched as separate processes:',
  '',
  "1. yt-dlp Windows executable $ytDlpVersion (including yt-dlp-ejs $ejsVersion)",
  '   License: Unlicense; bundled dependencies retain their own licenses',
  '   Source: https://github.com/yt-dlp/yt-dlp',
  '   Details: YT-DLP-UNLICENSE.txt, YT-DLP-EJS-UNLICENSE.txt,',
  '            YT-DLP-THIRD-PARTY-LICENSES.txt, ASTRING-MIT-LICENSE.txt,',
  '            MERIYAH-ISC-LICENSE.txt',
  '',
  "2. Deno JavaScript runtime $denoVersion",
  '   License: MIT License',
  '   Source: https://github.com/denoland/deno',
  '   Details: DENO-MIT-LICENSE.txt',
  '',
  "3. FFmpeg and ffprobe $ffmpegVersion - Gyan Essentials Build",
  '   License: GNU General Public License version 3 (GPL-3.0)',
  '   Source: https://github.com/FFmpeg/FFmpeg',
  '   Build information: https://www.gyan.dev/ffmpeg/builds/',
  '   Details: FFMPEG-LICENSE.txt',
  '',
  "4. Chromaprint fpcalc $chromaprintVersion",
  '   License: LGPL 2.1 (Chromaprint source itself is MIT; see upstream license)',
  '   Source: https://github.com/acoustid/chromaprint',
  '   Details: CHROMAPRINT-LICENSE.md',
  '',
  'The complete GPL version 3 text is included as GPL-3.0.txt.',
  'These projects are independent of and are not endorsed by NokMyo.'
) | Set-Content -Encoding UTF8 'payload/THIRD_PARTY_NOTICES.txt'

@(
  "yt-dlp $ytDlpVersion  SHA256 $ytDlpSha256",
  "yt-dlp-ejs $ejsVersion (bundled in yt-dlp.exe)",
  "Deno $denoVersion  SHA256 $denoSha256",
  "FFmpeg Gyan Essentials $ffmpegVersion  SHA256 $ffmpegSha256",
  "Chromaprint fpcalc $chromaprintVersion  SHA256 $chromaprintSha256"
) | Set-Content -Encoding UTF8 'payload/DEPENDENCY_VERSIONS.txt'
