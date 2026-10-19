// DEVWORLD — embedded RocksDB proof.
//
// The Kvrocks container gives us a networked RocksDB engine, but some state is
// better embedded in-process (no hop, no daemon). This program links the real
// RocksDB C++ library and exercises the operations we depend on, printing a
// JSON result. It exits non-zero on the first broken invariant, so it doubles
// as a container health gate:
//
//   docker compose --profile tools run --rm --build rocksdb-embedded
//
// It is intentionally dependency-free (no test framework) so it can build on a
// bare `ubuntu` + `librocksdb-dev` image.

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/slice.h"

namespace {

int failures = 0;

void check(bool cond, const char* what) {
  if (!cond) {
    std::fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}

std::string jsonEscape(const std::string& s) {
  std::string out;
  for (char c : s) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  return out;
}

}  // namespace

int main() {
  const std::string path = "/tmp/rocksdb-smoke";
  system(("rm -rf " + path).c_str());

  rocksdb::Options options;
  options.create_if_missing = true;

  rocksdb::DB* raw = nullptr;
  rocksdb::Status s = rocksdb::DB::Open(options, path, &raw);
  if (!s.ok()) {
    std::fprintf(stderr, "FAIL: open: %s\n", s.ToString().c_str());
    return 2;
  }
  std::unique_ptr<rocksdb::DB> db(raw);

  const int n = 1000;
  for (int i = 0; i < n; i++) {
    const std::string key = "player:" + std::to_string(i);
    const std::string val = "{\"xp\":" + std::to_string(i * 10) + "}";
    check(db->Put(rocksdb::WriteOptions(), key, val).ok(), "put");
  }
  check(static_cast<int>(failures) == 0, "all puts succeeded");

  // Point read.
  std::string got;
  s = db->Get(rocksdb::ReadOptions(), "player:42", &got);
  check(s.ok(), "get player:42");
  check(got == "{\"xp\":420}", "get returns what was written");

  // Missing key must report NotFound, not an error.
  s = db->Get(rocksdb::ReadOptions(), "player:99999", &got);
  check(s.IsNotFound(), "missing key is NotFound");

  // Ordered range scan.
  int scanned = 0;
  {
    std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(rocksdb::ReadOptions()));
    for (it->Seek("player:"); it->Valid(); it->Next()) scanned++;
    check(it->status().ok(), "iterator clean");
  }
  check(scanned == n, "range scan sees every key");

  // Delete half, then count again.
  for (int i = 0; i < n; i += 2) {
    check(db->Delete(rocksdb::WriteOptions(), "player:" + std::to_string(i)).ok(), "delete");
  }
  int remaining = 0;
  {
    std::unique_ptr<rocksdb::Iterator> it(db->NewIterator(rocksdb::ReadOptions()));
    for (it->Seek("player:"); it->Valid(); it->Next()) remaining++;
  }
  check(remaining == n / 2, "deletes are reflected");

  // Atomic write batch.
  {
    rocksdb::WriteBatch batch;
    batch.Put("kv:a", "1");
    batch.Put("kv:b", "2");
    batch.Delete("kv:a");
    check(db->Write(rocksdb::WriteOptions(), &batch).ok(), "write batch");
    std::string v;
    check(db->Get(rocksdb::ReadOptions(), "kv:a", &v).IsNotFound(), "batched delete applied");
    check(db->Get(rocksdb::ReadOptions(), "kv:b", &v).ok() && v == "2", "batched put applied");
  }

  db.reset();

  // Reopen: durability across process restart.
  {
    rocksdb::DB* raw2 = nullptr;
    s = rocksdb::DB::Open(options, path, &raw2);
    check(s.ok(), "reopen");
    if (s.ok()) {
      std::unique_ptr<rocksdb::DB> db2(raw2);
      std::string v;
      check(db2->Get(rocksdb::ReadOptions(), "player:1", &v).ok() && v == "{\"xp\":10}",
            "value survives reopen");
    }
  }

  std::printf(
      "{\"ok\":%s,\"engine\":\"rocksdb\",\"written\":%d,\"remaining_after_delete\":%d,"
      "\"failures\":%d}\n",
      failures == 0 ? "true" : "false", n, remaining, failures);
  return failures == 0 ? 0 : 1;
}
