# DEVWORLD execution worker. Needs the Docker CLI to spawn sibling
# containers through the mounted /var/run/docker.sock.
FROM oven/bun:1.4-slim
RUN apt-get update \
 && apt-get install -y --no-install-recommends curl ca-certificates \
 && curl -fsSL https://download.docker.com/linux/static/stable/x86_64/docker-28.5.1.tgz \
      | tar -xz -C /tmp \
 && mv /tmp/docker/docker /usr/local/bin/docker \
 && rm -rf /tmp/docker \
 && docker --version \
 && apt-get purge -y curl \
 && apt-get autoremove -y \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY package.json ./
RUN bun install
COPY . .
ENV PORT=8090
EXPOSE 8090
HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
  CMD ["bun", "-e", "const r = await fetch('http://localhost:8090/health'); process.exit(r.ok ? 0 : 1)"]
CMD ["bun", "server.ts"]
