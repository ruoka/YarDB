# Deployment

## Building for Production

```bash
# Clean build
make clean
make build

# Binaries are in build/bin/
./build/bin/yardb
```

## Docker

See `.devcontainer/Dockerfile` for containerized build environment.

## Runtime Requirements

- C++ runtime libraries (libc++)
- Network access for HTTP server
- File system access for database storage

