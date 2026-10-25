# Ecosystem versioning

How YarDB and its library submodules are versioned and kept in sync.

## Scheme

Each repository uses SemVer tags: `vMAJOR.MINOR.PATCH`.

| Bump | Means |
|------|-------|
| MAJOR | Public consume surface changed incompatibly (exported modules/API, CLI contract, or minimum toolchain) |
| MINOR | Additive, backward-compatible public change |
| PATCH | Fix that leaves documented behaviour intact |

**Product version ≠ dependency majors.** YarDB’s own tag (when cut) is independent of `tester` / `cryptic` / `net` / `xson` majors. Bumping a dependency’s MAJOR does not by itself bump YarDB.

**Modules-era baseline.** The Clang 21 + libc++ modules (`import …;`) consume surface is a new major relative to older header-era tags:

| Repo | Current modules-era tag | Prior line (historical) | Policy |
|------|-------------------------|-------------------------|--------|
| [YarDB](https://github.com/ruoka/YarDB) | [`v1.1.0`](https://github.com/ruoka/YarDB/releases/tag/v1.1.0) | [`v1.0.1`](https://github.com/ruoka/YarDB/releases/tag/v1.0.1) | this document |
| [tester](https://github.com/ruoka/tester) | [`v3.1.0`](https://github.com/ruoka/tester/releases/tag/v3.1.0) | `v2.x` / `v1.0.0` pre-release / pre-JSONL | [`deps/tester/docs/release-policy.md`](../deps/tester/docs/release-policy.md) |
| [cryptic](https://github.com/ruoka/cryptic) | [`v2.0.1`](https://github.com/ruoka/cryptic/releases/tag/v2.0.1) | untagged header history treated as v1 | [`deps/cryptic/docs/release-policy.md`](../deps/cryptic/docs/release-policy.md) |
| [net4cpp](https://github.com/ruoka/net4cpp) | [`v3.4.1`](https://github.com/ruoka/net4cpp/releases/tag/v3.4.1) | `v1.0` / `v2.0` / `v2.1` | [`deps/net/docs/release-policy.md`](../deps/net/docs/release-policy.md) |
| [json4cpp](https://github.com/ruoka/json4cpp) (`deps/xson`) | [`v3.1.0`](https://github.com/ruoka/json4cpp/releases/tag/v3.1.0) | `v1.0` / `v2.0` | [`deps/xson/docs/release-policy.md`](../deps/xson/docs/release-policy.md) |

**Current YarDB release: [`v1.1.0`](https://github.com/ruoka/YarDB/releases/tag/v1.1.0)** — minor on the modules-era product surface (`yardb`, REST/OData HTTP API, FSON engine, companion CLIs). It adds atomic `POST /$batch` and unique secondary indexes. Pins: tester [`v3.1.0`](https://github.com/ruoka/tester/releases/tag/v3.1.0) (`746fa80`), net4cpp `1f40684` (`v3.4.1` plus the HTTP client `io_timeout` change), json4cpp `df42763`, and cryptic `6c5cbf6`. Those three libraries record the same tester commit. Prior tag: [`v1.0.1`](https://github.com/ruoka/YarDB/releases/tag/v1.0.1). Between tags, prefer an explicit commit or `master` tip and keep submodule pins aligned.

## When not to bump

Do **not** cut a new SemVer tag solely for:

- **Pin-only** submodule gitlink updates (including nested `deps/tester` sync)
- **Test-only** changes that leave the public consume surface unchanged (e.g. adopting `require_throws_as<E>(callable)`, silencing warnings in `*.test.c++`, fixture cleanup)
- **Docs / CI / policy** wording that does not change documented public behaviour

Land those on the default branch and keep pins aligned. Cut a tag when the public surface itself warrants MAJOR, MINOR, or PATCH.

## Tester pin rule

YarDB owns `deps/tester`. Nested copies under cryptic, net, and xson are those repositories’ submodules.

When bumping tester:

1. Land the change on tester `main` (CI green); prefer a release tag when cutting a supported tester version.
2. Update nested pins in cryptic, net, and xson in those repositories.
3. Update YarDB’s `deps/tester`. Bump a library pointer only after that library’s tip records the new tester commit. Do not commit inside another dependency’s submodule.

A nested copy that still lags stays at the parent library’s recorded commit until that library picks up the tester tag.

## Release bar

Cut or move a SemVer tag only when:

1. The repo’s default-branch CI is green on the tagged commit.
2. Open PRs that affect the release surface are merged or closed.
3. Before a YarDB tag, nested `deps/tester` pins match the tester commit this repo records (those library pointers move in their own repos first).

Prefer tagging the default-branch tip (`main` / `master`), not a feature branch. Per-repo criteria and public-surface tables live in each library’s `docs/release-policy.md`.

## Related

- Tester policy (canonical for the framework): [`deps/tester/docs/release-policy.md`](../deps/tester/docs/release-policy.md)
- Agent build/test commands: [`AGENTS.md`](../AGENTS.md)
