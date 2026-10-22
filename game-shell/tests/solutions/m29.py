import sys

mtu, payload = (int(x) for x in sys.stdin.read().split())
capacity = mtu - 20
n = -(-payload // capacity)
last = payload - (n - 1) * capacity
word = "fragment" if n == 1 else "fragments"
print(f"{n} {word}, last {last} bytes")
