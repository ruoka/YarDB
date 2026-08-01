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
| [YarDB](https://github.com/ruoka/YarDB) | [`v1.0.0`](https://github.com/ruoka/YarDB/releases/tag/v1.0.0) | untagged `master` history | this document |
| [tester](https://github.com/ruoka/tester) | [`v2.0.0`](https://github.com/ruoka/tester/releases/tag/v2.0.0) | `v1.0.0` pre-release / pre-JSONL | [`deps/tester/docs/release-policy.md`](../deps/tester/docs/release-policy.md) |
| [cryptic](https://github.com/ruoka/cryptic) | [`v2.0.0`](https://github.com/ruoka/cryptic/releases/tag/v2.0.0) | untagged header history treated as v1 | [`deps/cryptic/docs/release-policy.md`](../deps/cryptic/docs/release-policy.md) |
| [net4cpp](https://github.com/ruoka/net4cpp) | [`v3.0.0`](https://github.com/ruoka/net4cpp/releases/tag/v3.0.0) | `v1.0` / `v2.0` / `v2.1` | [`deps/net/docs/release-policy.md`](../deps/net/docs/release-policy.md) |
| [json4cpp](https://github.com/ruoka/json4cpp) (`deps/xson`) | [`v3.0.0`](https://github.com/ruoka/json4cpp/releases/tag/v3.0.0) | `v1.0` / `v2.0` | [`deps/xson/docs/release-policy.md`](../deps/xson/docs/release-policy.md) |

**Current YarDB release: [`v1.0.0`](https://github.com/ruoka/YarDB/releases/tag/v1.0.0)** — first supported SemVer tag for the modules-era product surface (`yardb`, REST/OData HTTP API, FSON engine, companion CLIs). Between tags, prefer an explicit commit or `master` tip and keep submodule pins aligned.

## When not to bump

Do **not** cut a new SemVer tag solely for:

- **Pin-only** submodule gitlink updates (including nested `deps/tester` sync)
- **Test-only** changes that leave the public consume surface unchanged (e.g. adopting `require_throws_as<E>(callable)`, silencing warnings in `*.test.c++`, fixture cleanup)
- **Docs / CI / policy** wording that does not change documented public behaviour

Land those on the default branch and keep pins aligned. Cut a tag when the public surface itself warrants MAJOR, MINOR, or PATCH.

## Tester pin rule

**All `deps/tester` gitlinks must be the same commit** — YarDB’s top-level `deps/tester` and every nested `deps/*/deps/tester` (cryptic, net, xson).

When bumping tester:

1. Land the change on tester `main` (CI green); prefer a release tag when cutting a supported tester version.
2. Update nested pins in cryptic, net, and xson to that SHA and push those repos.
3. Update YarDB’s `deps/tester` **and** the three library submodule pointers together in one YarDB commit.

Do not leave YarDB on a newer tester tip while libraries still pin an older nested tester.

## Release bar

Cut or move a SemVer tag only when:

1. The repo’s default-branch CI is green on the tagged commit.
2. Open PRs that affect the release surface are merged or closed.
3. Nested `deps/tester` pins match across consumers (see above).

Prefer tagging the default-branch tip (`main` / `master`), not a feature branch. Per-repo criteria and public-surface tables live in each library’s `docs/release-policy.md`.

## Related

- Tester policy (canonical for the framework): [`deps/tester/docs/release-policy.md`](../deps/tester/docs/release-policy.md)
- Agent build/test commands: [`AGENTS.md`](../AGENTS.md)
