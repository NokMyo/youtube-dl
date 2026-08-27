param(
  [string]$CoreExe = 'build/Release/FebiusYTMP3Downloader.exe',
  [string]$OutputExe = 'FebiusYTMP3Downloader.exe'
)

$ErrorActionPreference = 'Stop'

Compress-Archive -Path 'payload/*' -DestinationPath 'tools-payload.zip' -CompressionLevel Optimal -Force
Copy-Item -LiteralPath $CoreExe -Destination $OutputExe -Force
$payload = [System.IO.File]::ReadAllBytes((Resolve-Path 'tools-payload.zip'))
$out = [System.IO.File]::Open($OutputExe, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write)
try {
  $out.Write($payload, 0, $payload.Length)
  $size = [BitConverter]::GetBytes([Int64]$payload.Length)
  $out.Write($size, 0, $size.Length)
  $magic = [Text.Encoding]::ASCII.GetBytes('YTMP3PK1')
  $out.Write($magic, 0, $magic.Length)
} finally {
  $out.Dispose()
}
