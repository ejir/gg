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

# run a file that may be an APE (needs the shell) or a plain host binary
run_any() { # run_any <file> [args...]
  f=$1
  shift
  if [ "$(head -c 2 "$f" 2>/dev/null)" = "MZ" ]; then
    sh "$f" "$@"
  else
    "$f" "$@"
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
check "json decode"       "2"                 "$GG" -e 'local t=gg.json.decode("{\"b\":2}"); print(t.b)'
check "sha256"            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" "$GG" -e 'print(gg.sha256("abc"))'
check "sha256 multi-block" "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3" "$GG" -e 'print(gg.sha256(string.rep("a",1000)))'
check "mkstemp is unique and supports a directory" "ok" "$GG" -e 'local a=gg.mkstemp(".tmp", gg.dir); local b=gg.mkstemp(".tmp", gg.dir); print(a~=b and gg.is_file(a) and gg.is_file(b) and "ok" or "bad"); gg.rm(a); gg.rm(b)'
check "platform table"     "linux"             "$GG" -e 'print(gg.platform.os)'
check "shell quote"        "a b"               "$GG" -e 'print(gg.str.quote("a b"))'
check "capture"            "hello"             "$GG" -e 'print(gg.capture("echo hello"))'
check "split/join"         "a,b,c"             "$GG" -e 'print(table.concat(gg.str.split("a b c"), ","))'
check "tui.form fields"    "u 16 true b"       "$GG" -e 'local f=gg.tui.form({title="t",fields={{name="url",label="U",kind="text",default="u"},{name="jobs",label="J",kind="text",default=16},{name="yes",label="Y",kind="bool",default=true},{name="pick",label="P",kind="choice",choices={"a","b"},default="b"}}}); print(f.url.." "..f.jobs.." "..tostring(f.yes).." "..f.pick)'
check "tui.menu cancel"    "nil"               "$GG" -e 'print(tostring(gg.tui.menu({title="t",items={"a","b"}})))'
check "completion script" "_gg_complete"       "$GG" completion bash
check "completion names"  "aria2c"            "$GG" __complete ar
check "module registry completion" "search"     "$GG" __complete modules s
check "completion flags"  "--jobs"            "$GG" __complete -- aria2c --jo
check "refresh PATH cache" "function:"        "$GG" -e 'print(gg.refresh_tools)'

echo
echo "modules"
check "ls shows builtins"  "aria2c"            "$GG" ls
check "setup module"       "Cross-distro package manager" "$GG" ls
check "setup help"         "mirrors"           "$GG" setup --help
check "setup status"       "Package manager:"  "$GG" setup status

# Verify distro selection dispatches to a stubbed native package manager, never the host package database.
if [ -r /etc/os-release ]; then
  . /etc/os-release
  setup_pm=""
  setup_action=""
  case " ${ID:-} ${ID_LIKE:-} " in
    *" debian "*|*" ubuntu "*|*" linuxmint "*) setup_pm=apt-get; setup_action=update ;;
    *" fedora "*|*" rhel "*|*" centos "*|*" rocky "*|*" almalinux "*)
      if command -v dnf >/dev/null 2>&1; then setup_pm=dnf; setup_action=makecache
      elif command -v yum >/dev/null 2>&1; then setup_pm=yum; setup_action=makecache; fi ;;
  esac
  if [ -n "$setup_pm" ]; then
    FAKE_PM="$WORK/fake-package-manager"
    mkdir -p "$FAKE_PM"
    cat > "$FAKE_PM/$setup_pm" <<'SH'
#!/bin/sh
printf 'fake-pm:%s\n' "$*"
SH
    chmod +x "$FAKE_PM/$setup_pm"
    cat > "$FAKE_PM/sudo" <<'SH'
#!/bin/sh
exec "$@"
SH
    chmod +x "$FAKE_PM/sudo"
    check "setup dispatches to $setup_pm" "fake-pm:$setup_action" env PATH="$FAKE_PM:$PATH" "$GG" setup update
    check "setup forwards package names" "fake-pm:install -y git curl" env PATH="$FAKE_PM:$PATH" "$GG" setup install git curl
  fi
fi

# A fake curl keeps mirror-speed tests deterministic and never touches real sources.
FAKE_CURL="$WORK/fake-curl"
mkdir -p "$FAKE_CURL"
cat > "$FAKE_CURL/curl" <<'SH'
#!/bin/sh
printf 'GG_RESULT:200:0.005:2097152'
SH
chmod +x "$FAKE_CURL/curl"
check "mirror speed probe offers tested choices" "Choose a reachable mirror explicitly" env PATH="$FAKE_CURL:$PATH" "$GG" setup mirrors

# Exercise the online registry with a fake transport. No network or user files are touched.
REGISTRY_FIXTURES="$WORK/registry-fixtures"
mkdir -p "$REGISTRY_FIXTURES"
cp -R modules "$REGISTRY_FIXTURES/modules"
FAKE_REGISTRY_CURL="$WORK/fake-registry-curl"
mkdir -p "$FAKE_REGISTRY_CURL"
cat > "$FAKE_REGISTRY_CURL/curl" <<'SH'
#!/bin/sh
dest=
url=
writeout=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --output|-o) dest=$2; shift 2 ;;
    --write-out|-w) writeout=1; shift 2 ;;
    https://*) url=$1; shift ;;
    *) shift ;;
  esac
done
case "$url" in
  *"/modules/index.json") file="$GG_TEST_REGISTRY_DIR/modules/index.json" ;;
  *"/modules/"*".lua") rel=${url##*/main/}; file="$GG_TEST_REGISTRY_DIR/$rel" ;;
  *) file=; status=404 ;;
esac
case "$url" in
  *gh-proxy.com*) elapsed=0.010 ;;
  *ghfast.top*) elapsed=0.020 ;;
  *ghproxy.net*) status=502; elapsed=0.030 ;;
  *) elapsed=0.050 ;;
esac
status=${status:-200}
tamper=${GG_TEST_TAMPER_ALL:-0}
if [ "${GG_TEST_TAMPER_FAST:-0}" = 1 ]; then
  case "$url" in *gh-proxy.com*) tamper=1 ;; esac
fi
if [ "$status" = 200 ] && [ -n "$file" ] && [ -r "$file" ]; then
  if [ "${GG_TEST_BAD_INDEX:-0}" = 1 ] && [ -z "$dest" ] && echo "$file" | grep -q 'index.json$'; then
    printf '{"schema":1,"packages":[]}\n'
  elif [ "$tamper" = 1 ] && [ -n "$dest" ] && echo "$file" | grep -q '\.lua$'; then
    printf 'local M={}\nfunction M.run() print("tampered") return 0 end\nreturn M\n' > "$dest"
  elif [ -n "$dest" ]; then
    cat "$file" > "$dest"
  else
    cat "$file"
  fi
else
  status=404
fi
if [ "$writeout" = 1 ]; then printf '\nGG_MODULE_FETCH:%s:%s\n' "$status" "$elapsed"; fi
[ "$status" = 200 ]
SH
chmod +x "$FAKE_REGISTRY_CURL/curl"
check "registry searches catalog" "hello-world" env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" "$GG" modules search hello
check_rc "modified registry index is rejected" 1 env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" GG_TEST_BAD_INDEX=1 "$GG" modules search
check "registry installs from fastest proxy" "Installed hello-world@1.0.0 via gh-proxy.com" env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" GG_ASSUME_YES=1 "$GG" modules install hello-world
check "registry module can be invoked online" "Hello, arena!" env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" "$GG" modules run hello-world arena
check_rc "registry install requires explicit confirmation" 1 env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" "$GG" modules install platform-info
if [ ! -e "$GG_DIR/modules/platform-info.lua" ]; then
  pass=$((pass + 1)); printf '  \033[38;5;42mok\033[0m   declined registry module is not installed\n'
else
  fail=$((fail + 1)); printf '  \033[38;5;203mFAIL\033[0m declined registry module is not installed\n'
fi
check_rc "tampered module is rejected" 1 env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" GG_TEST_TAMPER_ALL=1 GG_ASSUME_YES=1 "$GG" modules install platform-info
if [ ! -e "$GG_DIR/modules/platform-info.lua" ]; then
  pass=$((pass + 1)); printf '  \033[38;5;42mok\033[0m   tampered source is never installed\n'
else
  fail=$((fail + 1)); printf '  \033[38;5;203mFAIL\033[0m tampered source is never installed\n'
fi
check "bad fastest proxy falls back to the next hash-valid route" "Installed platform-info@1.0.0 via ghfast.top" env PATH="$FAKE_REGISTRY_CURL:$PATH" GG_TEST_REGISTRY_DIR="$REGISTRY_FIXTURES" GG_TEST_TAMPER_FAST=1 GG_ASSUME_YES=1 "$GG" modules install platform-info

check "hello module runs"  "hello world."      "$GG" hello
check "module params"      "hello tux!!!"      "$GG" hello tux -e
check "module defaults"    "hello world."      "$GG" hello
check "module help"        "options:"          "$GG" hello --help
check "demo module"        "available demos"   "$GG" demo
check "unknown module"     "unknown module"    "$GG" no-such-thing-xyz

# A folder module keeps its private helper/assets beside its entry point.
PKG=$WORK/module-package/mytool
mkdir -p "$PKG/assets"
cat > "$PKG/mytool.lua" <<'LUA'
local M = {}
function M.run(ctx)
  local path = gg.join_path(ctx.module_dir, "assets", "payload.txt")
  print(gg.read(path))
  return 0
end
return M
LUA
printf 'folder-asset-ok\n' > "$PKG/assets/payload.txt"
check "install module folder" "installed module mytool" "$GG" modules install "$PKG"
check "self-contained module runs" "folder-asset-ok" "$GG" mytool
check "remove module folder" "deleted" env GG_ASSUME_YES=1 "$GG" rm mytool

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
echo "self-contained scripts (gg bundle)"
BUNDLE=$WORK/bundle
mkdir -p "$BUNDLE/assets"
cat > "$BUNDLE/tool.lua" <<'LUA'
local M = {}
M.title = "tool · bundled"
M.params = { { name = "who", pos = 1, type = "string", default = "nobody" } }
function M.run(ctx)
  local data = gg.assets.read("greeting.txt")
  ctx.log("hi %s: %s", tostring(ctx.args.who), data or "no asset")
  return 0
end
return M
LUA
printf 'asset-payload' > "$BUNDLE/assets/greeting.txt"
printf 'print("bundle init ran")\n' > "$BUNDLE/init.lua"
printf 'quick\techo BUNDLED-OK\tbundled one liner\n' > "$BUNDLE/registry.tsv"
# a bundled module shadows a built-in with the same name
cat > "$BUNDLE/hello.lua" <<'LUA'
local M = {}
M.title = "hello (from the bundle)"
M.params = { { name = "who", pos = 1, type = "string", default = "x" } }
function M.run(ctx)
  print("BUNDLED-HELLO " .. tostring(ctx.args.who))
  return 0
end
return M
LUA
check "bundle writes"      "wrote"          "$GG" bundle "$BUNDLE/app.gg" "$BUNDLE/tool.lua" "$BUNDLE/assets" "$BUNDLE/init.lua" "$BUNDLE/registry.tsv"
check "bundle carries"     "modules/tool.lua" "$GG" bundle "$BUNDLE/again.gg" -f "$BUNDLE/tool.lua"
check "bundled module runs" "hi ada: asset-payload"  run_any "$BUNDLE/app.gg" tool ada
check "bundled init.lua"   "bundle init ran"  run_any "$BUNDLE/app.gg" -e 'print("x")'
check "bundled assets list" "modules/tool.lua" run_any "$BUNDLE/app.gg" -e 'print(table.concat(gg.assets.list(), " "))'
check "bundled registry"   "BUNDLED-OK"       run_any "$BUNDLE/app.gg" quick
check "bundled marker"     "(bundled)"        run_any "$BUNDLE/app.gg" ls
check "grep-still-works"   "gg"               "$GG" --version
check_rc "bundled rm refused" 1               run_any "$BUNDLE/app.gg" rm tool
# re-bundling from a bundled binary keeps its scripts and can drop entries
check "rm: drops entries"  "no extra files"   run_any "$BUNDLE/app.gg" bundle "$BUNDLE/lean.gg" rm:assets
check "dropped asset gone" "nil"              run_any "$BUNDLE/lean.gg" -e 'print(tostring(gg.assets.read("greeting.txt")))'
check "still-bundled module in lean" "hi bob: no asset" run_any "$BUNDLE/lean.gg" tool bob
check "bundle help"        "self-contained scripts" "$GG" help bundle
check "bundle shadows built-in" "wrote"      "$GG" bundle "$BUNDLE/shadow.gg" "$BUNDLE/hello.lua"
check "shadowed module wins" "BUNDLED-HELLO ada" run_any "$BUNDLE/shadow.gg" hello ada
mkdir -p "$GG_DIR/modules"
printf 'local M={}\nM.params={{name="a",pos=1,type="string",default="b"}}\nfunction M.run() print("USER-WINS") return 0 end\nreturn M\n' > "$GG_DIR/modules/hello.lua"
check "user file beats bundle" "USER-WINS"    run_any "$BUNDLE/shadow.gg" hello
rm -f "$GG_DIR/modules/hello.lua"

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
