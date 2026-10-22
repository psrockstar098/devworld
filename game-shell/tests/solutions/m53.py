import sys

w = int(sys.stdin.read().strip())
print(1 if w < 600 else 2 if w < 900 else 3)
