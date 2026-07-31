#!/usr/bin/env python3
"""Compile every fenced native snippet in docs against the public headers."""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
cxx = sys.argv[2]
cc = sys.argv[3]
blocks = []
for document in sorted((root / "docs").glob("*.md")):
    text = document.read_text()
    for match in re.finditer(r"```(cpp|c|c\+\+)\n(.*?)```", text, re.S):
        line = text[: match.start()].count("\n") + 1
        blocks.append((document, line, match.group(1), match.group(2)))

if not blocks:
    raise SystemExit("no native documentation snippets found")

with tempfile.TemporaryDirectory(prefix="labios-doc-snippets-") as directory:
    for index, (document, line, language, source) in enumerate(blocks):
        extension = ".c" if language == "c" else ".cpp"
        path = Path(directory) / f"snippet-{index}{extension}"
        path.write_text(source)
        compiler = cc if language == "c" else cxx
        standard = "-std=c11" if language == "c" else "-std=c++20"
        command = [compiler, standard, "-Wall", "-Wextra", "-Werror",
                   "-I", str(root / "include"), "-fsyntax-only", str(path)]
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            print(f"{document.relative_to(root)}:{line} failed", file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            raise SystemExit(result.returncode)
print(f"compiled {len(blocks)} native documentation snippets")
