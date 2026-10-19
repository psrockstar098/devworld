import sys

qty = int(sys.stdin.read().strip() or 0)
print("RESTOCK" if qty < 5 else "OK")
