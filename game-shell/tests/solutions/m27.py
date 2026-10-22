import sys

parts = sys.stdin.read().strip().split()
b0 = int(parts[0], 16)
total = int(parts[2], 16) * 256 + int(parts[3], 16)
print(f"version {b0 >> 4}, header {(b0 & 15) * 4} bytes, total {total} bytes")
