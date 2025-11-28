# Deployment

## Building for Production

```bash
# Clean build
./tools/CB.sh release clean
./tools/CB.sh release build

# Binaries are in build-{os}-release/bin/
./build-darwin-release/bin/yardb
# or
./build-linux-release/bin/yardb
```

## Docker

See `.devcontainer/Dockerfile` for containerized build environment.

## Runtime Requirements

- C++ runtime libraries (libc++)
- Network access for HTTP server
- File system access for database storage

