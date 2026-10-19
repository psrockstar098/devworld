import sys

def line_total(qty, cents):
    return qty * cents

def money(cents):
    return f"${cents // 100}.{cents % 100:02d}"

rows = [line.split() for line in sys.stdin.read().strip().splitlines() if line.strip()]
total = 0
for r in rows:
    qty, cents = int(r[0]), int(r[1])
    sub = line_total(qty, cents)
    total += sub
    print(f"{qty} x {cents}: {money(sub)}")
print(f"TOTAL: {money(total)}")
