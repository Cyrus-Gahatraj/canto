# Canto installer for Windows.
#
#   irm https://raw.githubusercontent.com/Cyrus-Gahatraj/canto/main/install.ps1 | iex
#
# Canto needs LLVM 18 with llvm-config, which has no simple native Windows
# install, so canto runs inside WSL. This installs it there with install.sh
# and adds a `canto` command to Windows that forwards to it.
# $env:CANTO_BRANCH picks the branch to build (default: main).
$ErrorActionPreference = "Stop"

$branch = if ($env:CANTO_BRANCH) { $env:CANTO_BRANCH } else { "main" }
$script = "https://raw.githubusercontent.com/Cyrus-Gahatraj/canto/$branch/install.sh"

function Say($msg) { Write-Host "canto: $msg" }

# ── WSL ─────────────────────────────────────────────────────────────────────
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw "canto: WSL isn't available. It needs Windows 10 2004+ or Windows 11."
}
$distros = (wsl.exe --list --quiet) -replace "`0", "" | Where-Object { $_.Trim() }
if (-not $distros) {
    Say "installing WSL with Ubuntu (needs admin, and maybe a reboot)"
    wsl.exe --install -d Ubuntu-24.04
    Say "once Ubuntu is set up (open it once to create your user), run this installer again"
    exit 0
}

# ── install inside WSL ──────────────────────────────────────────────────────
Say "installing canto inside WSL"
wsl.exe -- bash -lc "curl -fsSL $script | CANTO_BRANCH=$branch sh"
if ($LASTEXITCODE -ne 0) { throw "canto: the WSL install failed" }

# ── Windows command that forwards to WSL ───────────────────────────────────
$bin = Join-Path $env:LOCALAPPDATA "canto\bin"
New-Item -ItemType Directory -Force -Path $bin | Out-Null
Set-Content -Path (Join-Path $bin "canto.cmd") -Encoding ASCII -Value "@wsl.exe ~/.cargo/bin/canto %*"

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if (($userPath -split ";") -notcontains $bin) {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$bin", "User")
    Say "added $bin to your PATH; open a new terminal to use it"
}

Say "installed. Try:  canto run hello.ct   (use / in paths, not \)"
