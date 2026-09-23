import ast
import io
import sys

for f in sys.argv[1:]:
    ast.parse(io.open(f, encoding="utf-8").read())
    print(f, "语法 OK")
