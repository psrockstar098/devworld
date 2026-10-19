# Embedded RocksDB proof image: real librocksdb, real C++ program.
# Built and run by `infra/scripts/rocksdb-embedded-test.sh`.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      g++ make pkg-config librocksdb-dev ca-certificates \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY rocksdb_smoke.cpp .

# Prefer pkg-config; fall back to the explicit library set shipped by the
# distro package (rocksdb + its compression dependencies).
RUN set -eux; \
    if pkg-config --exists rocksdb; then \
      CFLAGS="$(pkg-config --cflags rocksdb)"; LIBS="$(pkg-config --libs rocksdb)"; \
    else \
      CFLAGS=""; LIBS="-lrocksdb -lpthread -ldl -lz -lsnappy -lbz2 -llz4 -lzstd"; \
    fi; \
    g++ -std=c++17 -O2 $CFLAGS rocksdb_smoke.cpp -o rocksdb_smoke $LIBS; \
    ./rocksdb_smoke

CMD ["./rocksdb_smoke"]
