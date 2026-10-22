import sys


def count_evens(values):
    count = 0
    for v in values:
        if v % 2 == 0:
            count = count + 1
    return count


def main():
    values = [int(v) for v in sys.stdin.read().split()]
    print(count_evens(values))


main()
