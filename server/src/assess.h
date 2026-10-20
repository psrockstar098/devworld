#pragma once
// assess.h — the client for the sandbox worker's grading endpoint. The game
// server never executes player code.
#include <string>
#include <vector>

namespace dw {

struct AssessTest {
  std::string name;
  std::string stdin;
  std::string expectedStdout;
  bool hidden = false;
};

struct AssessFile {
  std::string path;
  std::string content;
};

struct AssessTestResult {
  std::string name;
  bool passed = false;
  bool hidden = false;
  std::string actual;
  std::string errorKind;
};

struct AssessResult {
  int passed = 0;
  int total = 0;
  int durationMs = 0;
  bool timedOut = false;
  std::vector<AssessTestResult> results;
};

class AssessClient {
 public:
  AssessClient(std::string url, std::string apiKey = "")
      : url_(std::move(url)), apiKey_(std::move(apiKey)) {}

  bool assess(const std::string& runtime, const std::vector<AssessFile>& files,
              const std::vector<AssessTest>& tests, AssessResult* out,
              std::string* err) const;

  bool configured() const { return !url_.empty(); }

 private:
  std::string url_;
  std::string apiKey_;
};

}  // namespace dw
