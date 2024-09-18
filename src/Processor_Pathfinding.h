#pragma once

#include "TopographyProcessor.h"

class Processor_Pathfinding : public TopographyProcessor
{
public:
    void init();
    void imgui();
    void render(sf::RenderWindow & window);
    void processEvent(const sf::Event & event, const sf::Vector2f & mouse);
    void save(Save & save) const;
    void load(const Save & save);

    void processTopography(const cv::Mat & data);
};

