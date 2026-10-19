import sys

def square(n):
    return n * n

nums = sys.stdin.read().split()
for x in nums:
    print(square(int(x)))
