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

| Repo | Modules-era first tag | Prior line (historical) |
|------|----------------------|-------------------------|
| [tester](https://github.com/ruoka/tester) | `v2.0.0` | `v1.0.0` pre-release / pre-JSONL |
| [cryptic](https://github.com/ruoka/cryptic) | `v2.0.0` | untagged header history treated as v1 |
| [net4cpp](https://github.com/ruoka/net4cpp) | `v3.0.0` | `v1.0` / `v2.0` / `v2.1` |
| [json4cpp](https://github.com/ruoka/json4cpp) (`deps/xson`) | `v3.0.0` | `v1.0` / `v2.0` |

## Tester pin rule

**All `deps/tester` gitlinks must be the same commit** — YarDB’s top-level `deps/tester` and every nested `deps/*/deps/tester` (cryptic, net, xson).

When bumping tester:

1. Land the change on tester `main` (CI green).
2. Update nested pins in cryptic, net, and xson to that SHA and push those repos.
3. Update YarDB’s `deps/tester` **and** the three library submodule pointers together in one YarDB commit.

Do not leave YarDB on a newer tester tip while libraries still pin an older nested tester.

## Release bar

Cut or move a SemVer tag only when:

1. The repo’s default-branch CI is green on the tagged commit.
2. Open PRs that affect the release surface are merged or closed.
3. Nested `deps/tester` pins match across consumers (see above).

Prefer tagging the default-branch tip (`main` / `master`), not a feature branch.

## Related

- Tester’s own surface and rules: [`deps/tester/docs/release-policy.md`](../deps/tester/docs/release-policy.md)
- Agent build/test commands: [`AGENTS.md`](../AGENTS.md)
