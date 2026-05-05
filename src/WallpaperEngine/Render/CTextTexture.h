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
 * A TextureProvider that owns the single GL texture produced by rasterising
 * a text string with FreeType.  CText creates one of these during setup(),
 * assigns it to CRenderable::m_texture, and calls rebuild() whenever the
 * displayed string changes.
 *
 * The texture is stored as a single-channel (GL_RED) image so the quad
 * shader can use the red value as a coverage mask.
 */
class CTextTexture final : public TextureProvider {
public:
    CTextTexture ();
    ~CTextTexture () override;

    // Non-copyable, non-movable – GL resources have fixed identity
    CTextTexture (const CTextTexture&)            = delete;
    CTextTexture& operator= (const CTextTexture&) = delete;

    /**
     * Rasterise @p text using @p face and (re-)upload to the GPU.
     * Safe to call every frame; only issues a glTexImage2D when the
     * pixel buffer has actually changed.
     */
    void rebuild (FT_Face face, const std::string& text);

    // ── TextureProvider interface ────────────────────────────────────────
    [[nodiscard]] GLuint    getTextureID          (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t  getTextureWidth       (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t  getTextureHeight      (uint32_t imageIndex) const override;
    [[nodiscard]] uint32_t  getRealWidth          () const override;
    [[nodiscard]] uint32_t  getRealHeight         () const override;
    [[nodiscard]] TextureFormat getFormat         () const override;
    [[nodiscard]] uint32_t  getFlags              () const override;
    [[nodiscard]] const glm::vec4* getResolution  () const override;
    [[nodiscard]] const std::vector<FrameSharedPtr>& getFrames () const override;
    [[nodiscard]] bool      isAnimated            () const override;
    [[nodiscard]] uint32_t  getSpritesheetCols    () const override;
    [[nodiscard]] uint32_t  getSpritesheetRows    () const override;
    [[nodiscard]] uint32_t  getSpritesheetFrames  () const override;
    [[nodiscard]] float     getSpritesheetDuration() const override;
    void incrementUsageCount () const override {}
    void decrementUsageCount () const override {}
    void update              () const override {}

private:
    GLuint    m_glTexture  { 0 };
    uint32_t  m_width      { 1 };
    uint32_t  m_height     { 1 };
    glm::vec4 m_resolution {};

    /** Text textures are never animated; this stays empty. */
    std::vector<FrameSharedPtr> m_frames;
};

} // namespace WallpaperEngine::Render