// Released under the MIT License. See LICENSE for details.
// Derived from code licensed as follows:

/*
  Basic UTF-8 manipulation routines
  by Jeff Bezanson
  placed in the public domain Fall 2005

  This code is designed to provide the utilities you need to manipulate
  UTF-8 as an internal string encoding. These functions do not perform the
  error checking normally needed when handling UTF-8 data, so if you happen
  to be from the Unicode Consortium you will want to flay me alive.
  I do this because error checking can be performed at the boundaries (I/O),
  with these routines reserved for higher performance on data known to be
  valid.
*/
#include "ballistica/shared/generic/utf8.h"

#if _WIN32 || _WIN64
#include <malloc.h>
#endif

#if BA_PLATFORM_LINUX
#include <cstring>
#endif

namespace ballistica {

// Should tidy this up but don't want to risk breaking anything for now.
#pragma clang diagnostic push
#pragma ide diagnostic ignored "hicpp-signed-bitwise"
#pragma ide diagnostic ignored "bugprone-narrowing-conversions"

static const uint32_t offsetsFromUTF8[6] = {0x00000000UL, 0x00003080UL,
                                            0x000E2080UL, 0x03C82080UL,
                                            0xFA082080UL, 0x82082080UL};

static const char trailingBytesForUTF8[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    3, 3, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5};

/* returns length of next utf-8 sequence */
auto u8_seqlen(const char* s) -> int {
  return trailingBytesForUTF8[(unsigned int)(unsigned char)s[0]] + 1;
}

/* conversions without error checking
   only works for valid UTF-8, i.e. no 5- or 6-byte sequences
   srcsz = source size in bytes, or -1 if 0-terminated
   sz = dest size in # of wide characters

   returns # characters converted
   dest will always be L'\0'-terminated, even if there isn't enough room
   for all the characters.
   if sz = srcsz+1 (i.e. 4*srcsz+4 bytes), there will always be enough space.
*/
auto u8_toucs(uint32_t* dest, int sz, const char* src, int srcsz) -> int {
  uint32_t ch;
  const char* src_end = src + srcsz;
  int nb;
  int i = 0;

  while (i < sz - 1) {
    nb = trailingBytesForUTF8[(unsigned char)*src];  // NOLINT(cert-str34-c)
    if (srcsz == -1) {
      if (*src == 0) goto done_toucs;
    } else {
      if (src + nb >= src_end) goto done_toucs;
    }
    ch = 0;
    switch (nb) {
        /* these fall through deliberately */
      case 3:  // NOLINT(bugprone-branch-clone)
        ch += (unsigned char)*src++;
        ch <<= 6;
      case 2:
        ch += (unsigned char)*src++;
        ch <<= 6;
      case 1:
        ch += (unsigned char)*src++;
        ch <<= 6;
      case 0:
        ch += (unsigned char)*src++;
      default:
        break;
    }
    ch -= offsetsFromUTF8[nb];
    dest[i++] = ch;
  }
done_toucs:
  dest[i] = 0;
  return i;
}

/* srcsz = number of source characters, or -1 if 0-terminated
   sz = size of dest buffer in bytes

   returns # characters converted
   dest will only be '\0'-terminated if there is enough space. this is
   for consistency; imagine there are 2 bytes of space left, but the next
   character requires 3 bytes. in this case we could NUL-terminate, but in
   general we can't when there's insufficient space. therefore this function
   only NUL-terminates if all the characters fit, and there's space for
   the NUL as well.
   the destination string will never be bigger than the source string.
*/
auto u8_toutf8(char* dest, int sz, const uint32_t* src, int srcsz) -> int {
  uint32_t ch;
  int i = 0;
  char* dest_end = dest + sz;

  while (srcsz < 0 ? src[i] != 0 : i < srcsz) {
    ch = src[i];
    if (ch < 0x80) {
      if (dest >= dest_end) return i;
      *dest++ = (char)ch;
    } else if (ch < 0x800) {
      if (dest >= dest_end - 1) return i;
      *dest++ = static_cast<char>((ch >> 6) | 0xC0);
      *dest++ = static_cast<char>((ch & 0x3F) | 0x80);
    } else if (ch < 0x10000) {
      if (dest >= dest_end - 2) return i;
      *dest++ = static_cast<char>((ch >> 12) | 0xE0);
      *dest++ = static_cast<char>(((ch >> 6) & 0x3F) | 0x80);
      *dest++ = static_cast<char>((ch & 0x3F) | 0x80);
    } else if (ch < 0x110000) {
      if (dest >= dest_end - 3) return i;
      *dest++ = static_cast<char>((ch >> 18) | 0xF0);
      *dest++ = static_cast<char>(((ch >> 12) & 0x3F) | 0x80);
      *dest++ = static_cast<char>(((ch >> 6) & 0x3F) | 0x80);
      *dest++ = static_cast<char>((ch & 0x3F) | 0x80);
    }
    i++;
  }
  if (dest < dest_end) *dest = '\0';
  return i;
}

auto u8_wc_toutf8(char* dest, uint32_t ch) -> int {
  if (ch < 0x80) {
    dest[0] = (char)ch;
    return 1;
  }
  if (ch < 0x800) {
    dest[0] = static_cast<char>((ch >> 6) | 0xC0);
    dest[1] = static_cast<char>((ch & 0x3F) | 0x80);
    return 2;
  }
  if (ch < 0x10000) {
    dest[0] = static_cast<char>((ch >> 12) | 0xE0);
    dest[1] = static_cast<char>(((ch >> 6) & 0x3F) | 0x80);
    dest[2] = static_cast<char>((ch & 0x3F) | 0x80);
    return 3;
  }
  if (ch < 0x110000) {
    dest[0] = static_cast<char>((ch >> 18) | 0xF0);
    dest[1] = static_cast<char>(((ch >> 12) & 0x3F) | 0x80);
    dest[2] = static_cast<char>(((ch >> 6) & 0x3F) | 0x80);
    dest[3] = static_cast<char>((ch & 0x3F) | 0x80);
    return 4;
  }
  return 0;
}

/* charnum => byte offset */
auto u8_offset(const char* str, int charnum) -> int {
  int offs = 0;

  while (charnum > 0 && str[offs]) {
    (void)(isutf(str[++offs]) || isutf(str[++offs]) || isutf(str[++offs])
           || ++offs);
    charnum--;
  }
  return offs;
}

/* byte offset => charnum */
auto u8_charnum(const char* s, int offset) -> int {
  int charnum = 0, offs = 0;

  while (offs < offset && s[offs]) {
    (void)(isutf(s[++offs]) || isutf(s[++offs]) || isutf(s[++offs]) || ++offs);
    charnum++;
  }
  return charnum;
}

/* number of characters */
auto u8_strlen(const char* s) -> int {
  int count = 0;
  int i = 0;
  while (u8_nextchar(s, &i) != 0) {
    count++;
  }
  return count;
}

auto u8_nextchar(const char* s, int* i) -> uint32_t {
  uint32_t ch = 0;
  size_t sz = 0;

  do {
    ch <<= 6;
    ch += (unsigned char)s[(*i)];
    sz++;
  } while (s[*i] && (++(*i)) && !isutf(s[*i]));
  ch -= offsetsFromUTF8[sz - 1];

  return ch;
}

void u8_inc(const char* s, int* i) {
  (void)(isutf(s[++(*i)]) || isutf(s[++(*i)]) || isutf(s[++(*i)]) || ++(*i));
}

void u8_dec(const char* s, int* i) {
  (void)(isutf(s[--(*i)]) || isutf(s[--(*i)]) || isutf(s[--(*i)]) || --(*i));
}

auto octal_digit(char c) -> int { return (c >= '0' && c <= '7'); }

auto hex_digit(char c) -> int {
  return ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F')
          || (c >= 'a' && c <= 'f'));
}

/* assumes that src points to the character after a backslash
   returns number of input characters processed */
auto u8_read_escape_sequence(char* str, uint32_t* dest) -> int {
  uint32_t ch;
  char digs[9] = "\0\0\0\0\0\0\0\0";
  int dno = 0, i = 1;

  ch = (uint32_t)str[0]; /* take literal character */  // NOLINT(cert-str34-c)
  if (str[0] == 'n')
    ch = L'\n';
  else if (str[0] == 't')
    ch = L'\t';
  else if (str[0] == 'r')
    ch = L'\r';
  else if (str[0] == 'b')
    ch = L'\b';
  else if (str[0] == 'f')
    ch = L'\f';
  else if (str[0] == 'v')
    ch = L'\v';
  else if (str[0] == 'a')
    ch = L'\a';
  else if (octal_digit(str[0])) {
    i = 0;
    do {
      digs[dno++] = str[i++];
    } while (octal_digit(str[i]) && dno < 3);
    ch = static_cast<uint32_t>(strtol(digs, nullptr, 8));
  } else if (str[0] == 'x') {
    while (hex_digit(str[i]) && dno < 2) {
      digs[dno++] = str[i++];
    }
    if (dno > 0) ch = static_cast<uint32_t>(strtol(digs, nullptr, 16));
  } else if (str[0] == 'u') {
    while (hex_digit(str[i]) && dno < 4) {
      digs[dno++] = str[i++];
    }
    if (dno > 0) ch = static_cast<uint32_t>(strtol(digs, nullptr, 16));
  } else if (str[0] == 'U') {
    while (hex_digit(str[i]) && dno < 8) {
      digs[dno++] = str[i++];
    }
    if (dno > 0) ch = static_cast<uint32_t>(strtol(digs, nullptr, 16));
  }
  *dest = ch;

  return i;
}

/* convert a string with literal \uxxxx or \Uxxxxxxxx characters to UTF-8
   example: u8_unescape(mybuf, 256, "hello\\u220e")
   note the double backslash is needed if called on a C string literal */
auto u8_unescape(char* buf, int sz, char* src) -> int {
  int c = 0, amt;
  uint32_t ch;
  char temp[4];

  while (*src && c < sz) {
    if (*src == '\\') {
      src++;
      amt = u8_read_escape_sequence(src, &ch);
    } else {
      ch = (uint32_t)*src;  // NOLINT(cert-str34-c)
      amt = 1;
    }
    src += amt;
    amt = u8_wc_toutf8(temp, ch);
    if (amt > sz - c) break;
    memcpy(&buf[c], temp, static_cast<size_t>(amt));
    c += amt;
  }
  if (c < sz) buf[c] = '\0';
  return c;
}

auto u8_escape_wchar(char* buf, int sz, uint32_t ch) -> int {
  if (ch == L'\n')
    return snprintf(buf, static_cast<size_t>(sz), "\\n");
  else if (ch == L'\t')
    return snprintf(buf, static_cast<size_t>(sz), "\\t");
  else if (ch == L'\r')
    return snprintf(buf, static_cast<size_t>(sz), "\\r");
  else if (ch == L'\b')
    return snprintf(buf, static_cast<size_t>(sz), "\\b");
  else if (ch == L'\f')
    return snprintf(buf, static_cast<size_t>(sz), "\\f");
  else if (ch == L'\v')
    return snprintf(buf, static_cast<size_t>(sz), "\\v");
  else if (ch == L'\a')
    return snprintf(buf, static_cast<size_t>(sz), "\\a");
  else if (ch == L'\\')
    return snprintf(buf, static_cast<size_t>(sz), "\\\\");
  else if (ch < 32 || ch == 0x7f)
    return snprintf(buf, static_cast<size_t>(sz), "\\x%hhX", (unsigned char)ch);
  else if (ch > 0xFFFF)
    return snprintf(buf, static_cast<size_t>(sz), "\\U%.8X", (uint32_t)ch);
  else if (ch >= 0x80 && ch <= 0xFFFF)
    return snprintf(buf, static_cast<size_t>(sz), "\\u%.4hX",
                    (unsigned short)ch);

  return snprintf(buf, static_cast<size_t>(sz), "%c", (char)ch);
}

auto u8_escape(char* buf, int sz, char* src, int escape_quotes) -> int {
  int c = 0, i = 0, amt;

  while (src[i] && c < sz) {
    if (escape_quotes && src[i] == '"') {
      amt = snprintf(buf, static_cast<size_t>(sz - c), "\\\"");
      i++;
    } else {
      amt = u8_escape_wchar(buf, sz - c, u8_nextchar(src, &i));
    }
    c += amt;
    buf += amt;
  }
  if (c < sz) *buf = '\0';
  return c;
}

auto u8_strchr(char* s, uint32_t ch, int* charn) -> char* {
  int i = 0, lasti = 0;
  uint32_t c;

  *charn = 0;
  while (s[i]) {
    c = u8_nextchar(s, &i);
    if (c == ch) {
      return &s[lasti];
    }
    lasti = i;
    (*charn)++;
  }
  return nullptr;
}

auto u8_memchr(char* s, uint32_t ch, size_t sz, int* charn) -> char* {
  size_t i = 0, lasti = 0;
  uint32_t c;
  int csz;

  *charn = 0;
  while (i < sz) {
    c = static_cast<uint32_t>(csz = 0);
    do {
      c <<= 6;
      c += (unsigned char)s[i++];
      csz++;
    } while (i < sz && !isutf(s[i]));
    c -= offsetsFromUTF8[csz - 1];

    if (c == ch) {
      return &s[lasti];
    }
    lasti = i;
    (*charn)++;
  }
  return nullptr;
}

auto u8_is_locale_utf8(const char* locale) -> int {
  /* this code based on libutf8 */
  const char* cp = locale;

  for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++) {
    if (*cp == '.') {
      const char* encoding = ++cp;
      for (; *cp != '\0' && *cp != '@' && *cp != '+' && *cp != ','; cp++);
      if ((cp - encoding == 5 && !strncmp(encoding, "UTF-8", 5))
          || (cp - encoding == 4 && !strncmp(encoding, "utf8", 4)))
        return 1; /* it's UTF-8 */
      break;
    }
  }
  return 0;
}

auto u8_vprintf(char* fmt, va_list ap) -> int {
  char* buf;
  uint32_t* wcs;

  int sz{512};
  buf = (char*)alloca(sz);
try_print:
  int cnt = vsnprintf(buf, static_cast<size_t>(sz), fmt, ap);
  if (cnt >= sz) {
    buf = (char*)alloca(cnt - sz + 1);
    sz = cnt + 1;
    goto try_print;
  }
  wcs = (uint32_t*)alloca((cnt + 1) * sizeof(uint32_t));
  cnt = u8_toucs(wcs, cnt + 1, buf, cnt);
  printf("%ls", (wchar_t*)wcs);
  return cnt;
}

auto u8_printf(char* fmt, ...) -> int {
  int cnt;
  va_list args;

  va_start(args, fmt);

  cnt = u8_vprintf(fmt, args);

  va_end(args);
  return cnt;
}

#pragma clang diagnostic pop

}  // namespace ballistica
