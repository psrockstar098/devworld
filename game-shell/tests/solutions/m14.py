import sys

sales = [int(x) for x in sys.stdin.read().split()]
print(f"count: {len(sales)}")
print(f"total: {sum(sales)}")
print(f"best: {max(sales) if sales else 0}")
