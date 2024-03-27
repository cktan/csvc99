import sys
import subprocess

def run(cmd, inp=''):
    p = subprocess.Popen(cmd, shell=False, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    p.stdin.write(inp.encode('utf-8'))
    p.stdin.close()
    out = p.stdout.read()
    rc = p.wait()
    return (rc, out)

TEST = r'''
abcd     => abcd
ab"c"d   => abcd
ab""c""d => abcd
ab\cd    => ab\cd
ab\\cd   => ab\\cd
"abcd"   => abcd
"ab\cd"  => ab\cd
"ab\"cd" => ab"cd
"ab\\cd" => ab\cd
"ab""cd" => abcd
"ab" "cd" => ab cd
'''

TEST = TEST.split("\n")
TEST = [tuple(line.split("=>", 2)) for line in TEST if line]
TEST = [(a.strip(), b.strip()) for (a, b) in TEST]
for (a, b) in TEST:
    out = run(["./csvecho", a])
    if out[0] != 0:
        print(f"{a} ... failed\n")
        sys.exit(1)
    res = out[1].rstrip()
    if res != b.encode('utf-8'):
        print(f"{a} ... {res} != {b}")
        sys.exit(1)
    print(f"{a} => {b} (ok)")
