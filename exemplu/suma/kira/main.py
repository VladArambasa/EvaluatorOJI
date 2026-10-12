with open("suma.in") as f:
    date = f.read().split()
n = int(date[0])
with open("suma.out", "w") as g:
    g.write(str(sum(map(int, date[1:n + 1]))) + "\n")
