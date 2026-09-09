# Thunder Mod Runtime 1.0

Thunder's mod pipeline is game-neutral. A package declares identity and relationships;
the launcher, editor or package catalog supplies its content root and a hash of the
verified package bytes. No game name, campaign rule or content key is built into the
planner.

## Manifest format

A manifest is a UTF-8, line-oriented key/value document. `#` and `//` start comments
outside quoted values. Scalar keys occur once; relationship keys may be repeated.
The recommended extension is `.thundermod`.

```text
id = example.trade-rebalance
version = 2.4.1
load_priority = 20

required = thunder.base >=1.0.0, <2.0.0
required = example.shared-rules@^3.1.0
optional = example.scripted-ui >=1.2.0
conflict = example.legacy-overhaul <2.0.0

load_after = example.map-data
load_before = example.total-conversion
```

Supported fields are:

- `id`: required canonical package ID;
- `version`: required semantic version;
- `load_priority` (or `priority`): signed 32-bit ready-queue priority, default `0`;
- `required` / `required_dependency`: required dependency and optional version range;
- `optional` / `optional_dependency`: optional dependency and optional version range;
- `conflict` / `conflicts`: incompatible package and optional version range;
- `load_before`, `load_after`: ordering hints applied when the named package exists.

IDs are lower-case ASCII package names containing letters, digits, `.`, `-` and `_`.
They cannot begin or end with punctuation, contain `..`, or exceed 128 bytes. Thunder
derives a 64-bit stable ID from the canonical text and rejects a stored-ID mismatch,
duplicate ID or detected hash collision. Discovery order never becomes identity.

Versions use strict `major.minor.patch` SemVer, with optional prerelease and build
metadata. Dependency expressions accept `*`, an exact version, `=`, `==`, `<`, `<=`,
`>`, `>=`, compatible-major `^`, and compatible-minor `~`. Whitespace or commas join
predicates as a conjunction. Build metadata remains part of the stable version key
and compatibility hash but, as required by SemVer, does not affect version precedence.

## Package descriptors and hashes

`ModManifest` contains authored metadata. The package layer supplies a `ModPackage`:

```cpp
ModPackage package{
    .manifest = parsed.manifest,
    .content_root = verified_unpack_directory,
    .package_content_hash = catalog_hash,
};
```

The planner deliberately does not walk or hash the filesystem. Package tools should
compute `package_content_hash` from a documented, normalized package payload and
verify it before planning. This separation supports archives, workshop catalogs,
signed packages and editor directories without putting file I/O on simulation paths.
A zero hash is accepted for development packages but should not be used for
multiplayer compatibility decisions.

The final plan hash includes, in resolved load order:

- canonical mod ID and derived stable ID;
- canonical version;
- declared load priority;
- canonicalized dependency, conflict and ordering relationships;
- supplied package content hash;
- resolved load index.

Physical paths and package registration order are excluded. Changing package bytes,
manifest semantics or effective order changes the plan hash. `ContentLoader` folds a
mounted plan hash into its effective content hash, so save compatibility and network
handshakes can use the complete result rather than only the surviving overlay files.
The current hashes are deterministic compatibility hashes, not cryptographic
signatures.

## Deterministic dependency planning

`build_mod_load_plan` first canonicalizes packages by ID, validates the selected set,
then builds directed ordering edges:

```text
required dependency ──> dependent
installed compatible optional dependency ──> dependent
load_before package ──> target
load_after target ──> package
```

Missing `load_before`/`load_after` targets are intentionally ignored. This lets a mod
publish interoperability hints for packages that are not installed. A missing
optional dependency is also valid. An installed optional dependency with an
incompatible version produces a warning and no edge.

Thunder runs a deterministic Kahn topological sort. Among nodes whose dependencies are
currently satisfied, lower `load_priority` loads first; equal priorities sort by
canonical ID. Priority never overrides a dependency edge. The output `load_index` is
a dense total order starting at zero, so later entries have higher overlay precedence.
Strongly connected components are reported deterministically when dependency or
ordering edges form cycles.

Registration permutations of the same package set therefore produce identical entry
order, load indices, diagnostics and content hash.

## Validation diagnostics

Errors make a plan unmountable. They include:

- invalid syntax, IDs, versions, ranges or priorities;
- duplicate scalar fields, relationships or package IDs;
- stable-ID mismatch or collision;
- missing required dependencies;
- required dependency version mismatch;
- selected version-matched conflicts;
- dependency/load-order cycles.

An optional version mismatch is a warning and leaves the plan usable. Diagnostics
carry a stable code, severity, manifest source, owning mod ID, related mod ID and
message. They are sorted canonically rather than emitted in package discovery order.

## VFS overlay integration

`VirtualFileSystem::mount_plan` accepts only a valid complete plan and requires an
empty VFS. Each entry becomes a mount whose numeric overlay priority is its resolved
`load_index`. The VFS is then closed to ad-hoc mounts so the recorded plan hash cannot
become stale. Existing manual `mount` calls remain available when no plan is used.

For identical logical paths, the later planned package wins. Effective files are
processed deterministically by `(resolved priority, logical path)`. Definitions with
the same stable content key also follow this sequence, so later content can override
an earlier definition even when it is declared in a different file.

Typical startup flow is:

1. discover manifests and package roots;
2. verify or compute each package content hash;
3. parse manifests and present parse diagnostics;
4. build the deterministic load plan and reject plan errors;
5. mount the plan into a fresh VFS;
6. enumerate, parse, validate and compile effective content;
7. freeze immutable definitions and pass stable IDs/handles into simulation systems.

## Runtime separation

Manifest parsing, SemVer matching, graph construction, directory walking, file I/O,
string interning and content compilation are startup/tool operations. They do not add
authoritative per-tick state and do not change the SoA, JobSystem or deterministic
simulation hot paths. Runtime code receives immutable definitions, numeric constants,
stable handles and the already-computed compatibility hash.

This is an explicit Thunder rule: **mods may be text-heavy; simulation ticks may not be
text-heavy.**

## Remaining pipeline work

- explicit `replace / patch / extend` directives for nested definitions;
- compiled `.thundercache` artifacts keyed by package and plan hashes;
- cryptographic package signatures and catalog trust policy;
- localization-only compatibility partitions where simulation safety is provable;
- editor-only hot reload with authoritative-state restart boundaries.
