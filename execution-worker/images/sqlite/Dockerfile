# SQLite CLI for the SQL runtime. No official sqlite image exists;
# alpine + apk is the smallest trustworthy base.
FROM alpine:3.21
RUN apk add --no-cache sqlite \
 && sqlite3 --version
WORKDIR /work
