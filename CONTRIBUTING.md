# Contributing to Thunder

Thunder accepts focused changes that preserve its C++23, deterministic simulation,
SoA storage, and JobSystem architecture.

## Before opening a change

1. Build the Debug headless engine.
2. Build the Release headless engine.
3. Keep game-specific content in data files; do not hardcode one title's rules
   into the engine.
4. Do not submit proprietary source, extracted commercial-game assets, secrets,
   or content whose redistribution rights are unclear.

```powershell
cmake --preset dev-headless
cmake --build --preset dev-headless -j 8

cmake --preset release-headless
cmake --build --preset release-headless -j 8
```

## Authoritative-state contract

New authoritative state must have:

- a stable key or stable runtime ID;
- save/load and migration behavior;
- deterministic checksum coverage;
- malformed-input validation;
- deterministic worker-count behavior where parallel execution is involved.

Keep generated build trees, validation output, and local saves out of commits.
See `.gitignore` for the current exclusions.

After adding, removing, or changing release files, refresh the first-party
integrity manifests with:

```powershell
python tools/update_source_manifest.py
```
