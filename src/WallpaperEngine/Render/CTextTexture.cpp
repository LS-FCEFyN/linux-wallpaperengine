#include "CTextTexture.h"

#include <algorithm>
#include <vector>

using namespace WallpaperEngine::Render;

// ─────────────────────────────────────────────────────────────────────────────
CTextTexture::CTextTexture () = default;

CTextTexture::~CTextTexture () {
    if (m_glTexture) {
	glDeleteTextures (1, &m_glTexture);
	m_glTexture = 0;
    }
}

static bool nextCodePoint (const unsigned char*& p, uint32_t& codePoint) {
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

// ─────────────────────────────────────────────────────────────────────────────
// rebuild() — rasterise every glyph in @text and upload a fresh GL_RED texture.
// ─────────────────────────────────────────────────────────────────────────────
void CTextTexture::rebuild (FT_Face face, const std::string& text) {
    if (!face) {
	return;
    }

    FT_GlyphSlot slot = face->glyph;

    // ── Pass 1: measure total advance and vertical extents ───────────────────
    int penX = 0;
    int maxAscent = 0;
    int maxDescent = 0;

    {
	const auto* p = reinterpret_cast<const unsigned char*> (text.c_str ());

	uint32_t codePoint = 0;

	while (nextCodePoint (p, codePoint)) {
	    if (FT_Load_Char (face, static_cast<FT_ULong> (codePoint), FT_LOAD_RENDER) != 0) {
		continue;
	    }

	    penX += slot->advance.x >> 6;

	    maxAscent = std::max (maxAscent, slot->bitmap_top);

	    maxDescent = std::max (maxDescent, static_cast<int> (slot->bitmap.rows) - slot->bitmap_top);
	}
    }

    const int width = std::max (1, penX);
    const int height = std::max (1, maxAscent + maxDescent);

    std::vector<uint8_t> pixels (static_cast<size_t> (width) * height, 0);

    // ── Pass 2: rasterise each glyph into the pixel buffer ──────────────────
    penX = 0;

    {
	const auto* p = reinterpret_cast<const unsigned char*> (text.c_str ());

	uint32_t codePoint = 0;

	while (nextCodePoint (p, codePoint)) {
	    if (FT_Load_Char (face, static_cast<FT_ULong> (codePoint), FT_LOAD_RENDER) != 0) {
		continue;
	    }

	    const auto& bmp = slot->bitmap;
	    const int originX = penX + slot->bitmap_left;
	    const int originY = maxAscent - slot->bitmap_top;

	    for (unsigned int row = 0; row < bmp.rows; ++row) {
		for (unsigned int col = 0; col < bmp.width; ++col) {
		    const int dstX = originX + static_cast<int> (col);
		    const int dstY = originY + static_cast<int> (row);

		    if (dstX < 0 || dstX >= width || dstY < 0 || dstY >= height) {
			continue;
		    }

		    pixels[static_cast<size_t> (dstY) * width + dstX] = bmp.buffer[row * bmp.pitch + col];
		}
	    }

	    penX += slot->advance.x >> 6;
	}
    }

    // ── Upload to GPU ────────────────────────────────────────────────────────
    const bool firstUpload = (m_glTexture == 0);

    if (firstUpload) {
	glGenTextures (1, &m_glTexture);
    }

    glBindTexture (GL_TEXTURE_2D, m_glTexture);

    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);

    glTexImage2D (GL_TEXTURE_2D, 0, GL_RED, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data ());

    if (firstUpload) {
	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);

	glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    glBindTexture (GL_TEXTURE_2D, 0);

    // ── Keep cached dimensions in sync ──────────────────────────────────────
    m_width = static_cast<uint32_t> (width);
    m_height = static_cast<uint32_t> (height);

    m_resolution = { static_cast<float> (width), static_cast<float> (height), static_cast<float> (width),
		     static_cast<float> (height) };
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureProvider interface
// ─────────────────────────────────────────────────────────────────────────────
GLuint CTextTexture::getTextureID (uint32_t /*imageIndex*/) const { return m_glTexture; }

uint32_t CTextTexture::getTextureWidth (uint32_t /*imageIndex*/) const { return m_width; }

uint32_t CTextTexture::getTextureHeight (uint32_t /*imageIndex*/) const { return m_height; }

uint32_t CTextTexture::getRealWidth () const { return m_width; }
uint32_t CTextTexture::getRealHeight () const { return m_height; }

TextureFormat CTextTexture::getFormat () const {
    // Single-channel red; closest named format in the enum.
    return TextureFormat_R8;
}

uint32_t CTextTexture::getFlags () const {
    // Clamp UVs — text quads should never tile.
    return TextureFlags_ClampUVs;
}

const glm::vec4* CTextTexture::getResolution () const { return &m_resolution; }

const std::vector<FrameSharedPtr>& CTextTexture::getFrames () const {
    return m_frames; // always empty
}

bool CTextTexture::isAnimated () const { return false; }
uint32_t CTextTexture::getSpritesheetCols () const { return 1; }
uint32_t CTextTexture::getSpritesheetRows () const { return 1; }
uint32_t CTextTexture::getSpritesheetFrames () const { return 1; }
float CTextTexture::getSpritesheetDuration () const { return 0.0f; }