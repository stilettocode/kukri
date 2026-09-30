"""Compare synthetic fixtures with real language parsers when available."""
import ast
import pathlib
import shutil
import subprocess
import sys

exe = pathlib.Path(sys.argv[1]).resolve()
def clean(language, source):
    result = subprocess.run([str(exe), "clean", "--language", language],
                            input=source.encode(), capture_output=True, check=True)
    assert not result.stderr
    return result.stdout.decode()

python_sources = [
    'x = 1 #k note\ntext = "#k literal"\n',
    'text = """#k triple\ntext"""\nx = 1 #k note\n',
    'text = rf"literal #k {42:#k}" #k note\n',
    'text = "continued\\\r\n#k literal"\r\n#k note\r\n',
    'text = f"{42!r:>{3}}" #k note\n',
]
if sys.version_info >= (3, 12):
    python_sources += [
        'text = f"{42 #k note\n}"\n',
        'text = f"{f\'{42 #k inner\n}\'}"\n',
        'text = f"{42:{3 #k width\n}}"\n',
        'text = f"{ {\"#k\": 42}[\"#k\"] #k note\n}"\n',
    ]
for source in python_sources:
    output = clean("python", source)
    assert ast.dump(ast.parse(source)) == ast.dump(ast.parse(output)), (source, output)
print(f"Python AST equivalence: {len(python_sources)} fixtures")

node = shutil.which("node")
if node:
    sources = [
        'const ok=true; if (ok) /[/*k]/.test("k"); //k note\nconsole.log("ok");',
        'const value=42; console.log(`text //k ${value /*k note k*/}`);',
        'const value=42; console.log(`outer ${`inner ${value //k note\n}`} tail`);',
        'console.log(({return: 4}).return / 2); //k note\n',
        'console.log("continued\\\r\n//k literal"); //k note\n',
    ]
    for source in sources:
        output = clean("javascript", source)
        before = subprocess.run([node, "-e", source], capture_output=True, check=True)
        after = subprocess.run([node, "-e", output], capture_output=True, check=True)
        assert before.stdout == after.stdout, (source, output)
    print(f"JavaScript execution equivalence: {len(sources)} fixtures")
else:
    print("Node unavailable; JavaScript execution comparison skipped")
