import sys
d = sys.stdin.read().split()
n = int(d[0]); v = list(map(int, d[1:n + 1])); m = max(v)
print(m, v.count(m))
