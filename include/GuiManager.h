#ifndef GUI_MANAGER_H
#define GUI_MANAGER_H

#include "ServerCore.h"
#include "MapManager.h"

class GuiManager {
public:
    GuiManager(ServerCore& server, MapManager& mapManager);
    void render();   // вызывается каждый кадр

    // Параметры карты, доступные для изменения пользователем
    double mapCenterLat = 54.982000;
    double mapCenterLon = 82.893000;
    int mapZoom = 16;
    bool showHeatmap = true;
    bool autoCenter = true;    // автоматически центрировать по новым точкам
    bool firstPointReceived = false;
    int heatmapCriterion = 0;  // 0=RSRP, 1=RSRQ, 2=RSSI, 3=ALTITUDE
    float idwRadius = 40.0f;
    float idwPower = 2.0f;
    bool heatmapDirty = true;
    void requestHeatmapUpdate();
    bool isHeatmapUpdateRequested() const { return m_heatmapRequested; }
    void setAggregatedPoints(const std::vector<AggregatedPoint>& points);

private:
    ServerCore& m_server;
    MapManager& m_mapManager;
    void renderMenuBar();
    void renderCurrentDataWindow();
    void renderSignalPlotsWindow();
    void renderStatisticsWindow();
    void renderMapControlsWindow();
    void renderMapWindow();
    bool m_heatmapRequested = false;
    void generateHeatmap();
    std::vector<MapPoint> m_cachedAggregatedPoints;
    bool m_aggregatedLoaded = false;
};

#endif