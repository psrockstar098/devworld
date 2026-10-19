import sys

items = sys.stdin.read().split()
for i, item in enumerate(items, start=1):
    print(f"Shelf {i}: {item}")
