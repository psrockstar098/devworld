# DEVWORLD game API. Bun runtime; talks to ClickHouse, Kvrocks and Redis over
# the `devworld` Docker network.
FROM oven/bun:1.4-slim

WORKDIR /app

COPY package.json bun.lock ./
RUN bun install --frozen-lockfile --production

COPY . .

ENV NODE_ENV=production
ENV PORT=8091
EXPOSE 8091

# Missions content is mounted at /app/content (see docker-compose.yml).
ENV CONTENT_DIR=/app/content

CMD ["bun", "server.ts"]
