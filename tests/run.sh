#!/bin/sh
# gg head-less test suite.  usage: tests/run.sh [path-to-gg]
set -u

GG=${1:-build/gg-host}
[ -x "$GG" ] || { echo "no such binary: $GG" >&2; exit 2; }
GG=$(cd "$(dirname "$GG")" && pwd)/$(basename "$GG")

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
export HOME="$WORK/home"
export GG_DIR="$WORK/gg"
export GG_PLAIN=1
export GG_LANG=en
mkdir -p "$HOME"

pass=0
fail=0

check() { # check <name> <expected-substring> <command...>
  name=$1; want=$2; shift 2
  out=$("$@" 2>&1)
  rc=$?
  if printf '%s' "$out" | grep -qF -- "$want"; then
    pass=$((pass + 1))
    printf '  \033[38;5;42mok\033[0m   %s\n' "$name"
  else
    fail=$((fail + 1))
    printf '  \033[38;5;203mFAIL\033[0m %s (rc=%s)\n' "$name" "$rc"
    printf '       wanted: %s\n' "$want"
    printf '       got:    %s\n' "$(printf '%s' "$out" | head -4 | tr '\n' '|')"
  fi
}

check_rc() { # check_rc <name> <expected-rc> <command...>
  name=$1; want=$2; shift 2
  "$@" >/dev/null 2>&1
  rc=$?
  if [ "$rc" = "$want" ]; then
    pass=$((pass + 1)); printf '  \033[38;5;42mok\033[0m   %s\n' "$name"
  else
    fail=$((fail + 1)); printf '  \033[38;5;203mFAIL\033[0m %s (rc=%s want=%s)\n' "$name" "$rc" "$want"
  fi
}

echo "gg test suite — $GG"
echo

echo "core"
check "version"            "0.1.0"            "$GG" --version
check "help"               "usage"            "$GG" help
check "eval"               "42"               "$GG" -e 'print(6*7)'
check "lua stdlib"         "Lua 5.4"          "$GG" -e 'print(_VERSION)'
check "json encode"        '"a":1'             "$GG" -e 'print(gg.json.encode({a=1}))'
check "json decode"        "2"                 "$GG" -e 'local t=gg.json.decode("{\"b\":2}"); print(t.b)'
check "platform table"     "linux"             "$GG" -e 'print(gg.platform.os)'
check "shell quote"        "a b"               "$GG" -e 'print(gg.str.quote("a b"))'
check "capture"            "hello"             "$GG" -e 'print(gg.capture("echo hello"))'
check "split/join"         "a,b,c"             "$GG" -e 'print(table.concat(gg.str.split("a b c"), ","))'

echo
echo "modules"
check "ls shows builtins"  "aria2c"            "$GG" ls
check "apt module"         "package manager"   "$GG" ls
check "hello module runs"  "hello world."      "$GG" hello
check "module params"      "hello tux!!!"      "$GG" hello tux -e
check "module defaults"    "hello world."      "$GG" hello
check "module help"        "options:"          "$GG" hello --help
check "demo module"        "available demos"   "$GG" demo
check "unknown module"     "unknown module"    "$GG" no-such-thing-xyz

echo
echo "lua syntax"
if [ -d examples ]; then
  for f in examples/*.lua; do
    check "syntax $f" "ok" "$GG" -e "local f,e=loadfile('$f'); print(f and 'ok' or e)"
  done
fi
for f in "$GG_DIR"/modules/*.lua; do
  [ -e "$f" ] || continue
done

echo
echo "registry"
check "add"                "added"             "$GG" add hi 'echo hello-from-registry'
check "dispatch"           "hello-from-registry" "$GG" hi
check "registry args"      "hello-from-registry extra" "$GG" hi extra
check "ls shows command"   "hello-from-registry" "$GG" ls
check "show"               "hello-from-registry" "$GG" show hi
check "rm"                 "removed command"   "$GG" rm hi
check_rc "removed really"  127                 "$GG" hi

echo
echo "shell"
check "run"                "via-shell"         "$GG" run 'echo via-shell && echo again'
check "run multi arg"      "a b"               "$GG" run echo a b
check "lua script"         "from-script"       sh -c "printf 'print(\"from-script\", ...)\n' > $WORK/s.lua; $GG $WORK/s.lua"
check "script args"        "arg1"              sh -c "printf 'print(arg[1])\n' > $WORK/a.lua; $GG $WORK/a.lua arg1"

echo
echo "paths"
check "active print"       ">>> gg >>>"        "$GG" active --print
check "active print word"  "export PATH"       "$GG" active print
check "active block"       "$GG_DIR/bin"       "$GG" active --shell bash --print
check "portable block"     '.gg/bin'           env -u GG_DIR "$GG" active --shell bash --print
check "fish block"         "set -gx PATH"      "$GG" active --shell fish --print
check "powershell block"   '$ggbin'            "$GG" active --shell powershell --print
check "ps default block"   "USERPROFILE"       env -u GG_DIR "$GG" active --shell powershell --print
check "active help"        "usage: gg active"  "$GG" active --help
check "active runs"        "enabled gg in"     "$GG" active --shell bash
check "bashrc written"     "$GG_DIR/bin"       cat "$HOME/.bashrc"
check "active is idempotent" "1"               sh -c "grep -c '>>> gg >>>' $HOME/.bashrc"
check "active off"         "removed gg block"  "$GG" active off
check "block gone"         "0"                 sh -c "grep -c '>>> gg >>>' $HOME/.bashrc || true"

echo
echo "scaffolding"
check "init"               "created"           "$GG" init mytool --title "my tool" --desc "test module"
check "new module listed"  "mytool"            "$GG" ls
check "new module runs"    "target"            "$GG" mytool something
check "init actions"       "created"           "$GG" init actiontool --actions
check_rc "syntax ok"       0                   "$GG" mytool --help
check "doctor"             "gg doctor"         "$GG" doctor

echo
printf '  \033[1m%d passed, %d failed\033[0m\n' "$pass" "$fail"
[ "$fail" = 0 ]
