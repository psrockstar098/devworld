import sys

parts = sys.stdin.read().split()
for i in range(0, len(parts), 2):
    print(f"{parts[i]}: ${parts[i + 1]}")
