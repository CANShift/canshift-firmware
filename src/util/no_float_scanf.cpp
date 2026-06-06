// no_float_scanf.cpp — Override newlib's float-capable vfscanf entry points
// with the integer-only variants so the linker drops `__ssvfscanf_r` (~9.2 KB)
// and `_strtod_l` (~3.3 KB) — ~12 KB combined. Mirrors no_float_printf.cpp.
// See issue #1249 (F-3).
//
// Side effects:
//   - Any `%f`/`%g`/`%e` conversion in a format string passed to the scanf
//     family at runtime is treated like an unknown specifier — the matching
//     argument is left untouched (silent zero / whatever the caller had).
//     Intentional: the firmware never reads floats from strings. All numeric
//     parsing flows go through integer helpers — `parseColor`,
//     `decodeHexBytes`, `strtoul`, `strtol`, ArduinoJson's number parser
//     (which does not call newlib scanf) — so no production code path
//     exercises the float branch.
//   - If a future caller ever does need to parse a float with sscanf/scanf,
//     the result will be a silent zero rather than a link error. Use one of
//     the existing helpers above, or add a dedicated parser, rather than
//     dropping these stubs.
//
// Implementation: provide strong, externally-visible definitions of the
// float-capable entry points that delegate to the integer-only ones. Strong
// symbols in object files take precedence over the same name in archive
// members (libc.a), so the float-parsing code (and its `_strtod_r` /
// `_strtod_l` dependency) is never linked in.
//
// The five symbols below MUST keep C linkage — newlib looks them up
// unmangled — hence the `extern "C"` block.

#include <stdio.h>
#include <stdarg.h>
#include <reent.h>

extern "C" {

// Integer-only variants live in libc.a / ROM and are always linked in.
int _vfiscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);
int __ssvfiscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);
int __svfiscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);

// String-based sscanf path — heaviest single contributor (~9.2 KB).
int __ssvfscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return __ssvfiscanf_r(ptr, fp, fmt, ap);
}

// FILE-based fscanf path — pulls `_strtod_r` via the same float branch.
int __svfscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return __svfiscanf_r(ptr, fp, fmt, ap);
}

int _vfscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return _vfiscanf_r(ptr, fp, fmt, ap);
}

int vfscanf(FILE *fp, const char *fmt, va_list ap) {
    return _vfiscanf_r(_REENT, fp, fmt, ap);
}

} // extern "C"
