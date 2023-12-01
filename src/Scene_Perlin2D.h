#pragma once

#include "Scene.h"
#include "WorldView.hpp"
#include "Grid.hpp"
#include "Perlin.hpp"

#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <chrono>
#include <iostream>

class Scene_Perlin2D : public Scene
{   
    sf::Font            m_font;             
    sf::Text            m_text;

    float               m_gridSize = 32;
    bool                m_drawGrid = false;
    bool                m_drawGrey = false;


    Perlin2DNew         m_perlin;
    int                 m_octaves = 1;
    int                 m_seed = 0;
    int                 m_seedSize = 9;
    float               m_persistance = 0.5f;
    bool                m_drawContours = false;
    int                 m_waterLevel = 80;

    Grid<float>         m_grid;

    Vec2                m_drag = { -1, -1 };
    Vec2                m_mouseScreen;
    Vec2                m_mouseWorld;
    Vec2                m_mouseGrid;
    
    WorldView           m_view;
    
    void init();  

    void renderUI();
    void sUserInput();  
    void sRender();
    void calculateNoise();
    
public:

    Scene_Perlin2D(GameEngine * game);

    void onFrame();
};