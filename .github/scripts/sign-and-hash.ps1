param([string]$Exe = 'FebiusYTMP3Downloader.exe')

$ErrorActionPreference = 'Stop'
$certificateBase64 = $env:WINDOWS_CERTIFICATE_BASE64
$certificatePassword = $env:WINDOWS_CERTIFICATE_PASSWORD
$status = 'Authenticode: unsigned (no signing certificate configured)'

if ($certificateBase64) {
  if (-not $certificatePassword) { throw 'WINDOWS_CERTIFICATE_PASSWORD is required when a certificate is configured.' }
  $pfx = Join-Path $env:RUNNER_TEMP 'febius-signing.pfx'
  try {
    [IO.File]::WriteAllBytes($pfx, [Convert]::FromBase64String($certificateBase64))
    $signTool = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe" |
      Sort-Object FullName -Descending | Select-Object -First 1
    if (-not $signTool) { throw 'signtool.exe was not found.' }
    & $signTool.FullName sign /fd SHA256 /td SHA256 /tr 'http://timestamp.digicert.com' /f $pfx /p $certificatePassword $Exe
    if ($LASTEXITCODE -ne 0) { throw "signtool failed with exit code $LASTEXITCODE." }
    $signature = Get-AuthenticodeSignature -LiteralPath $Exe
    if ($signature.Status -ne 'Valid') { throw "Authenticode verification failed: $($signature.Status)" }
    $status = "Authenticode: signed and valid ($($signature.SignerCertificate.Thumbprint))"
  } finally {
    Remove-Item -LiteralPath $pfx -Force -ErrorAction SilentlyContinue
  }
}

$status | Set-Content -LiteralPath 'SIGNING_STATUS.txt' -Encoding utf8
$hash = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash *$([IO.Path]::GetFileName($Exe))" | Set-Content -LiteralPath "$Exe.sha256" -Encoding utf8
