# Thunder 1.0 Technology and Research Runtime

Thunder research is a data-driven deterministic pipeline, not a fixed weekly progress increment.

## Content

`technology` objects define a stable key, category, era, integer-scaled cost, prerequisite keys,
unlock keys, an optional Country-scoped `potential` script, and an optional Country-scoped
`on_researched` script. `research_rules` controls the base innovation and the contribution from
literate population. Later mod definitions replace earlier definitions with the same key.

Definitions are validated after the complete effective mod set is loaded. Missing prerequisites,
duplicate prerequisites, self-dependencies, dependency cycles, missing scripts, and wrong script
scopes are startup errors.

## Authoritative state contract

The existing `TechnologyRecord` is the authoritative queue and completion state:

- `country` is a stable entity ID;
- `key_hash` is the stable FNV-1a content key;
- `progress_ppm` is deterministic integer progress;
- `unlocked` records completion.

Queue order is record insertion order. For each country, the first incomplete definition whose
prerequisites and scripted potential pass becomes active. This preserves stable `TechnologyId`
values and avoids vector reordering. Definitions and the derived innovation scratch column are
immutable/derived state and are not serialized.

Technology records already participate in save/load, world validation, and the deterministic world
checksum. Content binding, prerequisite ordering, completion effects, definition-cycle rejection,
stable-key restore under definition reordering, and post-load deterministic progression remain part
of the engine contract.

## Performance

Weekly innovation is accumulated in one linear pass over Pop SoA columns. Country ownership is
resolved through province ownership with market ownership as a fallback. Only integer saturating
multiply/divide is used. Research completion effects execute on the deterministic simulation thread;
the system does not introduce unordered parallel writes into authoritative state.
