import sys

rps, cap = (int(v) for v in sys.stdin.read().split())
print(-(-rps // cap) if rps > 0 else 0)
