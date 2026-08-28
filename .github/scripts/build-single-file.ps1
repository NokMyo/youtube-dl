param(
  [string]$CoreExe = 'build/Release/FebiusDownrush.exe',
  [string]$OutputExe = 'FebiusDownrush.exe'
)

$ErrorActionPreference = 'Stop'

$payloadRoot = (Resolve-Path 'payload').Path
$payloadManifest = (Get-ChildItem -LiteralPath $payloadRoot -File -Recurse |
  Sort-Object { $_.FullName.Substring($payloadRoot.Length).TrimStart('\').Replace('\', '/') } |
  ForEach-Object {
    $relativePath = $_.FullName.Substring($payloadRoot.Length).TrimStart('\').Replace('\', '/')
    $fileHash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$relativePath`n$fileHash`n"
  }) -join ''
$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
  $manifestBytes = [Text.UTF8Encoding]::new($false).GetBytes($payloadManifest)
  $payloadId = $sha256.ComputeHash($manifestBytes)
} finally {
  $sha256.Dispose()
}

Compress-Archive -Path 'payload/*' -DestinationPath 'tools-payload.zip' -CompressionLevel Optimal -Force
Copy-Item -LiteralPath $CoreExe -Destination $OutputExe -Force
$payload = [System.IO.File]::ReadAllBytes((Resolve-Path 'tools-payload.zip'))
$out = [System.IO.File]::Open($OutputExe, [System.IO.FileMode]::Append, [System.IO.FileAccess]::Write)
try {
  $out.Write($payload, 0, $payload.Length)
  $size = [BitConverter]::GetBytes([Int64]$payload.Length)
  $out.Write($size, 0, $size.Length)
  $out.Write($payloadId, 0, $payloadId.Length)
  $magic = [Text.Encoding]::ASCII.GetBytes('YTMP3PK2')
  $out.Write($magic, 0, $magic.Length)
} finally {
  $out.Dispose()
}
