# YarDB Documentation

This directory contains comprehensive project documentation organized by topic and development phase.

## 📚 Documentation Structure

### 🔧 Development & Engineering
- **[development.md](development.md)** - Development workflows, quick reference, and **development roadmap**
- **[project_organization.md](project_organization.md)** - Project structure and P1204R0 compliance
- **[clang_module_flags.md](clang_module_flags.md)** - C++23 module handling in C++ Builder

### 🚀 Operations & Deployment
- **[programs.md](programs.md)** - Detailed documentation for all executables (`yardb`, `yarsh`, `yarproxy`, etc.)
- **[deployment.md](deployment.md)** - Deployment procedures and production considerations

### 📋 Proposals & Planning
- **[rest_api_evaluation.md](rest_api_evaluation.md)** - REST API evaluation, feature status, and prioritized roadmap

### 🤖 Agent / automation
- **[../AGENTS.md](../AGENTS.md)** - JSONL commands and triage for AI agents and CI

### 📁 Archive
- **[archive/](archive/)** - Completed/historical documentation
  - **[odata-metadata-plan.md](archive/odata-metadata-plan.md)** - ✅ **COMPLETED** OData /$metadata endpoint implementation (December 2025)

## 🎯 Current Development Focus

**Recommended next (API):** Relationship / navigation model for `$expand` and `$apply`. See **[rest_api_evaluation.md](rest_api_evaluation.md)**.

Broader roadmap priorities (see **[development.md](development.md)**):

1. **🔐 Security & Authentication** - JWT, RBAC
2. **📊 Observability & Monitoring** - Prometheus metrics, health checks
3. **🔒 TLS Proxy Implementation** - Cloud-native TLS termination
4. **🔗 Relationship model** - Prerequisite for full `$expand` / `$apply`

## ✅ Recently Completed

- **Piped `yarsh` smoke tests** (July 2026) - `tests/yarsh/smoke.sh` starts ephemeral `yardb`, pipes CLI commands, asserts status/headers/bodies; CI job `yarsh-smoke`.
- **`yarexport` smoke tests** (July 2026) - `tests/yarexport/smoke.sh` seeds data, stops `yardb`, exports JSONL, validates syntax and record shape; CI job `yarexport-smoke`.
- **`yarproxy` smoke tests** (July 2026) - `tests/yarproxy/smoke.sh` starts two `yardb` replicas plus proxy, checks CRUD, write fan-out, and read round-robin (`--replicas=N`); CI job `yarproxy-smoke`.
- **`yarproxy` docs clarified** (July 2026) - documented as dev HTTP fan-out proxy (not replication/HA); limitations in README, programs.md, deployment.md, and `--help`.
- **Index-only `count`** (July 2026) - `engine::count(selector)` and `$count=true` use index view sizes for empty selectors, single `_id`/indexed-field constraints with `$eq`/`$gt`/`$gte`/`$lt`/`$lte`; fall back to seek-and-match for `$ne`, multi-field AND, string post-filters, and OR branches.
- **Expanded `[yardb]` test coverage** (July 2026) - `yar-index.test.c++`, engine range/`$in`/replace/reindex tests, `count_with_parsed_filter` OData scenarios, HTTP `$ne` and `$count` variants (288 tests).
- **`yarsh` REPL improvements** (July 2026) - full body display (including single-digit `$count`), resilient JSON parse errors, refreshed help, optional `@Accept` / `@If-Match` / `@If-None-Match` request lines.
- **OData `$filter` `ne` operator** (July 2026) - `$filter=status ne 'deleted'` via xson `$ne` selectors and `document.match`.
- **Index-backed `$filter` `startswith`** (July 2026) - `startswith(name,'A')` lowered to prefix range on secondary-indexed top-level fields; `contains`/`endswith` remain post-filter.
- **Multivalue secondary indexes** (July 2026) - duplicate indexed values store multiple file positions; `m_index` and per-collection indexes use `std::flat_map`.
- **OData `$filter` nested paths** (July 2026) - `$filter=Customer/Country eq 'USA'` via nested selectors and deep-merge for `and`.
- **OData `$filter` `or` operator** (July 2026) - `$filter=age gt 25 or status eq 'active'` via OR-first parse and multi-branch `engine.read` merged by `_id`.
- **OData `$filter` `in` operator** (July 2026) - `$filter=status in ('active','pending')` and numeric lists via `parse_filter` → xson `$in` selectors. Tests in `yar-odata.test.c++` and `yar-httpd.test.c++`.
- **OData Metadata Endpoint** (December 2025) - Implemented `GET /$metadata` endpoint that returns OData 4.01 JSON CSDL metadata with automatically inferred schemas from existing collections. See **[archive/odata-metadata-plan.md](archive/odata-metadata-plan.md)** for implementation details.

See **[development.md](development.md)** for detailed roadmap and implementation plans.

## 📖 Reading Guide

### New to YarDB?
1. Start with root **[README.md](../README.md)** for project overview
2. Read **[programs.md](programs.md)** to understand available tools
3. Check **[development.md](development.md)** for build/test instructions

### Contributing?
1. Review **[development.md](development.md)** for development workflows
2. See **[project_organization.md](project_organization.md)** for code organization
3. Check **[archive/](archive/)** for historical documentation and completed proposals

### Operating YarDB?
1. Read **[programs.md](programs.md)** for detailed command documentation
2. Check **[deployment.md](deployment.md)** for production deployment
3. Monitor **[development.md](development.md)** roadmap for upcoming features

## 📝 Documentation Principles

1. **Root README.md** - Project overview and entry point
2. **docs/** - Detailed documentation by topic
3. **docs/archive/** - Historical/completed proposals
4. **Keep it current** - Update docs as code evolves
5. **Link related docs** - Cross-reference between documents

