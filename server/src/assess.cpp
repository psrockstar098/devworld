#include "assess.h"

#include "http.h"
#include "json.h"
#include "util.h"

namespace dw {

bool AssessClient::assess(const std::string& runtime,
                          const std::vector<AssessFile>& files,
                          const std::vector<AssessTest>& tests, AssessResult* out,
                          std::string* err) const {
  if (url_.empty()) {
    if (err) *err = "grading backend not configured (set WORKER_URL)";
    return false;
  }
  JVal body = jobj();
  jset(body, "runtime", jstr(runtime));
  JVal fa = jarr();
  for (const auto& f : files) {
    JVal fj = jobj();
    jset(fj, "path", jstr(f.path));
    jset(fj, "content", jstr(f.content));
    fa.arr.push_back(std::move(fj));
  }
  jset(body, "files", fa);
  JVal ta = jarr();
  for (const auto& t : tests) {
    JVal tj = jobj();
    jset(tj, "name", jstr(t.name));
    jset(tj, "stdin", jstr(t.stdin));
    jset(tj, "expected_stdout", jstr(t.expectedStdout));
    jset(tj, "hidden", jbool(t.hidden));
    ta.arr.push_back(std::move(tj));
  }
  jset(body, "tests", ta);

  std::string base = url_;
  while (!base.empty() && base.back() == '/') base.pop_back();
  std::map<std::string, std::string> headers;
  headers["Content-Type"] = "application/json";
  if (!apiKey_.empty()) headers["x-api-key"] = apiKey_;

  HttpClientResponse res;
  if (!httpRequest(base + "/assess", "POST", headers, dumpJson(body), &res, err,
                   60000))
    return false;
  if (res.status < 200 || res.status >= 300) {
    if (err) *err = "worker error " + std::to_string(res.status) + ": " +
                    res.body.substr(0, 200);
    return false;
  }
  JVal j;
  try {
    j = parseJson(res.body);
  } catch (const std::exception& e) {
    if (err) *err = std::string("worker returned bad JSON: ") + e.what();
    return false;
  }
  out->passed = j.i("passed", 0);
  out->total = j.i("total", (int)tests.size());
  out->durationMs = j.i("durationMs", 0);
  out->timedOut = j.boolean("timedOut", false);
  out->results.clear();
  const JVal& results = j.at("results");
  if (results.type == JVal::ARR) {
    for (const auto& r : results.arr) {
      AssessTestResult tr;
      tr.name = r.s("name");
      tr.passed = r.boolean("passed", false);
      tr.hidden = r.boolean("hidden", false);
      if (!tr.hidden) tr.actual = r.s("actual");
      tr.errorKind = r.s("errorKind");
      out->results.push_back(std::move(tr));
    }
  }
  return true;
}

}  // namespace dw
