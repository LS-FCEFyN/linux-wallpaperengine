#include "CText.h"
#include "WallpaperEngine/Scripting/ScriptEngine.h"
#include "WallpaperEngine/Data/Utils/UTF8.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>
#include <stdexcept>
#include <vector>

using namespace WallpaperEngine::Render::Objects;
using namespace WallpaperEngine::Utils;

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
    shutdownFont ();
    shutdownQuad ();
    // m_textTexture is a shared_ptr; its destructor releases the GL texture.
}

const Text& CText::getText () const { return m_text; }

// ─────────────────────────────────────────────
// setup()
// ─────────────────────────────────────────────
void CText::setup () {
    if (m_initialized) return;

    loadFont    ();
    setupQuad   ();
    setupScript ();

    // Create the TextureProvider and expose it through CRenderable::m_texture
    // so that getTexture() / the pass system / and our own render() all share
    // the same object without any extra indirection.
    m_textTexture = std::make_shared<CTextTexture> ();
    m_texture     = m_textTexture;   // CRenderable::m_texture (shared_ptr<const TextureProvider>)

    // Rasterise once so the first frame is not blank.
    m_currentText              = evaluateScript ();
    const std::string limited  = applyTextLimits (m_ftFace, m_currentText);
    m_textTexture->rebuild (m_ftFace, limited);
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
        m_currentText             = newText;
        const std::string limited = applyTextLimits (m_ftFace, m_currentText);
        m_textTexture->rebuild (m_ftFace, limited);
        updateQuadGeometry ();
    }

    updateModelMatrix ();

    // ── Depth test ────────────────────────────────────────────────────────
    const bool depthDisabled = (m_text.depthTest == "disabled");
    if (depthDisabled)
        glDisable (GL_DEPTH_TEST);

    glEnable    (GL_BLEND);
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    const glm::mat4 mvp = getScene ().getCamera ().getProjection ()
                        * getScene ().getCamera ().getLookAt ()
                        * m_modelMatrix;

    glUseProgram (m_quadProgram);
    glUniformMatrix4fv (
        glGetUniformLocation (m_quadProgram, "u_mvp"), 1, GL_FALSE, glm::value_ptr (mvp));

    // ── Opaque background rect ────────────────────────────────────────────
    if (m_text.opaqueBackground) {
        const glm::vec3& bg = m_text.backgroundColor;
        const float      br = m_text.backgroundBrightness;
        glUniform4f (
            glGetUniformLocation (m_quadProgram, "uColor"),
            bg.r * br, bg.g * br, bg.b * br, 1.0f);

        glActiveTexture (GL_TEXTURE0);
        glBindTexture   (GL_TEXTURE_2D, m_whiteTexture);

        glBindVertexArray (m_bgVao);
        glDrawArrays      (GL_TRIANGLES, 0, 6);
        glBindVertexArray (0);
    }

    // ── Text quad ─────────────────────────────────────────────────────────
    // m_text.color is a vec3; read alpha from the separate UserSetting to avoid
    // the w=0 trap that comes from calling getVec4() on a vec3 UserSetting.
    const auto  col   = m_text.color->value->getVec3 ();
    const float alpha = m_text.alpha->value->getFloat ();
    glUniform4f (
        glGetUniformLocation (m_quadProgram, "uColor"), col.r, col.g, col.b, alpha);

    glActiveTexture (GL_TEXTURE0);
    glBindTexture   (GL_TEXTURE_2D, m_textTexture->getTextureID (0));

    glBindVertexArray (m_quadVao);
    glDrawArrays      (GL_TRIANGLES, 0, 6);
    glBindVertexArray (0);

    glUseProgram (0);
    glDisable    (GL_BLEND);

    // ── Restore depth test ────────────────────────────────────────────────
    if (depthDisabled)
        glEnable (GL_DEPTH_TEST);
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
    curValue.update (m_currentText);

    std::map<std::string, DynamicValue*> props;
    for (auto& [key, val] : m_text.scriptProperties)
        props[key] = const_cast<DynamicValue*> (&val);

    auto res = WallpaperEngine::Scripting::ScriptEngine::instance ()
                   .evaluate (m_text.script, props, curValue);

    if (res->getType () == DynamicValue::String)
        return res->getString ();

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
    fontStream->read (reinterpret_cast<char*> (m_fontDataBuffer.data ()), fontDataSize);
    if (!fontStream->good ())
        throw std::runtime_error ("CText: failed to read font data: " + fontPath);

    if (FT_New_Memory_Face (m_ftLibrary,
                            m_fontDataBuffer.data (),
                            static_cast<FT_Long> (m_fontDataBuffer.size ()),
                            0, &m_ftFace))
        throw std::runtime_error ("CText: failed to load font face: " + fontPath);

    FT_Select_Charmap (m_ftFace, FT_ENCODING_UNICODE);
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
    // ── Shader ────────────────────────────────────────────────────────────
    GLuint qv = compileShader (GL_VERTEX_SHADER,   QUAD_VERT);
    GLuint qf = compileShader (GL_FRAGMENT_SHADER, QUAD_FRAG);
    m_quadProgram = linkProgram (qv, qf);
    glDeleteShader (qv);
    glDeleteShader (qf);

    glUseProgram (m_quadProgram);
    glUniform1i (glGetUniformLocation (m_quadProgram, "uTexture"), 0);
    glUseProgram (0);

    // ── Text quad VAO/VBO ─────────────────────────────────────────────────
    glGenVertexArrays (1, &m_quadVao);
    glGenBuffers      (1, &m_quadVbo);
    glBindVertexArray (m_quadVao);
    glBindBuffer      (GL_ARRAY_BUFFER, m_quadVbo);
    glBufferData      (GL_ARRAY_BUFFER, sizeof (float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                           reinterpret_cast<void*> (0));
    glEnableVertexAttribArray (1);
    glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                           reinterpret_cast<void*> (2 * sizeof (float)));
    glBindVertexArray (0);

    // ── Background quad VAO/VBO (same vertex layout as text quad) ─────────
    glGenVertexArrays (1, &m_bgVao);
    glGenBuffers      (1, &m_bgVbo);
    glBindVertexArray (m_bgVao);
    glBindBuffer      (GL_ARRAY_BUFFER, m_bgVbo);
    glBufferData      (GL_ARRAY_BUFFER, sizeof (float) * 6 * 4, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                           reinterpret_cast<void*> (0));
    glEnableVertexAttribArray (1);
    glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof (float),
                           reinterpret_cast<void*> (2 * sizeof (float)));
    glBindVertexArray (0);

    // Geometry is fixed to Text::size, so build it once here.
    updateBgGeometry ();

    // ── 1×1 white texture for solid background fills ───────────────────────
    // GL_RED with value 0xFF → coverage = 1.0 → fragment shader outputs full
    // opacity regardless of UV coordinates.
    const uint8_t white = 0xFF;
    glGenTextures (1, &m_whiteTexture);
    glBindTexture (GL_TEXTURE_2D, m_whiteTexture);
    glTexImage2D  (GL_TEXTURE_2D, 0, GL_RED, 1, 1, 0, GL_RED, GL_UNSIGNED_BYTE, &white);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture (GL_TEXTURE_2D, 0);
}

void CText::shutdownQuad () {
    if (m_bgVao)       { glDeleteVertexArrays (1, &m_bgVao);       m_bgVao       = 0; }
    if (m_bgVbo)       { glDeleteBuffers      (1, &m_bgVbo);       m_bgVbo       = 0; }
    if (m_whiteTexture){ glDeleteTextures     (1, &m_whiteTexture); m_whiteTexture = 0; }
    if (m_quadVao)     { glDeleteVertexArrays (1, &m_quadVao);     m_quadVao     = 0; }
    if (m_quadVbo)     { glDeleteBuffers      (1, &m_quadVbo);     m_quadVbo     = 0; }
    if (m_quadProgram) { glDeleteProgram (m_quadProgram);          m_quadProgram = 0; }
}

void CText::updateBgGeometry () {
    // The background rect always fills the full Text::size box, centred on the
    // model-matrix origin (which updateModelMatrix() has already shifted to the
    // box centre when anchor == "none").
    const float hw = m_text.size.x * 0.5f;
    const float hh = m_text.size.y * 0.5f;

    const float verts[] = {
        -hw, -hh,  0.0f, 0.0f,
         hw, -hh,  1.0f, 0.0f,
         hw,  hh,  1.0f, 1.0f,
        -hw, -hh,  0.0f, 0.0f,
         hw,  hh,  1.0f, 1.0f,
        -hw,  hh,  0.0f, 1.0f,
    };

    glBindVertexArray (m_bgVao);
    glBindBuffer      (GL_ARRAY_BUFFER, m_bgVbo);
    glBufferData      (GL_ARRAY_BUFFER, sizeof (verts), verts, GL_DYNAMIC_DRAW);
    glBindVertexArray (0);
}

void CText::updateQuadGeometry () {
    if (!m_textTexture) return;

    const float tw = static_cast<float> (m_textTexture->getTextureWidth  (0));
    const float th = static_cast<float> (m_textTexture->getTextureHeight (0));

    // Default: centred quad with no offset — correct for anchored objects where
    // the model matrix is already placed at the visual centre.
    float xOff = 0.0f;
    float yOff = 0.0f;

    if (m_text.anchor == "none") {
        // The model matrix origin has been shifted to the box centre by
        // updateModelMatrix(), so alignment offsets are relative to that centre.
        const float boxW = m_text.size.x;
        const float boxH = m_text.size.y;
        const float pad  = m_text.padding;

        // Horizontal
        if (m_text.horizontalAlign == "left")
            xOff = -boxW * 0.5f + pad + tw * 0.5f;
        else if (m_text.horizontalAlign == "right")
            xOff =  boxW * 0.5f - pad - tw * 0.5f;
        // "center" → xOff stays 0

        // Vertical  (OpenGL Y-up: positive = up = visually towards the top)
        if (m_text.verticalAlign == "top")
            yOff =  boxH * 0.5f - pad - th * 0.5f;
        else if (m_text.verticalAlign == "bottom")
            yOff = -boxH * 0.5f + pad + th * 0.5f;
        // "center" → yOff stays 0
    }

    const float x0 = xOff - tw * 0.5f,  x1 = xOff + tw * 0.5f;
    const float y0 = yOff - th * 0.5f,  y1 = yOff + th * 0.5f;

    const float verts[] = {
        x0, y0,  0.0f, 0.0f,
        x1, y0,  1.0f, 0.0f,
        x1, y1,  1.0f, 1.0f,
        x0, y0,  0.0f, 0.0f,
        x1, y1,  1.0f, 1.0f,
        x0, y1,  0.0f, 1.0f,
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

    float cx =  (origin.x - screenW * 0.5f);
    float cy = -(origin.y - screenH * 0.5f);

    // When anchor is "none", origin is not the visual centre — it is the edge
    // of the box that corresponds to the alignment direction.  Shift to the
    // box centre so the centred quad geometry and alignment offsets in
    // updateQuadGeometry land in the right place.
    //
    //  horizontalalign   origin point     shift to centre
    //  ────────────────  ───────────────  ──────────────────
    //  "left"            left edge        +size.x / 2
    //  "center"          centre           none
    //  "right"           right edge       -size.x / 2
    //
    //  verticalalign     origin point     shift to centre (OpenGL Y-up)
    //  ────────────────  ───────────────  ──────────────────────────────
    //  "top"             top edge         -size.y / 2
    //  "center"          centre           none
    //  "bottom"          bottom edge      +size.y / 2
    if (m_text.anchor == "none") {
        if      (m_text.horizontalAlign == "left")   cx += m_text.size.x * 0.5f;
        else if (m_text.horizontalAlign == "right")  cx -= m_text.size.x * 0.5f;
        // "center" → no horizontal shift; origin is already the box centre

        if      (m_text.verticalAlign == "top")      cy -= m_text.size.y * 0.5f;
        else if (m_text.verticalAlign == "bottom")   cy += m_text.size.y * 0.5f;
        // "center" → no vertical shift
    }

    const glm::vec3 scaleVec  = m_text.scale->value->getVec3  ();
    const glm::vec4 anglesVec = m_text.angles->value->getVec4 ();

    m_modelMatrix = glm::mat4 (1.0f);
    m_modelMatrix = glm::translate (m_modelMatrix, glm::vec3 (cx, cy, origin.z));
    m_modelMatrix = glm::rotate (m_modelMatrix, anglesVec.y, glm::vec3 (0.0f, 1.0f, 0.0f));
    m_modelMatrix = glm::rotate (m_modelMatrix, anglesVec.x, glm::vec3 (1.0f, 0.0f, 0.0f));
    m_modelMatrix = glm::rotate (m_modelMatrix, anglesVec.z, glm::vec3 (0.0f, 0.0f, 1.0f));
    m_modelMatrix = glm::scale  (m_modelMatrix, scaleVec);
}

// ─────────────────────────────────────────────
// Text limits
// ─────────────────────────────────────────────
std::string CText::applyTextLimits (FT_Face face, const std::string& text) const {
    // Fast path: no limits requested.
    if (!m_text.limitWidth && !m_text.limitRows)
        return text;

    // ── Split into lines ─────────────────────────────────────────────────────
    std::vector<std::string> lines;
    {
        std::istringstream ss (text);
        std::string line;
        while (std::getline (ss, line))
            lines.push_back (line);
    }

    // ── Enforce maxRows ──────────────────────────────────────────────────────
    if (m_text.limitRows && m_text.maxRows > 0
        && static_cast<int> (lines.size ()) > m_text.maxRows) {
        lines.resize (static_cast<size_t> (m_text.maxRows));
    }

    // ── Enforce maxWidth per line ────────────────────────────────────────────
    if (m_text.limitWidth && m_text.maxWidth > 0.0f) {
        const int maxPx = static_cast<int> (m_text.maxWidth);

        for (auto& l : lines) {
            int    penX    = 0;
            size_t cutByte = l.size ();   // byte offset at which we clip (default: no clip)

            const auto* raw   = reinterpret_cast<const unsigned char*> (l.c_str ());
            const auto* start = raw;
            uint32_t cp = 0;

            while (nextCodePoint (raw, cp)) {
                if (FT_Load_Char (face, static_cast<FT_ULong> (cp), FT_LOAD_DEFAULT) != 0)
                    continue;

                penX += face->glyph->advance.x >> 6;

                if (penX > maxPx) {
                    // Mark cut at the start of the current character.
                    // `raw` already advanced past it, so subtract the byte length
                    // of the last code point by diffing pointers before the advance.
                    cutByte = static_cast<size_t> (
                        reinterpret_cast<const char*> (raw) -
                        reinterpret_cast<const char*> (start)) - /* back up one char */
                        (cp < 0x80 ? 1u : cp < 0x800 ? 2u : cp < 0x10000 ? 3u : 4u);
                    break;
                }
            }

            if (cutByte < l.size ()) {
                if (m_text.limitUseEllipsis && cutByte >= 3)
                    l = l.substr (0, cutByte - 3) + "...";
                else
                    l = l.substr (0, cutByte);
            }
        }
    }

    // ── Re-join ──────────────────────────────────────────────────────────────
    std::string result;
    result.reserve (text.size ());
    for (size_t i = 0; i < lines.size (); ++i) {
        result += lines[i];
        if (i + 1 < lines.size ())
            result += '\n';
    }
    return result;
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

// ─────────────────────────────────────────────
// Visual-property overrides
// ─────────────────────────────────────────────
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