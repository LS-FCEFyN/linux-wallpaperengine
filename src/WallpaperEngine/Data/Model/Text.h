/**
 * @file Text.h
 * @brief Data-model types for wallpaper text objects (clocks, labels, scripted strings).
 *
 * @details
 * This file defines the plain-data layer for text objects as they appear in
 * `scene.json`.  No GPU resources or scripting state live here; those are
 * owned by the render-side counterpart `CText`.
 *
 * ### Typical scene.json excerpt (clock object)
 * @code{.json}
 * {
 *   "id": 105,
 *   "font": "fonts/Alcubierre.otf",
 *   "pointsize": 32.0,
 *   "color": "0.831 0.753 0.612",
 *   "alpha": 1.0,
 *   "scale": "1.000 1.000 1.000",
 *   "angles": "0.000 0.000 0.000",
 *   "origin": "1991.154 273.384 0.000",
 *   "anchor": "none",
 *   "size": "1170.000 130.000",
 *   "horizontalalign": "left",
 *   "verticalalign": "center",
 *   "padding": 0,
 *   "depthtest": "enabled",
 *   "opaquebackground": false,
 *   "backgroundcolor": "0.000 0.000 0.000",
 *   "backgroundbrightness": 1.0,
 *   "limitwidth": false,
 *   "maxwidth": 500.0,
 *   "limitrows": false,
 *   "maxrows": 1,
 *   "limituseellipsis": false,
 *   "visible": true,
 *   "text": {
 *     "value": "<Clock>",
 *     "script": "export function update(value) { ... }"
 *   }
 * }
 * @endcode
 *
 * @see CText   — render-side consumer of this data.
 * @see Object  — base class providing the common `ObjectData` fields.
 */
#pragma once

#include "Object.h"
#include "WallpaperEngine/Data/Model/Types.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <string>

namespace WallpaperEngine::Data::Model {

// ─────────────────────────────────────────────────────────────────────────────
// TextData — intermediate bag-of-fields used during JSON parsing
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @struct TextData
 * @brief Intermediate aggregate populated by the JSON parser before a `Text`
 *        instance is constructed.
 *
 * @details
 * The parser fills this struct field-by-field, then passes it to the `Text`
 * constructor which moves each member into its final storage.  Using a
 * separate struct avoids a constructor with a large parameter list and keeps
 * the parser code straightforward.
 *
 * All `UserSettingUniquePtr` members are user-adjustable properties that the
 * Wallpaper Engine UI can expose as sliders or colour pickers at runtime.
 */
struct TextData {
    // ── Transform / visibility ──────────────────────────────────────────────

    /** @brief Per-axis scale factor (`"scale"` in scene.json, e.g. `"1 1 1"`). */
    UserSettingUniquePtr scale;

    /**
     * @brief Euler rotation angles in radians (`"angles"` in scene.json).
     *        Layout: `vec4(pitch, yaw, roll, unused)`.
     *        Applied in yaw → pitch → roll order by `CText::updateModelMatrix`.
     */
    UserSettingUniquePtr angles;

    /**
     * @brief Visibility toggle (`"visible"` in scene.json).
     *        When false, `CText::render()` returns immediately without
     *        issuing any draw calls.
     */
    UserSettingUniquePtr visible;

    /**
     * @brief Global opacity in [0, 1] (`"alpha"` in scene.json).
     *        Multiplied with the FreeType coverage mask in the fragment shader.
     */
    UserSettingUniquePtr alpha;

    /**
     * @brief RGB text colour (`"color"` in scene.json, e.g. `"0.831 0.753 0.612"`).
     *
     * @note This is a `vec3` setting; calling `getVec4()` on it yields `w = 0`.
     *       Use `getVec3()` + the separate `alpha` setting for blending.
     */
    UserSettingUniquePtr color;

    /** @brief Pixel dimensions of the text bounding box (`"size"` in scene.json). */
    glm::vec2 size;

    /**
     * @brief Parallax depth weight (`"parallaxDepth"` in scene.json, `"1 1"` = full depth).
     *        Used by the camera parallax system when `cameraparallax` is enabled.
     */
    UserSettingUniquePtr parallaxDepth;

    // ── Layout / positioning ─────────────────────────────────────────────────

    /**
     * @brief Positioning anchor mode (`"anchor"` in scene.json).
     *
     * Controls how `origin` is interpreted:
     * | Value | Meaning |
     * |---|---|
     * | `"none"` | `origin` is the **top-left corner** of the `size` box |
     * | anything else | `origin` is the **visual centre** of the text quad |
     *
     * `CText::updateModelMatrix()` shifts the model matrix by half the box
     * dimensions when this is `"none"` so that centred quad geometry lands at
     * the correct screen position.
     */
    std::string anchor;

    /**
     * @brief Horizontal text alignment within the bounding box
     *        (`"horizontalalign"` in scene.json).
     *        Accepted values: `"left"`, `"center"`, `"right"`.
     *        Only applied when `anchor == "none"`.
     */
    std::string horizontalAlign;

    /**
     * @brief Vertical text alignment within the bounding box
     *        (`"verticalalign"` in scene.json).
     *        Accepted values: `"top"`, `"center"`, `"bottom"`.
     *        Only applied when `anchor == "none"`.
     */
    std::string verticalAlign;

    /**
     * @brief Inner padding in pixels around the text content
     *        (`"padding"` in scene.json).
     *        Applied as an inset when computing alignment offsets.
     */
    float padding;

    // ── Depth / rendering ────────────────────────────────────────────────────

    /**
     * @brief Depth-test mode (`"depthtest"` in scene.json).
     *
     * | Value | Behaviour |
     * |---|---|
     * | `"enabled"` (default) | `GL_DEPTH_TEST` is left in its current state |
     * | `"disabled"` | `GL_DEPTH_TEST` is disabled for this object's draw call and restored afterwards |
     */
    std::string depthTest;

    // ── Background ───────────────────────────────────────────────────────────

    /**
     * @brief When true the text background quad is drawn as a fully opaque
     *        rectangle in `backgroundColor` before the glyphs
     *        (`"opaquebackground"` in scene.json).
     */
    bool opaqueBackground;

    /**
     * @brief Background fill colour used when `opaqueBackground` is true
     *        (`"backgroundcolor"` in scene.json).
     */
    glm::vec3 backgroundColor;

    /**
     * @brief Brightness multiplier applied to `backgroundColor` before upload
     *        to the `uColor` uniform (`"backgroundbrightness"` in scene.json).
     *        1.0 = unchanged; values < 1.0 darken the background.
     */
    float backgroundBrightness;

    // ── Text limits ──────────────────────────────────────────────────────────

    /**
     * @brief When true, each line of text is clipped to `maxWidth` pixels
     *        (`"limitwidth"` in scene.json).
     */
    bool limitWidth;

    /**
     * @brief Maximum line width in pixels; only enforced when `limitWidth`
     *        is true (`"maxwidth"` in scene.json).
     */
    float maxWidth;

    /**
     * @brief When true, the rendered text is clipped to at most `maxRows`
     *        lines (`"limitrows"` in scene.json).
     */
    bool limitRows;

    /**
     * @brief Maximum number of visible lines; only enforced when `limitRows`
     *        is true (`"maxrows"` in scene.json).
     */
    int maxRows;

    /**
     * @brief When true and a line is clipped by `limitWidth`, the last three
     *        characters before the cut are replaced with `"..."`
     *        (`"limituseellipsis"` in scene.json).
     */
    bool limitUseEllipsis;

    // ── Typography ───────────────────────────────────────────────────────────

    /**
     * @brief Asset-relative path to the font file (`"font"` in scene.json).
     *        Supports `.otf` and `.ttf` formats via FreeType.
     */
    std::string font;

    /**
     * @brief FreeType pixel size divisor (`"pointsize"` in scene.json).
     *        `CText::loadFont()` passes `pointsize * 2` to `FT_Set_Pixel_Sizes`
     *        to match Wallpaper Engine's native sizing convention.
     */
    float pointsize;

    // ── Script / content ─────────────────────────────────────────────────────

    /**
     * @brief Body of the JavaScript `update(value)` function
     *        (`"text.script"` in scene.json).
     *
     * Evaluated every frame by `CText::evaluateScript()` via the ScriptEngine.
     * Empty string means the text is static and equals `value`.
     */
    std::string script;

    /**
     * @brief Static display string or editor placeholder
     *        (`"text.value"` in scene.json, e.g. `"<Clock>"`).
     *        Used verbatim when `script` is empty; otherwise serves as the
     *        initial `value` argument passed to the JS `update()` function.
     */
    std::string value;

    /**
     * @brief Named properties made available to the script at runtime
     *        (`"text.scriptproperties"` in scene.json).
     *
     * Values may be plain primitives or `{"user": "<propName>", "value": <default>}`
     * objects — the latter are resolved to live `UserSetting` references by the
     * parser so the script always sees the user's current preference.
     */
    std::map<std::string, DynamicValue> scriptProperties;

    // ── Material ─────────────────────────────────────────────────────────────

    /**
     * @brief Material descriptor associated with this text object.
     *        Passed through to `CRenderable` so the standard effect / pass
     *        system can inspect material properties if needed.
     */
    Material m_material;
};

// ─────────────────────────────────────────────────────────────────────────────
// Text — immutable data class owned by the scene object graph
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class Text
 * @brief Immutable data model for a wallpaper text object.
 *
 * @details
 * `Text` extends `Object` with all typography and scripting fields needed to
 * render a dynamic text quad.  Instances are constructed once by the JSON
 * parser (from a `TextData` aggregate) and then handed — by const reference —
 * to `CText` for the lifetime of the wallpaper session.
 *
 * All `UserSettingUniquePtr` members are runtime-adjustable via the Wallpaper
 * Engine settings panel (e.g. colour, alpha, visibility).
 *
 * @note All members are public and immutable in the sense that ownership is
 *       non-transferable; the `UserSettingUniquePtr` values may change
 *       at runtime through the settings system.
 */
class Text final : public Object {
public:
    /**
     * @brief Constructs a `Text` object by moving data out of a `TextData`
     *        aggregate and an `ObjectData` base.
     *
     * @param base Common object fields (id, name, origin, …) from the parser.
     * @param data Text-specific fields populated by the JSON parser.
     */
    Text (ObjectData base, TextData data) :
        Object (std::move (base)),
        scale             (std::move (data.scale)),
        angles            (std::move (data.angles)),
        visible           (std::move (data.visible)),
        alpha             (std::move (data.alpha)),
        color             (std::move (data.color)),
        size              (data.size),
        parallaxDepth     (std::move (data.parallaxDepth)),
        anchor            (std::move (data.anchor)),
        horizontalAlign   (std::move (data.horizontalAlign)),
        verticalAlign     (std::move (data.verticalAlign)),
        padding           (data.padding),
        depthTest         (std::move (data.depthTest)),
        opaqueBackground  (data.opaqueBackground),
        backgroundColor   (data.backgroundColor),
        backgroundBrightness (data.backgroundBrightness),
        limitWidth        (data.limitWidth),
        maxWidth          (data.maxWidth),
        limitRows         (data.limitRows),
        maxRows           (data.maxRows),
        limitUseEllipsis  (data.limitUseEllipsis),
        font              (std::move (data.font)),
        pointsize         (data.pointsize),
        script            (std::move (data.script)),
        value             (std::move (data.value)),
        scriptProperties  (std::move (data.scriptProperties)),
        m_material        (std::move (data.m_material)) {}

    // ── Transform / visibility ───────────────────────────────────────────────

    /** @brief Per-axis model-matrix scale (UserSetting wrapping a `vec3`). */
    UserSettingUniquePtr scale;

    /**
     * @brief Euler rotation angles as a `vec4(pitch, yaw, roll, unused)`.
     *        Applied in yaw → pitch → roll order inside `CText::updateModelMatrix`.
     */
    UserSettingUniquePtr angles;

    /**
     * @brief Render-enable flag.  `CText::render()` checks this every frame
     *        and skips the draw call when false.
     */
    UserSettingUniquePtr visible;

    /**
     * @brief Global opacity [0, 1].  Read each frame and forwarded to the
     *        `uColor.a` uniform in `CText::render()`.
     */
    UserSettingUniquePtr alpha;

    /**
     * @brief RGB tint applied to the glyph coverage mask in the fragment shader.
     *
     * @warning Stored as `vec3`; `getVec4()` will return `w = 0`.
     */
    UserSettingUniquePtr color;

    /** @brief Width and height of the layout bounding box in pixels. */
    glm::vec2 size;

    /** @brief Parallax depth weight (`vec2`); `1 1` = moves fully with camera. */
    UserSettingUniquePtr parallaxDepth;

    // ── Layout / positioning ─────────────────────────────────────────────────

    /**
     * @brief Positioning anchor mode (`"none"` = origin is top-left of box;
     *        any other value = origin is the visual centre of the quad).
     *
     * @see CText::updateModelMatrix for the coordinate-space conversion.
     */
    std::string anchor;

    /**
     * @brief Horizontal alignment within the bounding box.
     *        Values: `"left"`, `"center"`, `"right"`.
     *        Active only when `anchor == "none"`.
     */
    std::string horizontalAlign;

    /**
     * @brief Vertical alignment within the bounding box.
     *        Values: `"top"`, `"center"`, `"bottom"`.
     *        Active only when `anchor == "none"`.
     */
    std::string verticalAlign;

    /** @brief Inner padding around the text content in pixels. */
    float padding;

    // ── Depth / rendering ────────────────────────────────────────────────────

    /**
     * @brief Depth-test mode for this object's draw call.
     *        `"enabled"` (default) leaves `GL_DEPTH_TEST` untouched;
     *        `"disabled"` suppresses it for this object only.
     */
    std::string depthTest;

    // ── Background ───────────────────────────────────────────────────────────

    /**
     * @brief When true, a filled rectangle in `backgroundColor` is drawn
     *        behind the glyphs.
     */
    bool opaqueBackground;

    /** @brief Background fill colour; only visible when `opaqueBackground` is true. */
    glm::vec3 backgroundColor;

    /**
     * @brief Brightness multiplier for the background colour [0, ∞).
     *        Applied as `backgroundColor * backgroundBrightness` before the
     *        `uColor` uniform is set.  1.0 = unchanged.
     */
    float backgroundBrightness;

    // ── Text limits ──────────────────────────────────────────────────────────

    /**
     * @brief When true, each line is clipped to `maxWidth` pixels before
     *        rasterisation.
     */
    bool limitWidth;

    /** @brief Maximum line width in pixels; enforced when `limitWidth` is true. */
    float maxWidth;

    /**
     * @brief When true, the string is clipped to at most `maxRows` newline-
     *        separated lines before rasterisation.
     */
    bool limitRows;

    /** @brief Maximum number of visible lines; enforced when `limitRows` is true. */
    int maxRows;

    /**
     * @brief When true and a line is clipped by `limitWidth`, the truncated
     *        tail is replaced with `"..."`.
     */
    bool limitUseEllipsis;

    // ── Typography ───────────────────────────────────────────────────────────

    /**
     * @brief Asset-relative path to the font file.
     *        FreeType accepts both OpenType (`.otf`) and TrueType (`.ttf`).
     */
    std::string font;

    /**
     * @brief Raw point-size value from `scene.json`.
     *        `CText::loadFont()` scales this by 2 when calling
     *        `FT_Set_Pixel_Sizes`.
     */
    float pointsize;

    // ── Script / content ─────────────────────────────────────────────────────

    /**
     * @brief Full JavaScript source of the `update(value)` function.
     *        Empty when the text is static.
     */
    std::string script;

    /**
     * @brief Static display value or editor placeholder (e.g. `"<Clock>"`).
     *        Used as-is when `script` is empty; passed as the initial `value`
     *        argument to the JS `update()` function otherwise.
     */
    std::string value;

    /**
     * @brief Named properties made available to the script at runtime.
     *
     * Primitive entries are stored directly.  Object entries of the form
     * `{"user": "<propName>", "value": <default>}` are resolved by the parser
     * to the live `UserSetting` value so the script always reflects the user's
     * current preference.
     */
    std::map<std::string, DynamicValue> scriptProperties;

    // ── Material ─────────────────────────────────────────────────────────────

    /** @brief Material descriptor forwarded to `CRenderable` for the pass system. */
    Material m_material;
};

/** @brief Convenience alias for a heap-allocated `Text` with unique ownership. */
using TextUniquePtr = std::unique_ptr<Text>;

} // namespace WallpaperEngine::Data::Model