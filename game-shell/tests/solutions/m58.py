import sys


def double_and_add(values, bonus):
    total = 0
    for v in values:
        total = total + v * 2
    return total + bonus


def main():
    lines = sys.stdin.read().strip().splitlines()
    bonus = int(lines[0])
    values = [int(v) for v in lines[1].split()]
    print(double_and_add(values, bonus))


main()
