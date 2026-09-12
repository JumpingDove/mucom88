
#include "codeconv_dummy.h"
#include <string.h>

CodeConvertDummy::CodeConvertDummy() {
}

CodeConvertDummy::~CodeConvertDummy() {
}

void CodeConvertDummy::Utf8ToSjis(const char *src, char *dest, int bufSize) {
    if (dest == NULL || bufSize <= 0) return;
    if (src == NULL) {
        dest[0] = '\0';
        return;
    }
    strncpy(dest, src, static_cast<size_t>(bufSize - 1));
    dest[bufSize - 1] = '\0';
}

void CodeConvertDummy::FromSjis(const char *src, char *dest, int bufSize) {
    Utf8ToSjis(src, dest, bufSize);
}
