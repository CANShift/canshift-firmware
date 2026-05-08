// no_float_printf.c — Override newlib's float-capable vfprintf with the
// integer-only variant so the linker drops `_dtoa_r`, `_strtod_l`, and the
// ~25 KB of float-formatting code in libc.a. See issues #305 / #405.
//
// Side effects:
//   - Any `%f`/`%g`/`%e` in a format string passed to the printf family at
//     runtime is rendered like an unknown specifier (the literal letter is
//     emitted). The firmware no longer contains any such format strings —
//     callers that need decimal formatting use FloatFormat:: helpers in
//     format_float.h.
//   - ESP-IDF / arduino-esp32 / NimBLE log lines that contain `%f` will lose
//     the float — acceptable trade-off for ~25 KB flash, and CORE_DEBUG_LEVEL=1
//     already drops most of them in production.
//
// Implementation: provide strong, externally-visible definitions of the
// float-capable entry points that delegate to the integer-only ones. Strong
// symbols in object files take precedence over the same name in archive
// members (libc.a), so the float-formatting code is never linked in.

#include <stdio.h>
#include <stdarg.h>
#include <reent.h>

// Integer-only variants live in libc.a / ROM and are always linked in.
extern int _vfiprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);
extern int _svfiprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);

int _vfprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return _vfiprintf_r(ptr, fp, fmt, ap);
}

int _svfprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return _svfiprintf_r(ptr, fp, fmt, ap);
}

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
    return _vfiprintf_r(_REENT, fp, fmt, ap);
}
