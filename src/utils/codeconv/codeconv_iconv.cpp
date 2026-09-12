#include <stdio.h>
#include <iconv.h>
#include <errno.h>

#include "codeconv_iconv.h"
#include <string.h>

namespace {

void Convert(iconv_t converter, const char *src, char *dest, int bufSize)
{
    if (dest == NULL || bufSize <= 0) return;
    dest[0] = '\0';
    if (src == NULL || converter == (iconv_t)-1) return;

    iconv(converter, NULL, NULL, NULL, NULL);
    char *input = const_cast<char *>(src);
    size_t inputLeft = strlen(src);
    char *output = dest;
    size_t outputLeft = static_cast<size_t>(bufSize - 1);

    while (inputLeft > 0 && outputLeft > 0) {
        if (iconv(converter, &input, &inputLeft, &output, &outputLeft) != (size_t)-1) {
            break;
        }
        if (errno == E2BIG) break;
        if (errno == EILSEQ || errno == EINVAL) {
            ++input;
            --inputLeft;
            *output++ = '?';
            --outputLeft;
            iconv(converter, NULL, NULL, NULL, NULL);
            continue;
        }
        break;
    }
    *output = '\0';
}

} // namespace

CodeConvertIconv::CodeConvertIconv() {
  ToUtf8 = iconv_open("UTF-8", "CP932");
  ToSjis = iconv_open("CP932", "UTF-8");
}

CodeConvertIconv::~CodeConvertIconv() {
    if (ToSjis != (iconv_t)-1) iconv_close(ToSjis);
    if (ToUtf8 != (iconv_t)-1) iconv_close(ToUtf8);
}

void CodeConvertIconv::Utf8ToSjis(const char *src, char *dest, int bufSize) {
    Convert(ToSjis, src, dest, bufSize);
}

void CodeConvertIconv::FromSjis(const char *src, char *dest, int bufSize) {
    Convert(ToUtf8, src, dest, bufSize);
}
