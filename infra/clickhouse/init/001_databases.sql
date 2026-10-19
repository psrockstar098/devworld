-- DEVWORLD ClickHouse bootstrap — databases.
-- Runs once, on first start of an empty data volume (docker-entrypoint-initdb.d).

CREATE DATABASE IF NOT EXISTS devworld;
