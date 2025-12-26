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
- **[handler_refactoring_proposal.md](handler_refactoring_proposal.md)** - HTTP handler code consolidation proposal
- **[index_api_proposal.md](index_api_proposal.md)** - Database indexing API design
- **[rest_api_evaluation.md](rest_api_evaluation.md)** - REST API design evaluation

### 📁 Archive
- **[archive/](archive/)** - Completed/historical documentation
  - **[namespace_design_proposal.md](archive/namespace_design_proposal.md)** - ✅ **COMPLETED** namespace refactoring (implemented Dec 2025)
  - **[code_review.md](archive/code_review.md)** - Historical peer review from engine/index/metadata modules

## 🎯 Current Development Focus

Based on recent architectural review, current priorities include:

1. **🔐 Security & Authentication** - Enterprise-grade auth system (JWT, RBAC)
2. **📊 Observability & Monitoring** - Prometheus metrics, structured logging, health checks
3. **🔒 TLS Proxy Implementation** - Cloud-native TLS termination approach
4. **🌐 Net Module Modernization** - Convert to C++23 modules, remove headers

See **[development.md](development.md)** for detailed roadmap and implementation plans.

## 📖 Reading Guide

### New to YarDB?
1. Start with root **[README.md](../README.md)** for project overview
2. Read **[programs.md](programs.md)** to understand available tools
3. Check **[development.md](development.md)** for build/test instructions

### Contributing?
1. Review **[development.md](development.md)** for development workflows
2. Check **[code_review.md](code_review.md)** for contribution guidelines
3. See **[project_organization.md](project_organization.md)** for code organization

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

