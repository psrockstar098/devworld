# Zig toolchain. ziglang.org publishes no official Docker image, so we
# install the official release tarball. Debian slim is used because the
# upstream Zig binary targets glibc.
FROM debian:bookworm-slim
ARG ZIG_VERSION=0.15.1
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl xz-utils ca-certificates \
 && curl -fsSL -o /tmp/zig.tar.xz \
      "https://ziglang.org/download/${ZIG_VERSION}/zig-x86_64-linux-${ZIG_VERSION}.tar.xz" \
 && tar -xf /tmp/zig.tar.xz -C /opt \
 && rm /tmp/zig.tar.xz \
 && ln -s "/opt/zig-x86_64-linux-${ZIG_VERSION}/zig" /usr/local/bin/zig \
 && zig version \
 && apt-get purge -y curl xz-utils \
 && apt-get autoremove -y \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /work
