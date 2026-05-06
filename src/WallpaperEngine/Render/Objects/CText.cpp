#include "CText.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <stdexcept>
#include <vector>

using namespace WallpaperEngine::Render::Objects;

// ─────────────────────────────────────────────
// Quad shader — renders text coverage texture
// ─────────────────────────────────────────────
static const char* QUAD_VERT = R"(
#version 330 core
layout (location = 0) in vec2 a_pos;
layout (location = 1) in vec2 a_uv;
out vec2 TexCoord;
uniform mat4 u_mvp;
void main () {
    gl_Position = u_mvp * vec4 (a_pos, 0.0, 1.0);
    TexCoord    = a_uv;
}
)";

static const char* QUAD_FRAG = R"(
#version 330 core
in  vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec4      uColor;
void main () {
    float coverage = texture (uTexture, TexCoord).r;
    FragColor = vec4 (uColor.rgb, uColor.a * coverage);
}
)";

// ─────────────────────────────────────────────
// Construction / destruction
// ─────────────────────────────────────────────
CText::CText (Wallpapers::CScene& scene, const Text& text) :
    CRenderable (scene, text, text.m_material), m_text (text) {}

CText::~CText () {
    shutdownFont   ();
    shutdownQuad   ();
    // m_textTexture is a shared_ptr; its destructor releases the GL texture.
}

const Text& CText::getText () const { return m_text; }

// ─────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────
void CText::setup () {
    if (m_initialized) return;

    loadFont   ();
    setupQuad  ();
    setupScript();

    // Create the TextureProvider and expose it through CRenderable::m_texture
    // so that getTexture() / the pass system / and our own render() all share
    // the same object without any extra indirection.
    m_textTexture = std::make_shared<CTextTexture> ();
    m_texture     = m_textTexture;          // CRenderable::m_texture (shared_ptr<const TextureProvider>)

    // Rasterise once so the first frame is not blank.
    m_currentText = evaluateScript ();
    m_textTexture->rebuild (m_ftFace, m_currentText);
    updateQuadGeometry ();

    m_initialized = true;
}

// ─────────────────────────────────────────────
// render()  — called every frame by CScene
// ─────────────────────────────────────────────
void CText::render () {
    if (!m_initialized) return;
    if (!m_text.visible->value->getBool ()) return;

    // Re-evaluate script; rebuild texture only when the string actually changed.
    const std::string newText = evaluateScript ();
    if (newText != m_currentText) {
        m_currentText = newText;
        m_textTexture->rebuild (m_ftFace, m_currentText);
        updateQuadGeometry ();
    }

    updateModelMatrix ();

    // CScene already bound the correct framebuffer before this call — do not
    // override it here (that was the original bug described in the old code).

    glEnable  (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram (m_quadProgram);

    const glm::mat4 mvp = getScene ().getCamera ().getProjection ()
                        * getScene ().getCamera ().getLookAt ()
                        * m_modelMatrix;
    glUniformMatrix4fv (
        glGetUniformLocation (m_quadProgram, "u_mvp"), 1, GL_FALSE, glm::value_ptr (mvp));

    // m_text.color is a vec3; read alpha from the separate UserSetting to avoid
    // the w=0 trap that comes from calling getVec4() on a vec3 UserSetting.
    const auto  col   = m_text.color->value->getVec3 ();
    const float alpha = m_text.alpha->value->getFloat ();
    glUniform4f (
        glGetUniformLocation (m_quadProgram, "uColor"), col.r, col.g, col.b, alpha);

    glActiveTexture (GL_TEXTURE0);
    // Fetch the GL handle through the TextureProvider interface – no raw ID stored here.
    glBindTexture (GL_TEXTURE_2D, m_textTexture->getTextureID (0));

    glBindVertexArray (m_quadVao);
    glDrawArrays (GL_TRIANGLES, 0, 6);
    glBindVertexArray (0);

    glUseProgram  (0);
    glDisable (GL_BLEND);
}

// ─────────────────────────────────────────────
// QuickJS
// ─────────────────────────────────────────────
void CText::setupScript () {
    if (m_text.script.empty ()) {
        m_currentText = m_text.value;
        return;
    }

    m_scriptReady = true;
}

std::string CText::evaluateScript () {
    if (!m_scriptReady)
        return m_currentText.empty () ? m_text.value : m_currentText;

    DynamicValue curValue;
    curValue.update(m_currentText);
    std::map<std::string, DynamicValue*> emptyProps;
    auto res = WallpaperEngine::Scripting::ScriptEngine::instance ().evaluate (
        m_text.script, emptyProps, curValue);

    if (res->getType () == DynamicValue::String) {
        return res->getString ();
    }

    return m_currentText;
}

// ─────────────────────────────────────────────
// FreeType
// ─────────────────────────────────────────────
void CText::loadFont () {
    if (FT_Init_FreeType (&m_ftLibrary))
        throw std::runtime_error ("CText: failed to init FreeType");

    const std::string& fontPath = m_text.font;
    auto fontStream = getAssetLocator ().read (fontPath);
    if (!fontStream)
        throw std::runtime_error ("CText: failed to open font file: " + fontPath);

    fontStream->seekg (0, std::ios::end);
    const size_t fontDataSize = fontStream->tellg ();
    fontStream->seekg (0, std::ios::beg);

    m_fontDataBuffer.resize (fontDataSize);
    fontStream->read (reinterpret_cast<char*> (m_fontDataBuffer.data ()), fontDataSize); // Conversion from unsigned long to long is also implementation dependent
    if (!fontStream->good ())
        throw std::runtime_error ("CText: failed to read font data: " + fontPath);

    if (FT_New_Memory_Face (m_ftLibrary,
                            m_fontDataBuffer.data (),
                            static_cast<FT_Long> (m_fontDataBuffer.size ()),
                            0, &m_ftFace))
        throw std::runtime_error ("CText: failed to load font face: " + fontPath);

    FT_Set_Pixel_Sizes (m_ftFace, 0, static_cast<FT_UInt> (m_text.pointsize * 2));

    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
}

void CText::shutdownFont () {
    if (m_ftFace)    { FT_Done_Face     (m_ftFace);    m_ftFace    = nullptr; }
    if (m_ftLibrary) { FT_Done_FreeType (m_ftLibrary); m_ftLibrary = nullptr; }
    m_fontDataBuffer.clear ();
}

// ─────────────────────────────────────────────
// OpenGL quad (geometry only – no texture here)
// ─────────────────────────────────────────────
void CText::setupQuad () {
    GLuint qv = compileShader (GL_VERTEX_SHADER,   QUAD_VERT);
    GLuint qf = compileShader (GL_FRAGMENT_SHADER, QUAD_FRAG);
    m_quadProgram = linkProgram (qv, qf);
    glDeleteShader (qv);
    glDeleteShader (qf);

    glUseProgram (m_quadProgram);
    glUniform1i (glGetUniformLocation (m_quadProgram, "uTexture"), 0);
    glUseProgram (0);

    glGenVertexArrays (1, &m_quadVao);
    glGenBuffers      (1, &m_quadVbo);
    glBindVertexArray (m_quadVao);
    glBindBuffer      (GL_ARRAY_BUFFER, m_quadVbo);
    // Reserve space; actual vertices written by updateQuadGeometry().
    glBufferData (GL_ARRAY_BUFFER, sizeof (float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                           reinterpret_cast<void*> (0));
    glEnableVertexAttribArray (1);
    glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                           reinterpret_cast<void*> (2 * sizeof (float)));
    glBindVertexArray (0);
}

void CText::shutdownQuad () {
    if (m_quadVao)     { glDeleteVertexArrays (1, &m_quadVao);    m_quadVao     = 0; }
    if (m_quadVbo)     { glDeleteBuffers      (1, &m_quadVbo);    m_quadVbo     = 0; }
    if (m_quadProgram) { glDeleteProgram (m_quadProgram);         m_quadProgram = 0; }
}

void CText::updateQuadGeometry () {
    if (!m_textTexture) return;

    // Build a centred quad whose pixel dimensions match the rasterised bitmap.
    // V-coordinates are flipped so FreeType's top-down bitmap maps correctly
    // onto OpenGL's bottom-up UV space.
    const float hw = static_cast<float> (m_textTexture->getTextureWidth  (0)) * 0.5f;
    const float hh = static_cast<float> (m_textTexture->getTextureHeight (0)) * 0.5f;

    const float verts[] = {
        -hw, -hh,  0.0f, 0.0f,   // bottom-left
         hw, -hh,  1.0f, 0.0f,   // bottom-right
         hw,  hh,  1.0f, 1.0f,   // top-right
        -hw, -hh,  0.0f, 0.0f,
         hw,  hh,  1.0f, 1.0f,
        -hw,  hh,  0.0f, 1.0f,   // top-left
    };

    glBindVertexArray (m_quadVao);
    glBindBuffer      (GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData      (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_DYNAMIC_DRAW);
    glBindVertexArray (0);
}

// ─────────────────────────────────────────────
// Model matrix
// ─────────────────────────────────────────────
void CText::updateModelMatrix () {
    const float screenW = static_cast<float> (getScene ().getCamera ().getWidth  ());
    const float screenH = static_cast<float> (getScene ().getCamera ().getHeight ());

    const glm::vec3 origin = getObject ().origin->value->getVec3 ();
    const float cx =  (origin.x - screenW / 2.0f);
    const float cy = -(origin.y - screenH / 2.0f);   // flip Y for OpenGL

    const glm::vec3 scaleVec  = m_text.scale->value->getVec3  ();
    const glm::vec4 anglesVec = m_text.angles->value->getVec4 ();

    m_modelMatrix = glm::mat4 (1.0f);
    m_modelMatrix = glm::translate (m_modelMatrix, glm::vec3 (cx, cy, origin.z));
    m_modelMatrix = glm::rotate (m_modelMatrix, anglesVec.y, glm::vec3 (0.0f, 1.0f, 0.0f)); // yaw
    m_modelMatrix = glm::rotate (m_modelMatrix, anglesVec.x, glm::vec3 (1.0f, 0.0f, 0.0f)); // pitch
    m_modelMatrix = glm::rotate (m_modelMatrix, anglesVec.z, glm::vec3 (0.0f, 0.0f, 1.0f)); // roll
    m_modelMatrix = glm::scale  (m_modelMatrix, scaleVec);
}

// ─────────────────────────────────────────────
// Shader helpers
// ─────────────────────────────────────────────
GLuint CText::compileShader (GLenum type, const char* source) {
    GLuint shader = glCreateShader (type);
    glShaderSource  (shader, 1, &source, nullptr);
    glCompileShader (shader);
    GLint ok = 0;
    glGetShaderiv   (shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog (shader, 512, nullptr, log);
        throw std::runtime_error (std::string ("CText shader compile error: ") + log);
    }
    return shader;
}

GLuint CText::linkProgram (GLuint vert, GLuint frag) {
    GLuint prog = glCreateProgram ();
    glAttachShader (prog, vert);
    glAttachShader (prog, frag);
    glLinkProgram  (prog);
    GLint ok = 0;
    glGetProgramiv (prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog (prog, 512, nullptr, log);
        throw std::runtime_error (std::string ("CText program link error: ") + log);
    }
    return prog;
}
const float& CText::getBrightness () const {
    static const float brightness = 1.0f;
    return brightness;
}

const float& CText::getUserAlpha () const {
    return this->m_text.alpha->value->getFloat ();
}

const float& CText::getAlpha () const {
    return this->m_text.alpha->value->getFloat ();
}

const glm::vec3& CText::getColor () const {
    return this->m_text.color->value->getVec3 ();
}

const glm::vec4& CText::getColor4 () const {
    return this->m_text.color->value->getVec4 ();
}

const glm::vec3& CText::getCompositeColor () const {
    return this->m_text.color->value->getVec3 ();
}
