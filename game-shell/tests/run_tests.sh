#!/bin/bash
# Native C++ tests + mission reference-solution verification.
# Usage: ./tests/run_tests.sh [doctest args...]
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${CXX:-g++}"
OUT="tests/build"
mkdir -p "$OUT"

echo "== building native tests =="
"$CXX" -std=c++17 -O1 -Wall -Wextra \
  -I src -I tests -I tests/third_party \
  tests/test_main.cpp tests/test_json.cpp tests/test_progression.cpp \
  tests/test_world_model.cpp tests/test_ambience.cpp \
  tests/test_equipment.cpp tests/test_profile.cpp tests/test_settings.cpp \
  tests/test_regions.cpp tests/test_dungeons.cpp tests/test_housing.cpp \
  tests/test_social.cpp tests/test_company.cpp tests/test_portfolio.cpp \
  tests/test_curriculum.cpp tests/test_mastery.cpp \
  tests/test_capability.cpp \
  tests/test_unlocks.cpp \
  tests/test_achievements.cpp \
  tests/test_rewards.cpp \
  tests/test_careers.cpp \
  tests/test_content.cpp \
  src/progression.cpp src/world_model.cpp src/ambience.cpp \
  src/equipment.cpp src/profile.cpp src/settings.cpp \
  src/regions.cpp src/dungeons.cpp src/housing.cpp \
  src/social.cpp src/company.cpp src/portfolio.cpp \
  src/curriculum.cpp src/mastery.cpp src/capability.cpp src/unlocks.cpp \
  src/achievements.cpp src/rewards.cpp src/careers.cpp \
  -o "$OUT/tests"

echo "== running native tests =="
"$OUT/tests" "$@"

echo "== verifying missions (starters fail, reference solutions pass) =="
python3 tests/verify_missions.py

echo "== all tests passed =="
