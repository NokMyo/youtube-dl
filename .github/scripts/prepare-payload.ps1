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
Invoke-VerifiedDownload -Uri 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe' -OutFile 'payload/yt-dlp.exe'
Invoke-VerifiedDownload -Uri 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS' -OutFile 'yt-dlp-SHA2-256SUMS.txt'

$ytDlpLine = Get-Content -LiteralPath 'yt-dlp-SHA2-256SUMS.txt' |
  Where-Object { $_ -match '^(?<hash>[0-9a-fA-F]{64})\s+\*?yt-dlp\.exe\s*$' } |
  Select-Object -First 1
if (-not $ytDlpLine) { throw 'yt-dlp.exe checksum was not found.' }
$null = $ytDlpLine -match '^(?<hash>[0-9a-fA-F]{64})'
$ytDlpActual = (Get-FileHash -LiteralPath 'payload/yt-dlp.exe' -Algorithm SHA256).Hash
if ($ytDlpActual -ne $Matches.hash) { throw 'yt-dlp.exe SHA-256 verification failed.' }

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
Copy-Item 'LICENSE.txt' 'payload/APPLICATION_LICENSE.txt'

Invoke-VerifiedDownload -Uri 'https://raw.githubusercontent.com/FFmpeg/FFmpeg/n9.0.1/COPYING.GPLv3' -OutFile 'payload/GPL-3.0.txt'
Invoke-VerifiedDownload -Uri 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/LICENSE' -OutFile 'payload/YT-DLP-UNLICENSE.txt'
Invoke-VerifiedDownload -Uri 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/THIRD_PARTY_LICENSES.txt' -OutFile 'payload/YT-DLP-THIRD-PARTY-LICENSES.txt'

$ffmpegLicense = & 'payload/ffmpeg.exe' -hide_banner -L 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'Could not read the FFmpeg license information.' }
$ffmpegLicense | Set-Content -Encoding UTF8 'payload/FFMPEG-LICENSE.txt'

@(
  'Seowol YT MP3 Downloader - Third-Party Notices',
  '',
  'The following command-line programs are distributed alongside the application',
  'and are launched as separate processes:',
  '',
  '1. yt-dlp Windows executable',
  '   License: GNU General Public License version 3 or later (GPL-3.0-or-later)',
  '   Source: https://github.com/yt-dlp/yt-dlp',
  '   Details: YT-DLP-THIRD-PARTY-LICENSES.txt',
  '',
  '2. FFmpeg and ffprobe - Gyan Essentials Build',
  '   License: GNU General Public License version 3 (GPL-3.0)',
  '   Source: https://github.com/FFmpeg/FFmpeg',
  '   Build information: https://www.gyan.dev/ffmpeg/builds/',
  '   Details: FFMPEG-LICENSE.txt',
  '',
  'The complete GPL version 3 text is included as GPL-3.0.txt.',
  'These projects are independent of and are not endorsed by NokMyo.'
) | Set-Content -Encoding UTF8 'payload/THIRD_PARTY_NOTICES.txt'
