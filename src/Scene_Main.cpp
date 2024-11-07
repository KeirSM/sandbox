#include "Scene_Main.h"
#include "GameEngine.h"
#include "Assets.h"
#include "Profiler.hpp"

#include "Processor_Colorizer.h"
#include "Processor_Minecraft.h"
#include "Processor_Pathfinding.h"
#include "Processor_Heat.h"
#include "Source_Camera.h"
#include "Source_Perlin.h"
#include "Source_Snapshot.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <string>
#include <chrono>

#include <SFML/Graphics.hpp>
#include "imgui.h"
#include "imgui-SFML.h"


Scene_Main::Scene_Main(GameEngine * game)
    : Scene(game)
{
    init();
}

void Scene_Main::init()
{
    ImGui::GetStyle().ScaleAllSizes(2.0f);
    ImGui::GetIO().FontGlobalScale = 2.0f;

    registerSource<Source_Camera>("Camera");
    registerSource<Source_Perlin>("Perlin");
    registerSource<Source_Snapshot>("Snapshot");

    registerProcessor<Processor_Colorizer>("Colorizer");
    registerProcessor<Processor_Minecraft>("Minecraft");
    registerProcessor<Processor_Heat>("Heat");
    registerProcessor<Processor_Pathfinding>("Pathfinding");
    m_processorMap.emplace("None", []() {return nullptr; });

    load();
}

void Scene_Main::onFrame()
{
    m_topography = m_source->getTopography();
    if (m_processor && m_topography.rows > 0 && m_topography.cols > 0)
    {
        m_processor->processTopography(m_topography);
    }

    sUserInput();
    sRender();
    if (m_drawUI)
    {
        renderUI();
    }
    m_currentFrame++;
}

// This method is for processing events that are independant of which window is selected
void Scene_Main::sProcessEvent(const sf::Event& event)
{
    // this event triggers when the window is closed
    if (event.type == sf::Event::Closed)
    {
        endScene();
        m_game->quit();
    }

    // this event is triggered when a key is pressed
    if (event.type == sf::Event::KeyPressed)
    {
        switch (event.key.code)
        {
        
        case sf::Keyboard::Escape:
        {
            endScene();
            break;
        }

        case sf::Keyboard::I:
        {
            m_drawUI = !m_drawUI;
            break;
        }

        case sf::Keyboard::F:
        {
            if (!m_game->displayWindow().isOpen())
            {
                m_game->displayWindow().create(sf::VideoMode(1920, 1080), "Display", sf::Style::None);
                m_game->displayWindow().setPosition({ -1920, 0 });
            }
            else
            {
                m_game->displayWindow().close();
            }
        }
        }
    }
}

void Scene_Main::sUserInput()
{
    PROFILE_FUNCTION();

    bool displayOpen = m_game->displayWindow().isOpen();

    sf::Event event;
    while (m_game->window().pollEvent(event))
    {
        ImGui::SFML::ProcessEvent(m_game->window(), event);
        m_viewController.processEvent(m_game->window(), event);
        sProcessEvent(event);

        // happens whenever the mouse is being moved
        if (event.type == sf::Event::MouseMoved)
        {
            m_mouseScreen = { event.mouseMove.x, event.mouseMove.y };
            m_mouseWorld = m_game->window().mapPixelToCoords(m_mouseScreen);
        }

        if (m_source) { m_source->processEvent(event, m_mouseWorld); }
        if (m_processor && !displayOpen)
        {
            m_processor->processEvent(event, m_mouseWorld);
        }
    }

    if (displayOpen)
    {
        sf::Event displayEvent;
        while (m_game->displayWindow().pollEvent(displayEvent))
        {
            sProcessEvent(displayEvent);

            if (m_processor) { m_processor->processEvent(displayEvent, m_mouseDisplay); }

            // happens whenever the mouse is being moved
            if (displayEvent.type == sf::Event::MouseMoved)
            {
                m_mouseDisplay = { (float)displayEvent.mouseMove.x, (float)displayEvent.mouseMove.y };
            }
        }
    }
}

// renders the scene
void Scene_Main::sRender()
{
    PROFILE_FUNCTION();

    m_game->window().clear();
    m_game->displayWindow().clear();

    if (m_source) { m_source->render(m_game->window()); }
    if (!m_processor) { return; }
    if (m_game->displayWindow().isOpen())
    {
        m_processor->render(m_game->displayWindow());
    }
    else
    {
        m_processor->render(m_game->window());
    }
}

void Scene_Main::renderUI()
{
    PROFILE_FUNCTION();

    ImGui::BeginMainMenuBar();

    if (ImGui::Button("Snapshot"))
    {
        saveDataDump();
    }

    ImGui::Text("Framerate: %d", (int)m_game->framerate());
    ImGui::EndMainMenuBar();

    ImGui::Begin("Controls", &m_drawUI);
    ImGui::BeginTabBar("ControlTabs");

    // Source

    if (ImGui::BeginTabItem("Source"))
    {
        if (ImGui::BeginCombo("Selected Source", m_sourceID.c_str()))
        {
            for (auto & [name, _] : m_sourceMap)
            {
                bool selected = name == m_sourceID;
                if (ImGui::Selectable(name.c_str(), &selected))
                {
                    setSource(name);
                }
            }
            ImGui::EndCombo();
        }

        ImGui::Separator();
      
        if (m_source) { m_source->imgui(); }
      
        ImGui::EndTabItem();
    }

    // Processor

    if (ImGui::BeginTabItem("Processor"))
    {
        if (ImGui::BeginCombo("Selected Processor", m_processorID.c_str()))
        {
            for (auto & [name, _] : m_processorMap)
            {
                bool selected = name == m_processorID;
                if (ImGui::Selectable(name.c_str(), &selected))
                {
                    setProcessor(name);
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Separator();

        if (m_processor) { m_processor->imgui(); }

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
    ImGui::End();
}

void Scene_Main::save()
{
    PROFILE_FUNCTION();
    std::ofstream current("currentSave.txt");
    current << m_saveFile << '\n';
    current.close();

    if (m_source) { m_source->save(m_save); }
    if (m_processor) { m_processor->save(m_save); }

    m_save.source = m_sourceID;
    m_save.processor = m_processorID;

    m_save.saveToFile("saves/" + m_saveFile);
}

void Scene_Main::load()
{
    PROFILE_FUNCTION();
    std::ifstream current("currentSave.txt");
    if (current.good())
    {
        current >> m_saveFile;
    }
    current.close();

    std::string file = "saves/" + m_saveFile;

    // First find and initialize the source and processor
    m_save.loadFromFile(file);

    // This initializes the source and processor, even if there was no save file
    setSource(m_save.source);
    setProcessor(m_save.processor);
}

void Scene_Main::setSource(const std::string & source)
{
    if (m_source) { m_source->save(m_save); }
    m_sourceID = source;
    if (m_sourceMap.contains(source))
    {
        m_source = m_sourceMap.at(source)();
    }
    else
    {
        m_source = m_sourceMap.at("Camera")();
    }
    if (m_source) 
    {
        m_source->init();
        m_source->load(m_save);
    }
}

void Scene_Main::setProcessor(const std::string & processor)
{
    if (m_processor) { m_processor->save(m_save); }
    m_processorID = processor;
    if (m_processorMap.contains(processor))
    {
        m_processor = m_processorMap.at(processor)();
    }
    else
    {
        m_processor = m_processorMap.at("Colorizer")();
    }
    if (m_processor) 
    {
        m_processor->init();
        m_processor->load(m_save);
    }
}

void Scene_Main::saveDataDump()
{
    auto now = std::chrono::system_clock::now();
    cv::FileStorage fout(std::format("dataDumps/{0:%F_%H-%M-%S}_snapshot.bin", now), cv::FileStorage::WRITE);
    fout << "matrix" << m_topography;

}

void Scene_Main::endScene()
{
    m_game->displayWindow().close();
    save();
    m_game->quit();
}