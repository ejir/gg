# install.ps1 — get gg onto a windows box.
#
#   irm https://raw.githubusercontent.com/ejir/gg/main/scripts/install.ps1 | iex
#
# The same single file works everywhere: gg is an "Actually Portable
# Executable" that windows runs as a normal .exe.

param(
  [string]$Repo = $(if ($env:GG_REPO) { $env:GG_REPO } else { "ejir/gg" }),
  [string]$Bin  = $(if ($env:GG_BIN) { $env:GG_BIN } else { Join-Path $HOME ".gg\bin" }),
  [string]$Tag  = $(if ($env:GG_TAG) { $env:GG_TAG } else { "latest" }),
  [switch]$NoActive
)

$ErrorActionPreference = "Stop"

$url = if ($Tag -eq "latest") {
  "https://github.com/$Repo/releases/latest/download/gg.exe"
} else {
  "https://github.com/$Repo/releases/download/$Tag/gg.exe"
}

New-Item -ItemType Directory -Force -Path $Bin | Out-Null
$target = Join-Path $Bin "gg.exe"
Write-Host "downloading gg ($Tag) from $Repo ..."
Invoke-WebRequest -Uri $url -OutFile $target
# a copy called gg.com lets cmd.exe find it too
Copy-Item -Force $target (Join-Path $Bin "gg.com")

if (-not $NoActive) {
  & $target active
} else {
  Write-Host "add this to your PATH manually: $Bin"
}

Write-Host ""
Write-Host "done — open a new terminal and type: gg"
