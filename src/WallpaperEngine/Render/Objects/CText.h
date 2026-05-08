/**
 * @file CText.h
 * @brief Renderable wallpaper text object (clocks, labels, scripted strings).
 *
 * @details
 * `CText` is the render-side counterpart of the `Text` data model.  It reads
 * a `Text` struct (parsed from `scene.json`) and produces a visible, blended
 * quad every frame.
 *
 * ### scene.json fields consumed by this class
 * | JSON key | Stored in | Purpose |
 * |---|---|---|
 * | `font` | `Text::font` | Path to the `.otf`/`.ttf` font file |
 * | `pointsize` | `Text::pointsize` | FreeType pixel size (`pointsize * 2`) |
 * | `color` | `Text::color` | RGB tint applied to the coverage mask |
 * | `alpha` | `Text::alpha` | Global opacity of the text quad |
 * | `scale` | `Text::scale` | Per-axis model-matrix scale |
 * | `angles` | `Text::angles` | Euler angles (yaw / pitch / roll in radians) |
 * | `origin` | `Object::origin` | World-space position (pixels from top-left) |
 * | `visible` | `Text::visible` | Render guard; skips draw when false |
 * | `anchor` | `Text::anchor` | Positioning mode; `"none"` = origin is top-left of size box |
 * | `size` | `Text::size` | Pixel dimensions of the text bounding box |
 * | `horizontalalign` | `Text::horizontalAlign` | Horizontal alignment within the box |
 * | `verticalalign` | `Text::verticalAlign` | Vertical alignment within the box |
 * | `padding` | `Text::padding` | Inner margin for alignment offsets |
 * | `depthtest` | `Text::depthTest` | `"enabled"` / `"disabled"` |
 * | `opaquebackground` | `Text::opaqueBackground` | Whether to draw a solid background rect |
 * | `backgroundcolor` | `Text::backgroundColor` | RGB colour of the background rect |
 * | `backgroundbrightness` | `Text::backgroundBrightness` | Brightness multiplier for the background |
 * | `limitwidth` | `Text::limitWidth` | Whether to clip text to `maxwidth` |
 * | `maxwidth` | `Text::maxWidth` | Maximum line width in pixels |
 * | `limitrows` | `Text::limitRows` | Whether to clip text to `maxrows` lines |
 * | `maxrows` | `Text::maxRows` | Maximum number of visible lines |
 * | `limituseellipsis` | `Text::limitUseEllipsis` | Append `…` when a line is clipped |
 * | `text.value` | `Text::value` | Static fallback string |
 * | `text.script` | `Text::script` | JS `update(value)` function body |
 *
 * ### Rendering pipeline
 * 1. `setup()` loads the font via FreeType, compiles a bespoke coverage-mask
 *    shader, allocates the VAO/VBO, and creates a `CTextTexture` that is
 *    shared through `CRenderable::m_texture`.
 * 2. `render()` evaluates the JS script each frame, rebuilds the texture only
 *    when the resulting string has changed, recomputes the model matrix and
 *    draws six vertices (two triangles) with alpha blending enabled.
 *
 * ### Shader
 * The vertex shader (`QUAD_VERT`) transforms a centered `[-hw,+hw] × [-hh,+hh]`
 * quad with a full MVP matrix.  The fragment shader (`QUAD_FRAG`) samples the
 * single-channel `CTextTexture`, multiplies the red value (coverage) with the
 * `uColor` uniform, and writes the result with pre-multiplied alpha blending
 * (`GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA`).
 *
 * @see CTextTexture
 * @see Text
 */
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

/**
 * @class CText
 * @brief Renders a dynamic text object as a blended textured quad.
 *
 * @details
 * Each wallpaper text object defined in `scene.json` (typically a clock whose
 * content is computed by a JavaScript `update()` function) is represented by
 * one `CText` instance.
 *
 * The class manages three independent subsystems, each with a matching
 * setup / shutdown pair:
 *
 * | Subsystem | Init | Teardown | Responsibility |
 * |---|---|---|---|
 * | FreeType | `loadFont()` | `shutdownFont()` | Load `.otf`/`.ttf`, set pixel size |
 * | OpenGL quad | `setupQuad()` | `shutdownQuad()` | Compile shader, allocate VAO/VBO |
 * | Script | `setupScript()` | — | Detect JS body; mark ready flag |
 *
 * The font data buffer (`m_fontDataBuffer`) is kept alive for the entire
 * lifetime of the object because FreeType's memory-face mode requires the
 * raw bytes to remain valid as long as `m_ftFace` is open.
 *
 * @note `CText` is created and owned by `CObject`; `CObject` is declared a
 *       `friend` so it can call the constructor directly.
 */
class CText final : public CRenderable {
    friend CObject;

public:
    // ─────────────────────────────────────────────────────────────────────────
    // Construction / destruction
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Constructs the text renderable from scene data.
     *
     * @param scene Owning scene; used to access the camera (projection,
     *              look-at, viewport dimensions) and the asset locator.
     * @param text  Immutable data parsed from the `scene.json` text object
     *              (font path, point size, color, script, …).
     *
     * @note No GPU resources are allocated here.  Call `setup()` before the
     *       first `render()`.
     */
    CText (Wallpapers::CScene& scene, const Text& text);

    /**
     * @brief Destructor.  Calls `shutdownFont()` and `shutdownQuad()`;
     *        the shared `m_textTexture` releases the GL texture via its own
     *        destructor.
     */
    ~CText () override;

    // ─────────────────────────────────────────────────────────────────────────
    // CRenderable interface
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief One-time initialisation — idempotent after the first call.
     *
     * @details Execution order:
     *  1. `loadFont()`    — opens the font asset, creates an `FT_Face`.
     *  2. `setupQuad()`   — compiles the coverage-mask shader, allocates
     *                       VAO / VBO for both text and background quads, and
     *                       creates the 1×1 white texture used for the
     *                       opaque-background rect.
     *  3. `setupScript()` — detects whether the text has a JS body.
     *  4. Allocates a `CTextTexture` and stores it in both `m_textTexture`
     *     (for local use) and `CRenderable::m_texture` (for the pass system).
     *  5. Evaluates the script / reads the static value, applies text limits,
     *     calls `CTextTexture::rebuild()` to produce the first rasterised frame.
     *  6. Sets `m_initialized = true`.
     *
     * @throws std::runtime_error if FreeType initialisation fails or the
     *         font asset cannot be opened / read.
     */
    void setup () override;

    /**
     * @brief Per-frame draw call — renders the text quad if visible.
     *
     * @details
     *  - Returns immediately if `!m_initialized` or if `Text::visible` is
     *    false (allowing the wallpaper engine to hide the object at runtime).
     *  - Re-evaluates the JS script.  If the result differs from the cached
     *    `m_currentText`, `CTextTexture::rebuild()` and `updateQuadGeometry()`
     *    are called to refresh the GPU texture and quad dimensions.
     *  - Recomputes `m_modelMatrix` from the current origin, scale and angles.
     *  - Optionally draws a solid background rectangle when
     *    `Text::opaqueBackground` is true, tinted by `Text::backgroundColor`
     *    and `Text::backgroundBrightness`.
     *  - Draws 6 vertices (`GL_TRIANGLES`) with `GL_BLEND` enabled using the
     *    `GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA` blend equation.
     *  - Temporarily disables `GL_DEPTH_TEST` when `Text::depthTest` is
     *    `"disabled"`, restoring the default state after drawing.
     *
     * @note The framebuffer binding is managed by `CScene`; `render()` must
     *       not alter it.
     */
    void render () override;

    // ─────────────────────────────────────────────────────────────────────────
    // CRenderable visual-property overrides
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Returns a fixed brightness of 1.0 — text brightness is not
     *        individually adjustable via the `scene.json` schema.
     */
    [[nodiscard]] const float& getBrightness () const override;

    /**
     * @brief Returns the current alpha value from the `Text::alpha` user
     *        setting (the `"alpha"` field in `scene.json`).
     */
    [[nodiscard]] const float& getUserAlpha () const override;

    /**
     * @brief Same as `getUserAlpha()` — text has a single opacity level with
     *        no separate "user" vs "composite" distinction.
     */
    [[nodiscard]] const float& getAlpha () const override;

    /**
     * @brief Returns the RGB tint colour from the `Text::color` user setting
     *        (the `"color"` field in `scene.json`).
     */
    [[nodiscard]] const glm::vec3& getColor () const override;

    /**
     * @brief Returns the RGBA form of the colour setting.
     *
     * @warning The `color` user setting is stored as a `vec3`; calling
     *          `getVec4()` on it returns `w = 0`.  Prefer `getColor()` +
     *          `getAlpha()` for blending purposes (as `render()` does).
     */
    [[nodiscard]] const glm::vec4& getColor4 () const override;

    /**
     * @brief Returns the same value as `getColor()` — text objects have no
     *        additional scene-level colour compositing.
     */
    [[nodiscard]] const glm::vec3& getCompositeColor () const override;

    // ─────────────────────────────────────────────────────────────────────────
    // Accessors
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Returns the immutable `Text` data struct this renderable was
     *        built from.
     */
    [[nodiscard]] const Text& getText () const;

private:
    // ─────────────────────────────────────────────────────────────────────────
    // Script subsystem
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Determines at setup time whether a JS script body is present.
     *
     * @details If `Text::script` is empty the static `Text::value` string is
     *          used as-is and `m_scriptReady` stays false, bypassing the
     *          ScriptEngine entirely.
     */
    void setupScript ();

    /**
     * @brief Evaluates the JS `update(value)` function and returns the result.
     *
     * @details If `m_scriptReady` is false the cached `m_currentText` (or the
     *          static `Text::value` if the cache is empty) is returned without
     *          invoking the ScriptEngine.
     *
     *          When the engine returns a `DynamicValue` of type `String` its
     *          string payload is returned; otherwise the previous value is
     *          preserved so the display does not flicker on script errors.
     *
     * @return The string to display this frame.
     */
    std::string evaluateScript ();

    /**
     * @brief True once `setupScript()` has confirmed a non-empty JS body.
     *        Guards the per-frame ScriptEngine call in `evaluateScript()`.
     */
    bool m_scriptReady { false };

    // ─────────────────────────────────────────────────────────────────────────
    // FreeType subsystem
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Initialises FreeType, reads the font asset into `m_fontDataBuffer`,
     *        and creates an `FT_Face` via `FT_New_Memory_Face`.
     *
     * @details The pixel size is set to `Text::pointsize * 2` to match
     *          Wallpaper Engine's original sizing convention.
     *          `glPixelStorei(GL_UNPACK_ALIGNMENT, 1)` is also called here
     *          because FreeType bitmaps are byte-aligned.
     *
     * @throws std::runtime_error on any FreeType or asset-loading failure.
     */
    void loadFont ();

    /**
     * @brief Releases the FreeType face and library handles and clears the
     *        in-memory font buffer.
     */
    void shutdownFont ();

    /** @brief FreeType library handle; null until `loadFont()` succeeds. */
    FT_Library m_ftLibrary { nullptr };

    /** @brief FreeType face handle; null until `loadFont()` succeeds. */
    FT_Face m_ftFace { nullptr };

    /**
     * @brief Raw font file bytes kept alive for the duration of `m_ftFace`.
     *
     * `FT_New_Memory_Face` does **not** copy the data — the caller must keep
     * the buffer valid until `FT_Done_Face` is called.
     */
    std::vector<uint8_t> m_fontDataBuffer;

    // ─────────────────────────────────────────────────────────────────────────
    // OpenGL quad subsystem
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Compiles `QUAD_VERT` + `QUAD_FRAG`, links `m_quadProgram`, and
     *        allocates the VAO / VBO for the text quad with `GL_DYNAMIC_DRAW`
     *        storage.  Also allocates the background quad VAO / VBO and the
     *        1×1 `GL_RED` white texture used for solid background fills.
     *
     * @details Vertex layout (stride = 4 floats):
     *  - attribute 0 — `vec2` position (pixels, object-space, Y-up)
     *  - attribute 1 — `vec2` UV (0..1, Y-flipped to match FreeType's top-down
     *                  bitmap orientation)
     *
     * @throws std::runtime_error if shader compilation or program linking fails.
     */
    void setupQuad ();

    /**
     * @brief Deletes all VAOs, VBOs, the white texture, and the shader program.
     */
    void shutdownQuad ();

    /**
     * @brief Rebuilds the six-vertex text quad to match the current texture
     *        size, applying horizontal / vertical alignment offsets within the
     *        `Text::size` bounding box when `Text::anchor` is `"none"`.
     *
     * @details Must be called after every `CTextTexture::rebuild()` to keep
     *          the geometry in sync with the rasterised dimensions.
     */
    void updateQuadGeometry ();

    /**
     * @brief Builds the six-vertex background quad from `Text::size`.
     *
     * @details Only needs to be called once during `setup()` because the box
     *          dimensions do not change at runtime.
     */
    void updateBgGeometry ();

    /** @brief VAO wrapping the text quad geometry. */
    GLuint m_quadVao { 0 };

    /** @brief VBO holding the six `(pos.xy, uv.xy)` text vertices. */
    GLuint m_quadVbo { 0 };

    /** @brief VAO wrapping the solid background rect geometry. */
    GLuint m_bgVao { 0 };

    /** @brief VBO holding the six `(pos.xy, uv.xy)` background vertices. */
    GLuint m_bgVbo { 0 };

    /**
     * @brief 1×1 `GL_RED = 0xFF` texture used as a coverage mask for the
     *        solid background rect (makes the fragment shader output full
     *        opacity at every texel).
     */
    GLuint m_whiteTexture { 0 };

    /** @brief Linked shader program: coverage-mask vertex + fragment stages. */
    GLuint m_quadProgram { 0 };

    // ─────────────────────────────────────────────────────────────────────────
    // Texture
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Shared ownership of the GPU-side text texture.
     *
     * Also stored in `CRenderable::m_texture` so that `getTexture()` and the
     * standard pass / detection system can access the same provider without
     * any extra indirection.  The `shared_ptr` ensures the GL texture is
     * deleted exactly once when both references are dropped.
     */
    std::shared_ptr<CTextTexture> m_textTexture;

    // ─────────────────────────────────────────────────────────────────────────
    // Per-frame state
    // ─────────────────────────────────────────────────────────────────────────

    /** @brief Immutable reference to the parsed `scene.json` text data. */
    const Text& m_text;

    /**
     * @brief The string rendered in the last frame (after limit processing).
     *
     * Compared against the new `evaluateScript()` result every frame; a
     * mismatch triggers `applyTextLimits()`, `CTextTexture::rebuild()`, and
     * `updateQuadGeometry()`.
     */
    std::string m_currentText;

    /**
     * @brief Guards against double-initialisation; set to true at the end of
     *        the first successful `setup()` call.
     */
    bool m_initialized { false };

    /**
     * @brief Model matrix rebuilt each frame from the object's origin, scale
     *        and Euler angles (yaw → pitch → roll order).
     *
     * When `Text::anchor` is `"none"` the origin represents the top-left
     * corner of the `Text::size` box; the matrix is shifted by half the box
     * dimensions so that the centred quad geometry lands in the correct place.
     */
    glm::mat4 m_modelMatrix { 1.0f };

    /**
     * @brief Recalculates `m_modelMatrix` from the current `Text` transform
     *        settings and the scene camera's viewport dimensions.
     *
     * @details
     *  - **Translation** — maps from top-left pixel origin to a centred
     *    coordinate system (screen centre = origin).  When `anchor == "none"`
     *    an additional half-box offset is applied so that the origin is treated
     *    as the top-left corner of the bounding box.
     *  - **Rotation** — applies yaw (Y), pitch (X), roll (Z) in that order.
     *  - **Scale** — uniform per-axis scale from `Text::scale`.
     */
    void updateModelMatrix ();

    // ─────────────────────────────────────────────────────────────────────────
    // Text-limit helpers
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Applies `limitRows` / `maxRows`, `limitWidth` / `maxWidth`, and
     *        `limitUseEllipsis` constraints to @p text before rasterisation.
     *
     * @details Performs one lightweight measurement pass using FreeType advance
     *          widths (no bitmap rendering).  Lines that exceed `maxWidth` are
     *          truncated, optionally with a trailing `"..."` suffix.  Lines
     *          beyond `maxRows` are discarded entirely.
     *
     * @param face The pre-loaded FreeType face with pixel size already set.
     * @param text The raw string coming from `evaluateScript()`.
     * @return A (possibly shortened) copy of @p text ready for `rebuild()`.
     */
    std::string applyTextLimits (FT_Face face, const std::string& text) const;

    // ─────────────────────────────────────────────────────────────────────────
    // Shader helpers
    // ─────────────────────────────────────────────────────────────────────────

    /**
     * @brief Compiles a single GLSL shader stage.
     *
     * @param type   `GL_VERTEX_SHADER` or `GL_FRAGMENT_SHADER`.
     * @param source Null-terminated GLSL source string.
     * @return The compiled shader object handle.
     *
     * @throws std::runtime_error with the driver info-log on compilation failure.
     */
    static GLuint compileShader (GLenum type, const char* source);

    /**
     * @brief Links a vertex and fragment shader into a complete program.
     *
     * @param vert Compiled vertex shader handle.
     * @param frag Compiled fragment shader handle.
     * @return The linked program handle.
     *
     * @throws std::runtime_error with the driver info-log on link failure.
     */
    static GLuint linkProgram (GLuint vert, GLuint frag);
};

} // namespace WallpaperEngine::Render::Objects