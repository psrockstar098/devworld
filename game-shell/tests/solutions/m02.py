import sys

def total(prices):
    s = 0
    for p in prices:
        s += p
    return s

prices = [int(x) for x in sys.stdin.read().split()]
print(total(prices))
