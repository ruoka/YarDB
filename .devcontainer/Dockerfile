# syntax=docker/dockerfile:1.7
FROM ubuntu:24.04

RUN set -ex && \
    apt-get update && \
    apt-get install -y --no-install-recommends wget gnupg ca-certificates && \
    rm -rf /var/lib/apt/lists/*

# LLVM 23 is the apt.llvm.org development snapshot (unversioned suite).
# There is no llvm-toolchain-noble-23 — use llvm-toolchain-noble + *-23 packages.
RUN set -ex && \
    wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/apt-llvm.gpg && \
    echo "deb [signed-by=/usr/share/keyrings/apt-llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble main" > /etc/apt/sources.list.d/apt.llvm.org.list && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        git cmake make file jq libssl-dev \
        clang-23 clang-tools-23 lldb-23 lld-23 libc++-23-dev libc++abi-23-dev && \
    rm -rf /var/lib/apt/lists/*

RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-23 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-23 100 && \
    update-alternatives --install /usr/bin/lld lld /usr/bin/lld-23 100

ENV CC=clang-23 \
    CXX=clang++-23 \
    LDFLAGS="-Wl,--push-state,--no-as-needed -lc++ -lc++abi -lc++experimental -Wl,--pop-state -pthread -ldl"

ARG USERNAME=vscode
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN existing_group="$(getent group "$USER_GID" | cut -d: -f1 || true)" && \
    if [ -z "$existing_group" ]; then \
        groupadd --gid "$USER_GID" "$USERNAME"; \
    elif [ "$existing_group" != "$USERNAME" ]; then \
        groupmod -n "$USERNAME" "$existing_group"; \
    fi && \
    existing_user="$(getent passwd "$USER_UID" | cut -d: -f1 || true)" && \
    if [ -n "$existing_user" ] && [ "$existing_user" != "$USERNAME" ]; then \
        usermod -l "$USERNAME" "$existing_user" && \
        usermod -d "/home/$USERNAME" -m "$USERNAME"; \
    fi && \
    if ! id -u "$USERNAME" >/dev/null 2>&1; then \
        useradd --uid "$USER_UID" --gid "$USER_GID" -m "$USERNAME"; \
    fi

USER $USERNAME
