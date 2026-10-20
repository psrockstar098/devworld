import sys

n = int(sys.stdin.read().strip() or 0)
while n > 0:
    print(n)
    n -= 1
print("Lift-off!")
