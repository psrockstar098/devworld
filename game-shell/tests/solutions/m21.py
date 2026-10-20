import sys

nums = [int(x) for x in sys.stdin.read().split()]
bad = -1
for i in range(1, len(nums)):
    if nums[i] < nums[i - 1]:
        bad = i
        break
print(bad)
