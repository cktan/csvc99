import sys

with open(sys.argv[1]) as f1:
    f1 = eval(f1.read())

with open(sys.argv[2]) as f2:
    f2 = eval(f2.read())

if f1 != f2:
    sys.exit(1)
