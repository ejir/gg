#!/bin/sh
# install.sh — grab the latest gg release into ~/.gg/bin and wire up PATH.
#
#   curl -fsSL https://raw.githubusercontent.com/ejir/gg/main/scripts/install.sh | sh
#
# environment knobs:
#   GG_REPO   github repo to pull from        (default ejir/gg)
#   GG_BIN    install directory               (default ~/.gg/bin)
#   GG_TAG    pin a release tag               (default: latest)
#   GG_NO_ACTIVE=1   do not touch your rc files

set -e

REPO=${GG_REPO:-ejir/gg}
BIN=${GG_BIN:-$HOME/.gg/bin}
TAG=${GG_TAG:-latest}

say() { printf '%s\n' "$*" >&2; }
have() { command -v "$1" >/dev/null 2>&1; }

fetch() { # fetch <url> <target>
  if have curl; then curl -fL --retry 3 --connect-timeout 20 -o "$2" "$1"
  elif have wget; then wget -q -O "$2" "$1"
  else say "need curl or wget"; exit 1
  fi
}

case "$(uname -s)" in
  Linux|Darwin|FreeBSD|OpenBSD|NetBSD) ;;
  *) say "unsupported system: $(uname -s) — on windows use scripts/install.ps1"; exit 1 ;;
esac

mkdir -p "$BIN"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

say "downloading gg ($TAG) from $REPO…"

# 1) direct asset from the latest release (or a pinned tag)
url="https://github.com/$REPO/releases/latest/download/gg"
[ "$TAG" != "latest" ] && url="https://github.com/$REPO/releases/download/$TAG/gg"

if ! fetch "$url" "$TMP/gg" 2>/dev/null || ! head -c 2 "$TMP/gg" 2>/dev/null | grep -q MZ; then
  # no release (yet): fall back to the prebuilt binary committed on main
  say "no release asset — trying the prebuilt binary on main"
  if ! fetch "https://raw.githubusercontent.com/$REPO/main/bin/gg" "$TMP/gg" 2>/dev/null \
     || ! head -c 2 "$TMP/gg" 2>/dev/null | grep -q MZ; then
    say "no prebuilt binary either — building from source with cosmocc instead"
    if ! have git; then say "git is required for the source build"; exit 1; fi
    git clone --depth 1 "https://github.com/$REPO" "$TMP/src" >&2
    ( cd "$TMP/src" && sh scripts/fetch-cosmocc.sh >&2 && PATH="$HOME/.cosmocc/bin:$PATH" make ape >&2 )
    cp "$TMP/src/bin/gg" "$TMP/gg" || { say "build failed"; exit 1; }
  fi
fi

chmod +x "$TMP/gg"
install -m 755 "$TMP/gg" "$BIN/gg" 2>/dev/null || { cp "$TMP/gg" "$BIN/gg"; chmod 755 "$BIN/gg"; }
say "installed: $BIN/gg"

if [ "${GG_NO_ACTIVE:-0}" != "1" ]; then
  "$BIN/gg" active || true
else
  say "add this to your shell profile yourself:  export PATH=$BIN:\$PATH"
fi

say ""
say "done — open a new shell and type:  gg"
