import sys

mode, a, b, c, d = (int(v) for v in sys.stdin.read().split())
print((mode << 4) | (a << 3) | (b << 2) | (c << 1) | d)
