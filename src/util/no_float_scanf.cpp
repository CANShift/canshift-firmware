
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
}
