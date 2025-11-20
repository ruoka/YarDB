# Makefile Variable Naming Conventions

## General Rule

**UPPERCASE** = Public interface, user-configurable, exported  
**lowercase** = Internal implementation, not meant to be overridden

## When to Use UPPERCASE Variables

Use uppercase for variables that:

1. **User can override from command line**
   ```makefile
   make PREFIX=/custom/path build
   make CXX=clang++-21 build
   ```

2. **Exported to sub-makes**
   ```makefile
   export CC CXX CXXFLAGS LDFLAGS
   export PCMFLAGS_SUBMODULE
   ```

3. **Part of the public build interface**
   - Compiler settings: `CC`, `CXX`, `CXXFLAGS`, `LDFLAGS`
   - Build configuration: `PREFIX`, `BUILD_DIR`, `OS`
   - Flags that affect the build: `PCMFLAGS_*`

4. **Environment variables or system settings**
   - `LLVM_PREFIX`, `SDKROOT`, `PATH`

## When to Use lowercase Variables

Use lowercase for variables that:

1. **Internal implementation details**
   ```makefile
   moduledir = $(PREFIX)/pcm
   objectdir = $(PREFIX)/obj
   sourcedir = $(project)
   ```

2. **Derived/computed values**
   ```makefile
   objects = $(modules:$(sourcedir)%.c++m=$(objectdir)%.o)
   libraries = $(filter-out $(librarydir)/libcryptic.a, ...)
   ```

3. **Temporary or intermediate variables**
   ```makefile
   empty :=
   space := $(empty) $(empty)
   submodule_deps_prev :=
   ```

4. **Not meant to be overridden by users**
   - File lists: `modules`, `sources`, `objects`
   - Directory paths: `stamproot`, `testdir`
   - Internal flags: `submodule_module_stamps`

## Current Makefile Analysis

### ✅ Correctly Uppercase (User-configurable/Exported)
- `PREFIX`, `BUILD_DIR` - Build output location
- `CC`, `CXX`, `CXXFLAGS`, `LDFLAGS` - Compiler settings
- `LLVM_PREFIX`, `SDKROOT` - System configuration
- `PCMFLAGS_*` - Module flags (exported to submodules)
- `SUBMAKE_FLAGS` - Passed to sub-makes

### ✅ Correctly Lowercase (Internal)
- `moduledir`, `objectdir`, `librarydir` - Derived paths
- `modules`, `sources`, `objects` - File lists
- `submodule_*_stamps` - Internal tracking
- `programs`, `submodules` - Project configuration
- `header_deps`, `module_depfile`, `all_sources`, `clang_scan_deps` - Internal dependency tracking

## Best Practices

1. **Be consistent** - Once you choose a convention, stick to it
2. **Document overridable variables** - Add comments for variables users might want to override
3. **Use `?=` for defaults** - Allows easy override: `VAR ?= default`
4. **Use `override` sparingly** - Only when you must force a value
5. **Export explicitly** - Only export what sub-makes actually need

## Example: Good Practice

```makefile
# User-configurable (UPPERCASE)
PREFIX ?= build-$(shell uname -s | tr '[:upper:]' '[:lower:]')
CXX ?= clang++

# Exported to sub-makes (UPPERCASE)
export CXX CXXFLAGS

# Internal implementation (lowercase)
moduledir = $(PREFIX)/pcm
objectdir = $(PREFIX)/obj
sources = $(wildcard src/*.cpp)
objects = $(sources:src/%.cpp=$(objectdir)/%.o)
```

## References

- [GNU Make Manual - Using Variables](https://www.gnu.org/software/make/manual/html_node/Using-Variables.html)
- Common convention in major projects (Linux kernel, LLVM, etc.)

