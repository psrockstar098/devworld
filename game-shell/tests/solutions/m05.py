import sys

def discount(price):
    return price * 0.8

prices = [float(x) for x in sys.stdin.read().split()]
for p in prices:
    print(discount(p))
