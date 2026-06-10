// Saves ~12 KB by overriding newlib's float-capable scanf entry points with
// the integer-only variants (#1249). Mirrors no_float_printf.cpp.
//
// SIDE EFFECT: any %f/%g/%e in a scanf format silently leaves the arg
// untouched. Firmware never parses floats from strings; numeric parsing
// flows through strtoul / strtol / ArduinoJson. Add a dedicated parser
// rather than dropping these stubs.
//
// Symbols keep C linkage — newlib looks them up unmangled.

#include <stdio.h>
#include <stdarg.h>
#include <reent.h>

extern "C" {

int _vfiscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);
int __ssvfiscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);
int __svfiscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap);

int __ssvfscanf_r(struct _reent *ptr, FILE *fp, const char *fmt, va_list ap) {
    return __ssvfiscanf_r(ptr, fp, fmt, ap);
}

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
