#!/bin/sh
# fetch-cosmocc.sh — install the cosmocc toolchain that builds gg.
#
# usage: scripts/fetch-cosmocc.sh [version] [prefix]
#
# The script tries three places, in order, and stops at the first that works:
#   1. the official github release asset (cosmocc-<v>.zip)
#   2. the npm mirror @x-cmd-pkg/cosmocc (handy when github assets are blocked)
#   3. cosmo.zip
#
# Afterwards, add <prefix>/bin to PATH (or re-run with a prefix you already
# have on PATH) and run `make ape`.

set -e

VERSION=${1:-4.0.2}
PREFIX=${2:-$HOME/.cosmocc}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "installing cosmocc $VERSION into $PREFIX" >&2

have() { command -v "$1" >/dev/null 2>&1; }

download() { # download <url> <target>
  if have curl; then
    curl -fL --retry 3 --connect-timeout 20 -o "$2" "$1"
  elif have wget; then
    wget -q -O "$2" "$1"
  else
    echo "need curl or wget" >&2
    exit 1
  fi
}

unpack() { # unpack <archive> <destdir>
  case "$1" in
    *.zip)
      if have unzip; then unzip -q -o "$1" -d "$2"
      else python3 -c 'import sys,zipfile;zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$1" "$2"
      fi
      ;;
    *.tar.gz|*.tgz) tar xzf "$1" -C "$2" ;;
    *.tar.xz)       tar xJf "$1" -C "$2" ;;
    *) echo "unknown archive: $1" >&2; exit 1 ;;
  esac
}

ok=0

# 1) github release
if [ "$ok" = 0 ]; then
  echo "  [1/3] github release" >&2
  if download "https://github.com/jart/cosmopolitan/releases/download/$VERSION/cosmocc-$VERSION.zip" "$TMP/cosmocc.zip" 2>/dev/null; then
    mkdir -p "$TMP/x" && unpack "$TMP/cosmocc.zip" "$TMP/x" && ok=1
  fi
fi

# 2) npm mirror
if [ "$ok" = 0 ]; then
  echo "  [2/3] npm mirror @x-cmd-pkg/cosmocc" >&2
  meta=$(download "https://registry.npmjs.org/@x-cmd-pkg/cosmocc" "$TMP/meta.json" 2>/dev/null && cat "$TMP/meta.json" || true)
  if [ -n "$meta" ]; then
    url=$(printf '%s' "$meta" | tr ',' '\n' | grep -o '"tarball":"[^"]*"' | head -1 | cut -d'"' -f4)
    if [ -n "$url" ] && download "$url" "$TMP/pkg.tgz" 2>/dev/null; then
      mkdir -p "$TMP/y" && unpack "$TMP/pkg.tgz" "$TMP/y"
      for inner in "$TMP"/y/package/dist/*.tar.xz; do
        [ -e "$inner" ] || continue
        mkdir -p "$TMP/z" && unpack "$inner" "$TMP/z"
      done
      ok=1
    fi
  fi
fi

# 3) cosmo.zip
if [ "$ok" = 0 ]; then
  echo "  [3/3] cosmo.zip" >&2
  if download "https://cosmo.zip/pub/cosmocc/cosmocc-$VERSION.zip" "$TMP/cosmocc3.zip" 2>/dev/null; then
    mkdir -p "$TMP/w" && unpack "$TMP/cosmocc3.zip" "$TMP/w" && ok=1
  fi
fi

if [ "$ok" = 0 ]; then
  echo "could not download cosmocc — grab it manually from" >&2
  echo "  https://github.com/jart/cosmopolitan/releases" >&2
  echo "and set COSMOCC=/path/to/cosmocc/bin/cosmocc when running make" >&2
  exit 1
fi

# find the directory that contains bin/cosmocc
src=""
for cand in "$TMP"/x "$TMP"/x/* "$TMP"/y/package "$TMP"/z/* "$TMP"/z/*/* "$TMP"/w "$TMP"/w/*; do
  if [ -x "$cand/bin/cosmocc" ]; then src=$cand; break; fi
done
if [ -z "$src" ]; then
  src=$(find "$TMP" -name cosmocc -type f -perm -u+x 2>/dev/null | head -1 | xargs -r dirname | xargs -r dirname)
fi
[ -n "$src" ] || { echo "unpacked, but no bin/cosmocc found" >&2; exit 1; }

mkdir -p "$PREFIX"
cp -a "$src"/. "$PREFIX"/
chmod +x "$PREFIX/bin/"* 2>/dev/null || true

echo >&2
echo "cosmocc installed: $PREFIX/bin/cosmocc" >&2
"$PREFIX/bin/cosmocc" --version 2>&1 | head -1 >&2
echo >&2
echo "now run:" >&2
echo "  export PATH=$PREFIX/bin:\$PATH" >&2
echo "  make ape          # builds bin/gg, bin/gg.exe, bin/gg.com" >&2
