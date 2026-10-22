import sys

fr, fg, fb, br, bg, bb, a = (int(v) for v in sys.stdin.read().split())
out = [
    (fr * a + br * (100 - a)) // 100,
    (fg * a + bg * (100 - a)) // 100,
    (fb * a + bb * (100 - a)) // 100,
]
print(" ".join(str(v) for v in out))
