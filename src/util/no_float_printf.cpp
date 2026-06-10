// Saves ~25 KB by overriding newlib's float printf with the integer-only
// variant (#305 / #405). Callers needing decimal formatting use the helpers
// in format_float.h. Strong symbols here outrank libc.a, so the float branch
// is never linked. Symbols keep C linkage — newlib looks them up unmangled.

#include <stdio.h>
#include <stdarg.h>
#include <reent.h>

extern "C" {

int _vfiprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);
int _svfiprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);

int _vfprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return _vfiprintf_r(ptr, fp, fmt, ap);
}

int _svfprintf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return _svfiprintf_r(ptr, fp, fmt, ap);
}

int vfprintf(FILE *fp, const char *fmt, va_list ap) {
    return _vfiprintf_r(_REENT, fp, fmt, ap);
}

} // extern "C"
