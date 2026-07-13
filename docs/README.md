# YarDB Documentation

This directory contains comprehensive project documentation organized by topic and development phase.

## 📚 Documentation Structure

### 🔧 Development & Engineering
- **[development.md](development.md)** - Development workflows, quick reference, and **development roadmap**
- **[changelog.md](changelog.md)** - **Shipped features and smoke coverage** (completed work)
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

1. **🔐 Security & Authentication** — scoped PATs, JWT/RBAC (Bearer PAT MVP shipped; see [changelog.md](changelog.md))
2. **📊 Observability & Monitoring** — Prometheus `/metrics`, `/ready` 503 semantics (`GET /health`, `GET /ready`, and `correlation_id` tracing shipped)
3. **🔒 TLS Proxy Implementation** — Cloud-native TLS termination
4. **🔗 Relationship model** — Prerequisite for full `$expand` / `$apply`

## ✅ Latest shipped (July 2026)

See **[changelog.md](changelog.md)** for the full list. Most recent:

- **Public probes** — **`GET /health`** and **`GET /ready`** return `{}` (PAT-exempt)
- **`correlation_id` tracing** — `X-Correlation-ID` on all HTTP handler logs
- **yarproxy header forwarding** — `Authorization` and `X-Correlation-ID` to backends; `header_forward_auth` / `header_forward_correlation` smokes
- **yarsh smokes** — auth CRUD, `$filter` `or` / `startswith`, OData query cases
- **`yarexport` `export_empty`** smoke — export fresh DB with no documents

## 📖 Reading Guide

### New to YarDB?
1. Start with root **[README.md](../README.md)** for project overview
2. Read **[programs.md](programs.md)** to understand available tools
3. Check **[development.md](development.md)** for build/test instructions

### Contributing?
1. Review **[development.md](development.md)** for development workflows
2. See **[project_organization.md](project_organization.md)** for code organization
3. Check **[changelog.md](changelog.md)** for what already shipped
4. Check **[archive/](archive/)** for historical documentation and completed proposals

### Operating YarDB?
1. Read **[programs.md](programs.md)** for detailed command documentation
2. Check **[deployment.md](deployment.md)** for production deployment
3. Monitor **[development.md](development.md)** roadmap for upcoming features

## 📝 Documentation Principles

1. **Root README.md** - Project overview and entry point
2. **docs/** - Detailed documentation by topic
3. **docs/changelog.md** - Canonical completed-work log (grows over time)
4. **docs/archive/** - Historical/completed proposals
5. **Keep it current** - Update changelog when features ship; keep README as an index
6. **Link related docs** - Cross-reference between documents