import sys

print(sum(int(v) for v in sys.stdin.read().split()) % 256)
