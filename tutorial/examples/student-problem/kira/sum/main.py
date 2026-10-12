with open("sum.in") as f:
    data = f.read().split()
n = int(data[0])
with open("sum.out", "w") as g:
    g.write(str(sum(map(int, data[1:n + 1]))) + "\n")
