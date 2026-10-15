import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_DIRS = ("src", "include", "test", "sim", "bench")
SOURCE_EXTENSIONS = (".h", ".cpp", ".inc", ".c")
EXCLUDED = (
    "src/ui/fonts/",
    "test/native/vendor/",
    "test/native/shim/",
    "sim/shims/",
    "bench/host/shims/",
    "include/lv_conf.h",
)
LVGL_CONFIG = "include/lv_conf.h"
FRAMEWORK_SHIMS = ("portMUX_INITIALIZER_UNLOCKED",)

LITERAL_OR_COMMENT = re.compile(
    r'R"([^(]*)\(.*?\)\1"|"(?:[^"\\\n]|\\.)*"|\'(?:[^\'\\\n]|\\.)*\'|//[^\n]*|/\*.*?\*/',
    re.S,
)
UPPER_SNAKE = r"[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+|[A-Z]{2,}[0-9]*"
CONSTANT_DECLARATION = re.compile(
    r"\b(?:constexpr|const)\b[^;=(){}]*?\b("
    + UPPER_SNAKE
    + r")\s*(?:\[[^\]]*\]\s*)*(?:=|\{)"
)
ANONYMOUS_ENUM = re.compile(r"\benum\s*(?::\s*\w+\s*)?\{([^}]*)\}")
VALUE_DEFINE = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)(?!\()[ \t]+\S", re.M)
CONDITION = re.compile(r"^\s*#\s*(?:if|elif|ifdef|ifndef)\b(.*)$", re.M)


def blank_literals(match):
    return "\n" * match.group(0).count("\n")


def source_files():
    for top in SOURCE_DIRS:
        for directory, _, names in os.walk(os.path.join(ROOT, top)):
            for name in sorted(names):
                path = os.path.join(directory, name)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                if name.endswith(SOURCE_EXTENSIONS) and not rel.startswith(EXCLUDED):
                    yield rel, path


def read_code(path):
    with open(path, "r", encoding="utf-8") as handle:
        return LITERAL_OR_COMMENT.sub(blank_literals, handle.read())


def line_of(code, offset):
    return code.count("\n", 0, offset) + 1


def without_directives(code):
    return re.sub(r"^\s*#.*$", "", code, flags=re.M)


def upper_snake_constants(rel, code):
    body = without_directives(code)
    for match in CONSTANT_DECLARATION.finditer(body):
        yield f"{rel}:{line_of(body, match.start(1))}: constant {match.group(1)} is not kCamelCase"
    for match in ANONYMOUS_ENUM.finditer(body):
        for item in match.group(1).split(","):
            name = item.split("=")[0].strip()
            if re.fullmatch(UPPER_SNAKE, name):
                yield f"{rel}:{line_of(body, match.start())}: constant {name} is not kCamelCase"


def preprocessor_names(codes):
    names = set(re.findall(r"\w+", read_code(os.path.join(ROOT, LVGL_CONFIG))))
    for code in codes:
        for match in CONDITION.finditer(code):
            names.update(re.findall(r"\w+", match.group(1)))
    return names.union(FRAMEWORK_SHIMS)


def value_defines(rel, code, allowed):
    for match in VALUE_DEFINE.finditer(code):
        if match.group(1) not in allowed:
            yield (
                f"{rel}:{line_of(code, match.start(1))}: #define {match.group(1)} "
                "is a value, not a preprocessor condition or build flag; use constexpr"
            )


def main():
    codes = {rel: read_code(path) for rel, path in source_files()}
    allowed = preprocessor_names(codes.values())
    failures = []
    for rel, code in codes.items():
        failures.extend(upper_snake_constants(rel, code))
        failures.extend(value_defines(rel, code, allowed))
    if failures:
        sys.stderr.write("[naming] constants must be kCamelCase constexpr:\n")
        for line in failures:
            sys.stderr.write(f"[naming]   {line}\n")
        return 1
    print(f"[naming] {len(codes)} files follow the kCamelCase constexpr convention")
    return 0


if __name__ == "__main__":
    sys.exit(main())
