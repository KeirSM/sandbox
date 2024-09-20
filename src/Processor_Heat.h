#pragma once

#include "HeatMap.h"
#include "Profiler.hpp"
#include "SandboxProjector.h"
#include "Tools.h"
#include "TopographyProcessor.h"

class Processor_Heat : public TopographyProcessor
{
    HeatMap heatMap{{
        { { 100, 100 }, 100.0 }
    }};

    SandBoxProjector m_projector;
    cv::Mat m_cvTransformedDepthImage32f;
    bool m_drawProjection = true;
    sf::Image m_sfTransformedDepthImage;
    sf::Texture m_sfTransformedDepthTexture;
    sf::Sprite m_sfTransformedDepthSprite;
    sf::Shader          m_shader;
    int                 m_selectedShaderIndex = 0;
    bool                m_drawContours = false;
    int                 m_numberOfContourLines = 19;

public:
    void init();
    void imgui();
    void render(sf::RenderWindow& window);
    void processEvent(const sf::Event& event, const sf::Vector2f& mouse);
    void save(Save& save) const;
    void load(const Save& save);

    void processTopography(const cv::Mat& data);
};
