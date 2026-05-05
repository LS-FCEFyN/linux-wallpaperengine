/**
 * @file CTextTexture.h
 * @brief GPU-side texture produced by rasterising a text string with FreeType.
 *
 * @details
 * `CTextTexture` implements the `TextureProvider` interface and owns the
 * single OpenGL texture that backs a text object (e.g. a wallpaper clock
 * driven by a JS @c update() script).  The lifetime contract is:
 *
 *  1. `CText::setup()` creates one `CTextTexture` instance, stores it in
 *     `CRenderable::m_texture`, and immediately calls `rebuild()` so the
 *     first rendered frame is not blank.
 *  2. Every frame, `CText::render()` calls `rebuild()` again if the
 *     evaluated string has changed (e.g. the clock minute ticked over).
 *  3. The destructor releases the GL texture handle; no explicit cleanup
 *     call is needed from the owning `CText`.
 *
 * The rasterised bitmap is stored as a single-channel `GL_RED` texture.
 * The quad shader in `CText` treats the red channel as a per-pixel coverage
 * mask and multiplies it with the configured RGBA colour uniform (`uColor`)
 * to produce the final blended output.
 *
 * @note This class is non-copyable and non-movable because it owns a raw
 *       OpenGL object whose identity must not change after creation.
 *
 * @see CText
 * @see TextureProvider
 */
#pragma once

#include "TextureProvider.h"

#include <GL/glew.h>
#include <glm/vec4.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <vector>

namespace WallpaperEngine::Render {

/**
 * @class CTextTexture
 * @brief FreeType-backed `TextureProvider` for wallpaper text objects.
 *
 * @details
 * Rasterises an arbitrary UTF-8 / Latin-1 string into a single OpenGL
 * texture using a pre-loaded `FT_Face`.  The owner (`CText`) supplies the
 * face and calls `rebuild()` whenever the string changes.
 *
 * ### Texture layout
 * | Property | Value |
 * |---|---|
 * | Internal format | `GL_RED` (single channel, 8-bit) |
 * | Filter | `GL_LINEAR` (min + mag) |
 * | Wrap | `GL_CLAMP_TO_EDGE` on both axes |
 * | Pixel layout | top-left origin, one byte per pixel |
 *
 * ### Spritesheet / animation
 * Text textures are never animated.  All spritesheet-related accessors
 * return trivial values (1 col, 1 row, 1 frame, 0 duration).
 */
class CTextTexture final : public TextureProvider {
public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction / destruction
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Default constructor.  No GPU resources are allocated here;
     *        call `rebuild()` to produce the first texture.
     */
    CTextTexture ();

    /**
     * @brief Destructor.  Releases the owned OpenGL texture (if any) via
     *        `glDeleteTextures`.
     */
    ~CTextTexture () override;

    /// @cond – suppress from generated docs
    /** @brief Deleted copy constructor — GL resources have fixed identity. */
    CTextTexture (const CTextTexture&)            = delete;
    /** @brief Deleted copy-assignment — GL resources have fixed identity. */
    CTextTexture& operator= (const CTextTexture&) = delete;
    /// @endcond

    // ─────────────────────────────────────────────────────────────────────────
    // Core API
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Rasterise @p text and (re-)upload the result to the GPU.
     *
     * @details
     * Performs two passes over the glyph sequence:
     *  - **Pass 1** — measures total advance width and the maximum ascent /
     *    descent to determine the pixel buffer dimensions.
     *  - **Pass 2** — renders each glyph into the CPU buffer at the correct
     *    pen position, then uploads the whole buffer via `glTexImage2D`.
     *
     * On the very first call a new texture object is generated with
     * `glGenTextures` and its filtering / wrapping parameters are set once.
     * Subsequent calls reuse the same `GLuint` and only issue `glTexImage2D`
     * (the parameters are left unchanged).
     *
     * @param face A valid, pre-loaded FreeType face with a pixel size already
     *             set by `FT_Set_Pixel_Sizes`.  The caller (`CText`) owns the
     *             face and keeps it alive for the lifetime of this object.
     * @param text The string to rasterise.  Characters for which
     *             `FT_Load_Char` fails are silently skipped.
     *
     * @note Safe to call every frame; the caller is responsible for
     *       throttling calls when the string has not changed.
     */
    void rebuild (FT_Face face, const std::string& text);

    // ─────────────────────────────────────────────────────────────────────────
    // TextureProvider interface
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Returns the OpenGL texture handle.
     * @param imageIndex Ignored (text textures have exactly one image).
     * @return The `GLuint` produced by `glGenTextures`, or 0 before the first
     *         `rebuild()` call.
     */
    [[nodiscard]] GLuint getTextureID (uint32_t imageIndex) const override;

    /**
     * @brief Width of the rasterised texture in pixels.
     * @param imageIndex Ignored.
     * @return Pixel width; equals the sum of all glyph advances for the
     *         current string, clamped to at least 1.
     */
    [[nodiscard]] uint32_t getTextureWidth (uint32_t imageIndex) const override;

    /**
     * @brief Height of the rasterised texture in pixels.
     * @param imageIndex Ignored.
     * @return Pixel height; equals `maxAscent + maxDescent` across all glyphs,
     *         clamped to at least 1.
     */
    [[nodiscard]] uint32_t getTextureHeight (uint32_t imageIndex) const override;

    /**
     * @brief Physical (logical) width — identical to `getTextureWidth()` for
     *        text textures, which have no separate atlas padding.
     */
    [[nodiscard]] uint32_t getRealWidth () const override;

    /**
     * @brief Physical (logical) height — identical to `getTextureHeight()` for
     *        text textures.
     */
    [[nodiscard]] uint32_t getRealHeight () const override;

    /**
     * @brief Returns `TextureFormat_R8` — a single red channel, 8 bits per
     *        pixel, which the quad shader reads as a coverage mask.
     */
    [[nodiscard]] TextureFormat getFormat () const override;

    /**
     * @brief Returns `TextureFlags_ClampUVs` to prevent tiling artefacts on
     *        the text quad edges.
     */
    [[nodiscard]] uint32_t getFlags () const override;

    /**
     * @brief Returns a pointer to the cached resolution vector.
     * @return Pointer to a `glm::vec4` laid out as
     *         `(textureW, textureH, realW, realH)`.  For text textures the
     *         "real" and "texture" dimensions are always equal.
     *         The pointer remains valid for the lifetime of this object.
     */
    [[nodiscard]] const glm::vec4* getResolution () const override;

    /**
     * @brief Returns an empty frame list — text textures are never animated.
     */
    [[nodiscard]] const std::vector<FrameSharedPtr>& getFrames () const override;

    /**
     * @brief Always returns @c false — text textures are static.
     */
    [[nodiscard]] bool isAnimated () const override;

    /** @brief Always returns 1 — no spritesheet columns. */
    [[nodiscard]] uint32_t getSpritesheetCols () const override;

    /** @brief Always returns 1 — no spritesheet rows. */
    [[nodiscard]] uint32_t getSpritesheetRows () const override;

    /** @brief Always returns 1 — single frame. */
    [[nodiscard]] uint32_t getSpritesheetFrames () const override;

    /** @brief Always returns 0.0f — no animation duration. */
    [[nodiscard]] float getSpritesheetDuration () const override;

    /** @brief No-op — text textures are not reference-counted externally. */
    void incrementUsageCount () const override {}

    /** @brief No-op — text textures are not reference-counted externally. */
    void decrementUsageCount () const override {}

    /** @brief No-op — texture updates are driven by `rebuild()`, not the
     *         generic update pump. */
    void update () const override {}

private:
    // ─────────────────────────────────────────────────────────────────────────
    // GPU state
    // ─────────────────────────────────────────────────────────────────────────

    /** @brief OpenGL texture object handle; 0 until the first `rebuild()`. */
    GLuint m_glTexture { 0 };

    /** @brief Cached width of the last rasterised bitmap, in pixels. */
    uint32_t m_width { 1 };

    /** @brief Cached height of the last rasterised bitmap, in pixels. */
    uint32_t m_height { 1 };

    /**
     * @brief Cached resolution vector passed to the shader system.
     *
     * Layout: `(textureWidth, textureHeight, realWidth, realHeight)`.
     * For text objects all four components always equal the bitmap dimensions
     * because there is no atlas padding or "real vs logical" distinction.
     */
    glm::vec4 m_resolution {};

    /**
     * @brief Always-empty frame list returned by `getFrames()`.
     *
     * Text textures are never animated, so this vector is permanently empty.
     * It exists solely to satisfy the `TextureProvider` interface which
     * returns the container by const reference.
     */
    std::vector<FrameSharedPtr> m_frames;
};

} // namespace WallpaperEngine::Render