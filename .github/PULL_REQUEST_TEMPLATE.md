## Summary

<!-- What changes and why? For catalog packages, describe what the module does. -->

## Module/catalog checklist (when applicable)

- [ ] New packages use a new immutable `modules/<name>/<version>.lua` path.
- [ ] `modules/index.json` includes the exact package SHA-256, license, and only required capabilities.
- [ ] `GG_MODULES_INDEX_SHA256` in `src/gg.h` matches the exact index bytes.
- [ ] `python3 scripts/check-modules.py --gg build/gg-host` and `make test` pass.
- [ ] I inspected the final Lua source; it contains no credential collection, hidden downloads, obfuscated payloads, or unrelated filesystem/process activity.
- [ ] I understand registry Lua runs with the invoking user's full permissions and is not sandboxed.

### Declared capabilities and rationale

<!-- process / network / filesystem-write / environment, or none -->

## Test plan

<!-- Include the exact commands and expected behavior. -->
