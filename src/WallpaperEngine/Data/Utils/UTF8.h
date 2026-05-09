#pragma once
#include <cstdint>

namespace WallpaperEngine::Utils {
/**
 * @brief Decodes the next UTF-8 code point from the byte stream at @p p,
 *        advancing the pointer past the consumed bytes.
 *
 * @param[in,out] p         Pointer into a NUL-terminated UTF-8 byte stream.
 *                          Advanced past the current code point on success.
 * @param[out]    codePoint Receives the decoded Unicode scalar value.
 *                          Set to U+003F ('?') for invalid lead bytes.
 *
 * @return @c true  if a code point was decoded (p was advanced).
 *         @c false if the NUL terminator was reached (p unchanged).
 */
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