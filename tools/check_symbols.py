#!/usr/bin/env python3
"""Static coverage check of the loader symbol table against spike-c/SYMBOLS.md.

Reads:
  - spike-c/SYMBOLS.md: the 123 dynamic imports of liblooking_glass.so (section 3)
    and the EGL / GLES2 / GLES3 / AAudio names looked up at run time (section 6);
  - source/dynlib.c: the default_dynlib[] table, preprocessed for the build
    configuration (NDK_PORT off, USE_SCELIBC_IO on);
  - lib/vitagl/source/lookup.c: vitaGL's own proc table (fallback of
    soloader_lookup_symbol() for gl* names).
Prints what each name resolves to and fails (exit 1) if something that must
resolve does not, or if a needed import is still mapped to a silent ret0/ret1.

Usage: spike-c/loader/tools/check_symbols.py [SYMBOLS.md] [loader dir]
No console needed; this is the static half of the jalon 2 check (the dynamic
half is the "import check" line the loader prints at boot).
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
LOADER = Path(sys.argv[2]) if len(sys.argv) > 2 else HERE.parent
SYMBOLS = Path(sys.argv[1]) if len(sys.argv) > 1 else LOADER.parent / "SYMBOLS.md"

DEFINED = {"USE_SCELIBC_IO"}            # NDK_PORT is OFF for this port
EXPECTED_UNRESOLVED_IMPORTS = {"copy_file_range", "splice"}   # weak, NULL on purpose
SILENT = {"ret0", "ret1", "retminus1"}


def symbols_sections(text):
    """Return {section title: [names]} for every '### title (N)' list."""
    out = {}
    cur = None
    for line in text.splitlines():
        m = re.match(r"^#{3,4} (.+?) \((\d+)\)\s*$", line)
        if m:
            cur = m.group(1)
            out.setdefault(cur, [])
            continue
        if line.startswith("#"):
            cur = None
            continue
        if cur and line.startswith("`"):
            out[cur] += re.findall(r"`([A-Za-z_][A-Za-z0-9_]*)`", line)
    return out


def preprocess(src):
    """Tiny #ifdef/#ifndef/#else/#endif evaluator for dynlib.c."""
    keep = []
    stack = []
    for line in src.splitlines():
        s = line.strip()
        m = re.match(r"#\s*(ifdef|ifndef)\s+(\w+)", s)
        if m:
            cond = (m.group(2) in DEFINED) == (m.group(1) == "ifdef")
            stack.append(cond)
            continue
        if re.match(r"#\s*if\b", s):
            stack.append(True)
            continue
        if re.match(r"#\s*else", s):
            stack[-1] = not stack[-1]
            continue
        if re.match(r"#\s*endif", s):
            stack.pop()
            continue
        if all(stack):
            keep.append(line)
    return "\n".join(keep)


def main():
    text = SYMBOLS.read_text(encoding="utf-8")
    sec3 = text[text.index("\n## 3."):text.index("\n## 4.")]
    sec6 = text[text.index("\n## 6."):text.index("\n## 7.")]
    sym = symbols_sections(sec3)
    sym.update(symbols_sections(sec6))
    imports = []
    for title in ["libc", "libm", "libOpenSLES", "libdl", "libandroid", "liblog",
                  "libc (API 28+)", "libc (API 34+)"]:
        imports += [n for n in sym.get(title, []) if n not in imports]
    by_name = {lib: sym.get(f"{lib}, par nom", []) for lib in ["libEGL", "libGLESv2", "libGLESv3", "libaaudio"]}

    dyn = preprocess((LOADER / "source/dynlib.c").read_text(encoding="utf-8"))
    table = {}
    for m in re.finditer(r'\{\s*"([^"]+)"\s*,\s*\(uintptr_t\)\s*&?\s*([A-Za-z0-9_]+)\s*\}', dyn):
        table.setdefault(m.group(1), m.group(2))
    vgl = set(re.findall(r'\{"(gl[A-Za-z0-9_]+)",', (LOADER / "lib/vitagl/source/lookup.c").read_text()))

    def resolve(name):
        if name in table:
            return "table:" + table[name]
        if name.startswith("gl"):
            stripped = re.sub(r"(EXT|ARB|OES)$", "", name)
            if stripped in vgl:
                return "vitaGL"
        return None

    errors = 0
    print(f"SYMBOLS.md: {len(imports)} imports, " +
          ", ".join(f"{k} {len(v)}" for k, v in by_name.items()) + " looked up by name")
    print(f"dynlib.c table: {len(table)} entries; vitaGL proc table: {len(vgl)} gl* names\n")

    print("== Dynamic imports (resolved by so_resolve at load time)")
    unresolved = [n for n in imports if not resolve(n)]
    silent = [n for n in imports if table.get(n) in SILENT]
    for n in unresolved:
        tag = "expected (weak, stays NULL)" if n in EXPECTED_UNRESOLVED_IMPORTS else "MISSING"
        print(f"  unresolved: {n:28s} {tag}")
        if n not in EXPECTED_UNRESOLVED_IMPORTS:
            errors += 1
    for n in silent:
        print(f"  SILENT STUB: {n} -> {table[n]}")
        errors += 1
    print(f"  {len(imports) - len(unresolved)}/{len(imports)} resolved, {len(silent)} silent stub(s)\n")

    for lib, must in [("libEGL", True), ("libGLESv2", True), ("libGLESv3", False), ("libaaudio", False)]:
        names = by_name[lib]
        ok = [n for n in names if resolve(n)]
        miss = [n for n in names if not resolve(n)]
        print(f"== {lib} by name: {len(ok)}/{len(names)} served")
        for n in names:
            r = resolve(n)
            if must and not r:
                print(f"  MISSING {n}")
                errors += 1
            elif r and table.get(n) in SILENT:
                print(f"  SILENT STUB {n} -> {table[n]}")
                errors += 1
        if not must:
            if ok:
                print("  served (vitaGL implements them): " + " ".join(ok))
            if miss:
                print("  NULL on purpose: " + " ".join(miss))
        print()

    print("RESULT:", "OK" if errors == 0 else f"{errors} problem(s)")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
