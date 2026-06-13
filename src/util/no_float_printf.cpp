
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
}
