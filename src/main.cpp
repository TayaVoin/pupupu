#include <SDL2/SDL.h>
#include <GL/glew.h>
#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "implot.h"
#include "ServerCore.h"
#include "MapManager.h"
#include "GuiManager.h"
#include "Config.h"
#include <iostream>

int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_Window* window = SDL_CreateWindow("Cell Monitor", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          1280, 720, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext glCtx = SDL_GL_CreateContext(window);
    glewInit();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, glCtx);
    ImGui_ImplOpenGL3_Init("#version 130");
    ImGui::StyleColorsDark();

    ServerCore server;
    MapManager mapManager;
    GuiManager gui(server, mapManager);
    mapManager.initGL();

    server.start();

    // Загружаем агрегированные точки из БД для отображения (не меняем центр)
    // Они будут использоваться в GuiManager для отрисовки и тепловой карты
    auto aggPoints = server.loadAggregatedPoints();
    // Передаём их в GuiManager
    gui.setAggregatedPoints(aggPoints);
    std::cout << "Aggregated points loaded from DB: " << aggPoints.size() << std::endl;

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        ImGui::DockSpaceOverViewport();

        gui.render();

        if (server.hasNewData()) {
            auto m = server.getLastMeasurement();
            if (gui.autoCenter && !gui.firstPointReceived && m.location.latitude != 0) {
                gui.mapCenterLat = m.location.latitude;
                gui.mapCenterLon = m.location.longitude;
                gui.firstPointReceived = true;
                gui.autoCenter = false;
            }
            // Проверка выхода точки за пределы экрана (если автоцентрирование выключено)
            if (!gui.autoCenter) {
                double lonMin = gui.mapCenterLon - 180.0 / (1 << gui.mapZoom);
                double lonMax = gui.mapCenterLon + 180.0 / (1 << gui.mapZoom);
                double latMin = gui.mapCenterLat - 90.0 / (1 << gui.mapZoom);
                double latMax = gui.mapCenterLat + 90.0 / (1 << gui.mapZoom);
                double lon = m.location.longitude;
                double lat = m.location.latitude;
                if (lon < lonMin || lon > lonMax || lat < latMin || lat > latMax) {
                    gui.mapCenterLat = lat;
                    gui.mapCenterLon = lon;
                }
            }
        }

        ImGui::Render();
        glClearColor(0.1f,0.1f,0.1f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    server.stop();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glCtx);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}