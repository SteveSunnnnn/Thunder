# Notification runtime in Thunder 1.0

Thunder has a dedicated, game-neutral notification content and state runtime. It is
separate from the Event/Decision/Journal gameplay log: a log records simulation
history, while a notification has user-facing lifecycle, deduplication and actions.

This is a functional foundation, not a complete strategy-game notification UI.
Notification state currently participates in the authoritative engine checksum and
save, so changes to its lifecycle must preserve deterministic continuation.

## Content definition

Notifications are normal `.thunder` objects compiled through
`NotificationContentDatabase`:

```text
script can_apply_relief {
    scope = country
    trigger = { treasury_above = 10 }
    effect = { add_treasury = 5 }
}

notification fiscal_capacity_available {
    scope = country
    title = notification.fiscal.title
    body = notification.fiscal.body
    icon = ui.notification.fiscal
    category = economy
    priority = high
    dedupe = replace
    lifetime_ticks = 8
    potential = can_apply_relief
    action = {
        key = apply
        label = notification.fiscal.apply
        allow = can_apply_relief
        effect = can_apply_relief
    }
}
```

Required fields are stable notification key, scope, title localization key and body
localization key. Optional fields are icon asset key, category key, priority,
deduplication policy, exact non-negative lifetime, potential script, and repeated
actions. Each action requires a definition-local stable key and label localization
key and may name allow/effect scripts.

Supported scopes are Country, State, Province, Pop and Market. Potential and action
scripts must have exactly the notification's root scope. Unknown scripts, scope
mismatches, duplicate notification/action keys, invalid enum values and non-integral
or non-finite lifetimes are content diagnostics. Later effective content can replace
a notification spec by the same top-level key through the existing content overlay;
semantic patch/extend/remove is not implemented.

Binding is explicit: content is ingested/compiled first, then
`DefinitionDatabase::bind_notifications` resolves script references and publishes
runtime definitions. Definitions are immutable content and are not copied into a
save payload.

## Runtime state and lifecycle

Each emitted instance receives a nonzero, monotonic 64-bit
`NotificationInstanceId`. An instance stores:

- definition reference and primary scope;
- optional source and map-target scopes;
- created, last-updated and optional expiry ticks;
- caller-provided 64-bit deduplication key;
- occurrence count;
- selected action and lifecycle state.

States are `Unread`, `Read`, `Actioned`, `Dismissed` and `Expired`. Only Unread and
Read are active. Mark-read and dismiss operations are explicit validated transitions.
An action first evaluates its optional allow script, then runs its optional effect;
the primary notification scope is ROOT/THIS and the stored source scope is FROM. A
successful action records the stable action selection and makes the instance
Actioned. Failed allow/effect evaluation leaves it active.

A zero lifetime never expires. A positive lifetime computes a saturated expiry tick;
`NotificationRuntime::update` changes active instances to Expired when the supplied
tick reaches it. `ThunderEngine` currently calls this update on its daily boundary.
Terminal instances remain in the store for history/save consistency; pruning and
archival policy are not implemented.

## Deduplication

Deduplication compares active instances by definition, primary scope and caller
dedupe key:

- `stack` always creates a new instance;
- `suppress` reuses the active instance, increments occurrence count, updates its
  timestamp/expiry, and preserves its read state and original source/map target;
- `replace` performs the same aggregation but replaces source/map target and resets
  the reused instance to Unread with no selected action.

Occurrence count saturates instead of overflowing. Dedupe lookup and ID lookup are
currently linear over a bounded vector; this is correct and deterministic but will
need stable-key indices or a SoA inbox store for very high notification volumes.

## Save, restore and checksum

Save schema v4 can append the tagged `NTF1` section. It records the monotonic next
instance ID and every instance. Definition and selected action references are encoded
as stable content keys, so restoring against the same effective content succeeds even
when definitions were registered in another order.

Decode is staged. Before commit, Thunder checks:

- section framing, count limits and duplicate extension tags;
- unique, valid instance IDs and a monotonic next-ID;
- presence of referenced definition and action keys;
- root/source/map scope validity against the staged World;
- tick ordering, nonzero occurrence counts and state/action invariants;
- payload and runtime checksums.

Missing content or corrupt state rejects the complete restore and leaves the target
engine unchanged. The notification checksum covers definition presentation/behavior
metadata relevant to instances, stable action keys, next ID and all instance fields;
the engine checksum includes that result. The effective content hash remains the
compatibility contract for compiled potential/effect program bytes.

Older saves without `NTF1` restore an empty notification state with next ID 1. The
tagged section is emitted once notification content/state requires it, without
changing the Thunder 1.0 product version.

## Validation contract

Content binding, all dedupe policies, potential gating, action effects, state
transitions, expiry, stable definition/action restoration under registration
reordering, save/restore continuation, invalid state rejection and atomic failure
when content is missing remain part of the engine contract. Future authoritative
fields must extend the save codec, checksum and validation in the same change.

## Remaining work

The following capabilities are deliberately not claimed:

- a typed gameplay-signal/on-action bridge that emits notifications from content;
- typed localization arguments or captured script/event-target payloads;
- inbox/toast/map-focus UI, sorting/filtering, user preferences and accessibility;
- sounds, portraits, backgrounds, grouping policy and notification history pruning;
- multiplayer presentation filtering and a deliberate split between simulation-
  authoritative facts and per-user delivery/read state;
- indexed/SoA storage and bounded archival for production-scale campaigns.

Until the authoritative/presentation split is designed, UI code must use the runtime's
validated methods and must not mutate notification records directly.
