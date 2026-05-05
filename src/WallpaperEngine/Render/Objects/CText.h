#pragma once

#include "CRenderable.h"
#include "WallpaperEngine/Render/CTextTexture.h"
#include "WallpaperEngine/Render/CObject.h"
#include "WallpaperEngine/Render/Wallpapers/CScene.h"
#include "WallpaperEngine/Data/Model/Text.h"

#include <GL/glew.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <memory>
#include <string>
#include <vector>

using namespace WallpaperEngine;
using namespace WallpaperEngine::Data::Model;

namespace WallpaperEngine::Render::Objects {

class CText final : public CRenderable {
    friend CObject;
public:
    CText (Wallpapers::CScene& scene, const Text& text);
    ~CText () override;

    void setup () override;
    void render () override;

    [[nodiscard]] const float& getBrightness () const override;
    [[nodiscard]] const float& getUserAlpha () const override;
    [[nodiscard]] const float& getAlpha () const override;
    [[nodiscard]] const glm::vec3& getColor () const override;
    [[nodiscard]] const glm::vec4& getColor4 () const override;
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

    [[nodiscard]] const Text& getText () const;

private:
    // ── QuickJS ──────────────────────────────────────────────────────────────
    void        setupScript    ();
    std::string evaluateScript ();

    bool       m_scriptReady  { false };

    // ── FreeType ─────────────────────────────────────────────────────────────
    void loadFont    ();
    void shutdownFont();

    FT_Library           m_ftLibrary    { nullptr };
    FT_Face              m_ftFace       { nullptr };
    std::vector<uint8_t> m_fontDataBuffer;  // keeps font bytes alive for FreeType

    // ── OpenGL quad ──────────────────────────────────────────────────────────
    // The text texture itself lives in m_textTexture (a TextureProvider) which
    // is also stored in CRenderable::m_texture so the rest of the pipeline can
    // inspect it.  The quad VAO/VBO and compiled shader stay here because text
    // needs a bespoke coverage-mask shader that is not part of the standard
    // material pass system.
    void setupQuad    ();
    void shutdownQuad ();
    void updateQuadGeometry ();   // called after every rebuild to resize the quad

    GLuint m_quadVao     { 0 };
    GLuint m_quadVbo     { 0 };
    GLuint m_quadProgram { 0 };

    // ── Text-as-TextureProvider ───────────────────────────────────────────────
    // Owns the GPU texture.  CRenderable::m_texture points at this same object
    // so that getTexture() / detectTexture() return the correct provider.
    std::shared_ptr<CTextTexture> m_textTexture;

    // ── State ─────────────────────────────────────────────────────────────────
    const Text& m_text;
    std::string m_currentText;
    bool        m_initialized { false };

    glm::mat4 m_modelMatrix { 1.0f };
    void updateModelMatrix ();

    // ── Shader helpers ────────────────────────────────────────────────────────
    static GLuint compileShader (GLenum type, const char* source);
    static GLuint linkProgram   (GLuint vert, GLuint frag);
};

} // namespace WallpaperEngine::Render::Objects