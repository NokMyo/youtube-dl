$ErrorActionPreference = 'Stop'

New-Item -ItemType Directory -Force payload | Out-Null
Invoke-WebRequest -Uri 'https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe' -OutFile 'payload/yt-dlp.exe'
Invoke-WebRequest -Uri 'https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip' -OutFile 'ffmpeg.zip'
Expand-Archive -LiteralPath 'ffmpeg.zip' -DestinationPath 'ffmpeg-dist' -Force

$ffmpeg = Get-ChildItem 'ffmpeg-dist' -Recurse -Filter 'ffmpeg.exe' | Select-Object -First 1
$ffprobe = Get-ChildItem 'ffmpeg-dist' -Recurse -Filter 'ffprobe.exe' | Select-Object -First 1
if (-not $ffmpeg -or -not $ffprobe) { throw 'FFmpeg binaries were not found.' }
Copy-Item $ffmpeg.FullName 'payload/ffmpeg.exe'
Copy-Item $ffprobe.FullName 'payload/ffprobe.exe'
Copy-Item 'LICENSE.txt' 'payload/APPLICATION_LICENSE.txt'

Invoke-WebRequest -Uri 'https://www.gnu.org/licenses/gpl-3.0.txt' -OutFile 'payload/GPL-3.0.txt'
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/LICENSE' -OutFile 'payload/YT-DLP-UNLICENSE.txt'
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/yt-dlp/yt-dlp/master/THIRD_PARTY_LICENSES.txt' -OutFile 'payload/YT-DLP-THIRD-PARTY-LICENSES.txt'

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
