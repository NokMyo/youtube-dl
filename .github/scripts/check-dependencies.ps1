$ErrorActionPreference = 'Stop'

$payloadScript = Get-Content -LiteralPath '.github/scripts/prepare-payload.ps1' -Raw
function Read-PinnedVersion([string]$name) {
  $pattern = '\$' + [regex]::Escape($name) + "\s*=\s*'(?<version>[^']+)'"
  $match = [regex]::Match($payloadScript, $pattern, [Text.RegularExpressions.RegexOptions]::IgnoreCase)
  if (-not $match.Success) { throw "Pinned version was not found: $name" }
  return $match.Groups['version'].Value
}

$headers = @{ Authorization = "Bearer $env:GITHUB_TOKEN"; 'User-Agent' = 'Febius-Downrush-dependency-check' }
$dependencies = @(
  @{ Name = 'yt-dlp'; Variable = 'ytDlpVersion'; Repository = 'yt-dlp/yt-dlp'; Prefix = '' },
  @{ Name = 'yt-dlp-ejs'; Variable = 'ejsVersion'; Repository = 'yt-dlp/ejs'; Prefix = '' },
  @{ Name = 'Deno'; Variable = 'denoVersion'; Repository = 'denoland/deno'; Prefix = 'v' },
  @{ Name = 'FFmpeg Gyan'; Variable = 'ffmpegVersion'; Repository = 'GyanD/codexffmpeg'; Prefix = '' },
  @{ Name = 'Chromaprint'; Variable = 'chromaprintVersion'; Repository = 'acoustid/chromaprint'; Prefix = 'v' }
)

$outdated = @()
foreach ($dependency in $dependencies) {
  $pinned = Read-PinnedVersion $dependency.Variable
  $release = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/$($dependency.Repository)/releases/latest"
  $latest = [string]$release.tag_name
  $expected = "$($dependency.Prefix)$pinned"
  if ($latest -ne $expected) {
    $outdated += "$($dependency.Name): pinned $expected, latest $latest"
  }
}

if ($outdated.Count) {
  $outdated | ForEach-Object { "- $_" } | Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
  throw "Pinned dependencies need review: $($outdated -join '; ')"
}

'All pinned dependencies match their latest stable releases.' |
  Add-Content -LiteralPath $env:GITHUB_STEP_SUMMARY -Encoding utf8
