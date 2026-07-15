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

### 🤖 Agent / automation
- **[../AGENTS.md](../AGENTS.md)** - JSONL commands and triage for AI agents and CI

### 📁 Archive
- **[archive/](archive/)** - Completed/historical documentation
  - **[odata-metadata-plan.md](archive/odata-metadata-plan.md)** - ✅ **COMPLETED** OData /$metadata endpoint implementation (December 2025)
  - **[rest_api_evaluation.md](archive/rest_api_evaluation.md)** - Historical REST API design evaluation

## 🎯 Current Development Focus

See the canonical **[development roadmap](development.md#development-roadmap)**. Shipped work belongs in **[changelog.md](changelog.md)**.

## ✅ Shipped Work

See **[changelog.md](changelog.md)**, the single source of truth for shipped features and current verification totals.

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

1. **Root README.md** - Concise project overview and entry point
2. **programs.md** - Canonical executable and HTTP API behavior
3. **deployment.md** - Canonical operational, security, backup, and recovery guidance
4. **development.md** - Contributor workflow and active roadmap
5. **changelog.md** - Canonical shipped-work log and current verification totals
6. **archive/** - Historical evaluations and completed proposals; never the current reference
7. **Link instead of copying** - Keep detailed facts in their canonical document