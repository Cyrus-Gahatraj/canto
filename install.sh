#!/bin/sh
# Canto installer for Linux and macOS.
#
#   curl -fsSL https://raw.githubusercontent.com/Cyrus-Gahatraj/canto/main/install.sh | sh
#
# Installs LLVM 18, clang, gperf and Rust if they're missing, then builds
# canto with `cargo install` into ~/.cargo/bin.
# CANTO_BRANCH picks the branch to build (default: main).
set -eu

REPO="https://github.com/Cyrus-Gahatraj/canto"
BRANCH="${CANTO_BRANCH:-main}"

say() { printf '\033[1mcanto:\033[0m %s\n' "$1"; }
die() { printf '\033[31mcanto: %s\033[0m\n' "$1" >&2; exit 1; }
has() { command -v "$1" >/dev/null 2>&1; }

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    has sudo && SUDO="sudo"
fi

# ── system packages ────────────────────────────────────────────────────────
case "$(uname -s)" in
Darwin)
    has brew || die "Homebrew is required: https://brew.sh"
    say "installing llvm@18 and gperf with Homebrew"
    brew install llvm@18 gperf
    ;;
Linux)
    if has apt-get; then
        say "installing build tools with apt"
        $SUDO apt-get update
        $SUDO apt-get install -y build-essential curl git gperf zlib1g-dev libzstd-dev libxml2-dev
        if ! apt-cache show llvm-18-dev >/dev/null 2>&1; then
            say "LLVM 18 isn't in your apt sources, adding apt.llvm.org"
            $SUDO apt-get install -y lsb-release wget software-properties-common gnupg
            curl -fsSL https://apt.llvm.org/llvm.sh | $SUDO bash -s 18
        fi
        $SUDO apt-get install -y llvm-18-dev clang-18
    elif has dnf; then
        say "installing build tools with dnf"
        $SUDO dnf install -y gcc gcc-c++ git gperf zlib-devel libzstd-devel libxml2-devel
        $SUDO dnf install -y llvm18-devel clang18 || $SUDO dnf install -y llvm-devel clang
    else
        say "unknown package manager: install LLVM 18 (with llvm-config), clang, gperf, git and a C/C++ compiler yourself"
    fi
    ;;
*)
    die "unsupported system $(uname -s); on Windows use install.ps1"
    ;;
esac

# ── find LLVM 18's llvm-config ─────────────────────────────────────────────
LLVM_CONFIG=""
for c in \
    "$(brew --prefix llvm@18 2>/dev/null || true)/bin/llvm-config" \
    /usr/lib/llvm-18/bin/llvm-config \
    /usr/lib64/llvm18/bin/llvm-config \
    "$(command -v llvm-config-18 || true)" \
    "$(command -v llvm-config || true)"; do
    if [ -n "$c" ] && [ -x "$c" ] && "$c" --version | grep -q '^18\.'; then
        LLVM_CONFIG="$c"
        break
    fi
done
[ -n "$LLVM_CONFIG" ] || die "couldn't find LLVM 18's llvm-config"
LLVM_BIN="$("$LLVM_CONFIG" --bindir)"
say "using $LLVM_CONFIG"

# ── Rust ───────────────────────────────────────────────────────────────────
if [ -f "$HOME/.cargo/env" ]; then . "$HOME/.cargo/env"; fi
if ! has cargo; then
    say "installing Rust with rustup"
    curl -fsSL https://sh.rustup.rs | sh -s -- -y --no-modify-path
    . "$HOME/.cargo/env"
fi

# ── build and install ──────────────────────────────────────────────────────
# build.rs runs `llvm-config` from PATH, so LLVM 18 has to come first
say "building canto ($BRANCH), this takes a few minutes"
PATH="$LLVM_BIN:$PATH" cargo install --git "$REPO" --branch "$BRANCH" --force canto

say "installed $(command -v canto || echo "$HOME/.cargo/bin/canto")"
case ":$PATH:" in
*":$HOME/.cargo/bin:"*) ;;
*) say "add ~/.cargo/bin to your PATH:  export PATH=\"\$HOME/.cargo/bin:\$PATH\"" ;;
esac
if [ ! -x "$LLVM_BIN/clang" ] && ! has clang; then
    say "warning: no clang found; canto needs it to build programs"
fi
say "try it:  canto"
