# TypeScript 5.9 toolchain. No official tsc image exists, and containers run
# with --network none, so the compiler must be baked into the image.
FROM node:22-slim
RUN npm install -g typescript@5.9 \
 && tsc --version
WORKDIR /work
