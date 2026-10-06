# gg module registry

`modules/` is the source-controlled catalog for small, self-contained Lua modules.
The public index is `index.json`; packages are immutable, versioned Lua files at
`<name>/<version>.lua`. The gg client fetches them from this repository's
`main` branch after probing GitHub's raw host and configured HTTPS accelerators.

## Use the registry

```sh
gg modules search [words]          # search the online catalog
gg modules info hello-world        # inspect version, hash, license, capabilities
gg modules install hello-world     # install after an explicit confirmation
gg modules install hello-world@1.0.0
gg modules run hello-world Arena   # install if needed, then invoke it
gg modules update hello-world      # install the newest indexed version
gg modules test                    # show measured GitHub route latencies
```

After installation, a module is a normal user module under `~/.gg/modules/` and
can be called directly as `gg hello-world`. In a non-interactive shell, module
installation is declined by default; `GG_ASSUME_YES=1` is an explicit opt-in.

GitHub routes are probed on each registry operation. By default the client tries
direct `raw.githubusercontent.com`, `gh-proxy.com`, `ghfast.top`, and
`ghproxy.net`, sorts successful routes by measured request time, and retries
other tested routes if a package download fails. Set `GG_GITHUB_PROXIES` to a
comma-separated list of additional HTTPS proxy **hostnames**; HTTP, credentials,
paths, query strings, and fragments are rejected. The proxy is only a transport:
the index digest and package digest must match before any code is installed.

## Add a package

1. Add a new file with a new version. Do not change a published version in place:
   `modules/<lowercase-name>/<major>.<minor>.<patch>.lua`.
2. Add an entry to `index.json` with `name`, `version`, `description`, `file`,
   `sha256`, `license`, and `permissions`. `sha256` is the lowercase SHA-256 of
   the exact Lua file bytes. Current permissions are `process`, `network`,
   `filesystem-write`, and `environment`; keep the list to the APIs the module
   actually needs.
3. Update `GG_MODULES_INDEX_SHA256` in `src/gg.h` to the SHA-256 of the exact
   `modules/index.json` bytes. The CI checker verifies the index pin, every
   package digest, file paths, policy rules, and Lua syntax.
4. Run `make host`, `python3 scripts/check-modules.py --gg build/gg-host`, and
   `make test`. On PRs, CI also compares against the target branch: it rejects
   edits/removals of published versions and reuse of an already-tracked version
   path. The test suite uses a fake curl; it does not contact GitHub or install
   anything on the machine.
5. Include a clear description of what the module does and why each declared
   permission is needed. The modules paths and registry index have CODEOWNERS
   coverage; repository settings must require code-owner approval if reviews
   should block merges.

## Trust and security model

A gg module is arbitrary Lua, **not a sandbox**. Once invoked it has the current
user's process, filesystem, network, and environment privileges; the catalog
permission labels are disclosures, not operating-system enforcement. Inspect
source before running any module, especially one requesting capabilities.

The registry index hash is compiled into each gg build. This prevents a proxy or
network intermediary from silently substituting a different catalog; the
selected package is separately SHA-256 checked, size-limited, and syntax-checked
before an atomic install. That pin also means catalog updates are released with
a new gg build. GitHub Actions statically checks the allowlisted APIs, obvious
credential/secret theft indicators, suspicious dynamic code and shell-download
patterns, version/path consistency, hashes, and Lua syntax. It does not execute
submitted modules in the security job. These checks reduce risk but cannot prove
arbitrary code harmless, so maintainers must review every catalog change. Enable
**Require review from Code Owners** and make the `CI / module-security` status check
required in the repository's branch-protection/ruleset settings.
