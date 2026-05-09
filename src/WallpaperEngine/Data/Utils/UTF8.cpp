#include "UTF8.h"

namespace WallpaperEngine::Utils {
bool nextCodePoint (const unsigned char*& p, uint32_t& codePoint) {
    if (*p == '\0') {
	return false;
    }

    if (*p < 0x80) {
	codePoint = *p++;
    } else if ((*p & 0xE0) == 0xC0) {
	codePoint = (*p++ & 0x1F) << 6;
	codePoint |= (*p++ & 0x3F);
    } else if ((*p & 0xF0) == 0xE0) {
	codePoint = (*p++ & 0x0F) << 12;
	codePoint |= (*p++ & 0x3F) << 6;
	codePoint |= (*p++ & 0x3F);
    } else if ((*p & 0xF8) == 0xF0) {
	codePoint = (*p++ & 0x07) << 18;
	codePoint |= (*p++ & 0x3F) << 12;
	codePoint |= (*p++ & 0x3F) << 6;
	codePoint |= (*p++ & 0x3F);
    } else {
	++p;
	codePoint = '?';
    }

    return true;
}
} // namespace WallpaperEngine::Utils