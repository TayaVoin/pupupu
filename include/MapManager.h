#ifndef MAP_MANAGER_H
#define MAP_MANAGER_H

#include <queue>
#include <map>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <cstdint>
#include <mutex>
#include <GL/glew.h>
#include "DataModels.h"
#include "Config.h"

struct Tile {
    int z, x, y;
    int width = 256, height = 256;
    std::vector<uint8_t> rgbaData;
    GLuint texture = 0;
    bool loaded = false;
};

class MapManager {
public:
    MapManager();
    ~MapManager();
    void initGL();

    void requestTile(int z, int x, int y);
    Tile* getTile(int z, int x, int y);

    void renderMap(int winW, int winH, double centerLat, double centerLon, int zoom,
                   const std::vector<MapPoint>& currentPoints,
                   const std::vector<MapPoint>& aggregatedPoints,
                   bool showHeatmap, float heatmapRadiusPixels, float heatmapRadiusMeters,
                   int criterion);

    void clearCache();

private:
    std::string getTilePath(int z, int x, int y);
    bool loadTileFromFile(Tile& tile);
    bool saveTileToFile(const Tile& tile);
    bool downloadTile(Tile& tile);
    static size_t curlWriteCallback(void* data, size_t size, size_t nmemb, void* userp);
    void workerThreadFunc();

    double lonToTileX(double lon, int z);
    double latToTileY(double lat, int z);
    double tileXToLon(double tx, int z);
    double tileYToLat(double ty, int z);

    double metersToPixels(double meters, double centerLat, int zoom, int winH);

    // Тепловая карта (синхронная)
    void generateHeatmapTexture(const std::vector<MapPoint>& points,
                                double centerLat, double centerLon, int zoom,
                                int winW, int winH,
                                float radiusPixels, float radiusMeters,
                                int criterion);
    void drawCircle(std::vector<uint8_t>& pixels, int cx, int cy, int r,
                    uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha = 200);
    void getColorForValue(float value, uint8_t& r, uint8_t& g, uint8_t& b);

    std::atomic<bool> m_workerRunning{true};
    std::thread m_workerThread;
    struct TileRequest { int z, x, y; };
    std::queue<TileRequest> m_requestQueue;
    std::mutex m_queueMutex;
    std::mutex m_cacheMutex;
    std::map<std::string, Tile> m_tileCache;

    GLuint m_heatmapTexture = 0;
    int m_heatmapTexW = 0, m_heatmapTexH = 0;
};

#endif