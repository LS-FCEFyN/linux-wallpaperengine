#pragma once

#include "Object.h"
#include "WallpaperEngine/Data/Model/Types.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <optional>
#include <string>

namespace WallpaperEngine::Data::Model {

struct TextData {
    UserSettingUniquePtr scale;
    UserSettingUniquePtr angles;
    UserSettingUniquePtr visible;
    UserSettingUniquePtr alpha;
    UserSettingUniquePtr color;
    glm::vec2 size;
    UserSettingUniquePtr parallaxDepth;

    std::string font;
    float pointsize;
    std::string horizontalAlign;
    std::string verticalAlign;
    float padding;
    bool opaqueBackground;
    glm::vec3 backgroundColor;

    // The JS script string and its default static value
    std::string script;
    std::string value;

    Material m_material;
};

class Text final : public Object {
public:
    Text (ObjectData base, TextData data) :
        Object (std::move (base)),
        scale (std::move (data.scale)),
        angles (std::move (data.angles)),
        visible (std::move (data.visible)),
        alpha (std::move (data.alpha)),
        color (std::move (data.color)),
        size (data.size),
        parallaxDepth (std::move (data.parallaxDepth)),
        font (std::move (data.font)),
        pointsize (data.pointsize),
        horizontalAlign (std::move (data.horizontalAlign)),
        verticalAlign (std::move (data.verticalAlign)),
        padding (data.padding),
        opaqueBackground (data.opaqueBackground),
        backgroundColor (data.backgroundColor),
        script (std::move (data.script)),
        value (std::move (data.value)),
        m_material (std::move (data.m_material)) {}

    UserSettingUniquePtr scale;
    UserSettingUniquePtr angles;
    UserSettingUniquePtr visible;
    UserSettingUniquePtr alpha;
    UserSettingUniquePtr color;
    glm::vec2 size;
    UserSettingUniquePtr parallaxDepth;

    std::string font;
    float pointsize;
    std::string horizontalAlign;
    std::string verticalAlign;
    float padding;
    bool opaqueBackground;
    glm::vec3 backgroundColor;

    std::string script;
    std::string value;

    Material m_material;
};

using TextUniquePtr = std::unique_ptr<Text>;

} // namespace WallpaperEngine::Data::Model
