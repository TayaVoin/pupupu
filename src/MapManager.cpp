#include "MapManager.h"
#include "Config.h"
#include <curl/curl.h>
#include "imgui.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <cmath>
#include <queue>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <GL/glew.h>
#include <filesystem>
namespace fs = std::filesystem;

#define PI 3.14159265358979323846
#define DEG_TO_RAD (PI/180.0)

// ----------------------------------------------------------------------------
// Конструктор / деструктор
// ----------------------------------------------------------------------------
MapManager::MapManager() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    m_workerRunning = true;
    m_workerThread = std::thread(&MapManager::workerThreadFunc, this);
    m_heatmapWorker = std::thread(&MapManager::heatmapWorkerFunc, this);
}

MapManager::~MapManager() {
    m_workerRunning = false;
    if (m_workerThread.joinable()) m_workerThread.join();
    m_heatmapPending = false;
    if (m_heatmapWorker.joinable()) m_heatmapWorker.join();
    curl_global_cleanup();
}

void MapManager::initGL() {
    // Ничего не делаем, текстуры создаются на лету
}

// ----------------------------------------------------------------------------
// Тайлы OSM
// ----------------------------------------------------------------------------
void MapManager::requestTile(int z, int x, int y) {
    std::string key = std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
    {
        std::lock_guard<std::mutex> lock(m_requestedMutex);
        if (m_requestedTiles.find(key) != m_requestedTiles.end()) return;
        m_requestedTiles.insert(key);
    }
    std::lock_guard<std::mutex> lock(m_queueMutex);
    m_requestQueue.push({z, x, y});
}

Tile* MapManager::getTile(int z, int x, int y) {
    std::string key = std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y);
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    auto it = m_tileCache.find(key);
    if (it != m_tileCache.end() && it->second.loaded) {
        return &it->second;
    }
    return nullptr;
}

std::string MapManager::getTilePath(int z, int x, int y) {
    return TILE_CACHE_DIR + "/" + std::to_string(z) + "/" + std::to_string(x) + "/" + std::to_string(y) + ".png";
}

bool MapManager::loadTileFromFile(Tile& tile) {
    std::string path = getTilePath(tile.z, tile.x, tile.y);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::vector<uint8_t> pngData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    int w, h, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* ptr = stbi_load_from_memory(pngData.data(), pngData.size(), &w, &h, &channels, STBI_rgb_alpha);
    if (!ptr) return false;
    tile.width = w; tile.height = h;
    tile.rgbaData.assign(ptr, ptr + w*h*4);
    stbi_image_free(ptr);
    tile.loaded = true;
    return true;
}

bool MapManager::saveTileToFile(const Tile& tile) {
    std::string path = getTilePath(tile.z, tile.x, tile.y);
    std::string dir = path.substr(0, path.find_last_of('/'));
    fs::create_directories(dir);
    return stbi_write_png(path.c_str(), tile.width, tile.height, 4,
                          tile.rgbaData.data(), tile.width*4) != 0;
}

size_t MapManager::curlWriteCallback(void* data, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    auto* vec = static_cast<std::vector<uint8_t>*>(userp);
    vec->insert(vec->end(), (uint8_t*)data, (uint8_t*)data + realsize);
    return realsize;
}

bool MapManager::downloadTile(Tile& tile) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    std::string url = "https://tile.openstreetmap.org/" + std::to_string(tile.z) + "/" +
                      std::to_string(tile.x) + "/" + std::to_string(tile.y) + ".png";
    std::vector<uint8_t> pngData;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pngData);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MyMapApp/1.0 (taya@voinov.info)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code == 418) {
        std::cerr << "HTTP 418 for tile " << url << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        return false;
    }

    curl_easy_cleanup(curl);
    if (res != CURLE_OK || pngData.empty()) return false;
    int w, h, channels;
    stbi_set_flip_vertically_on_load(false);
    unsigned char* ptr = stbi_load_from_memory(pngData.data(), pngData.size(),
                                                &w, &h, &channels, STBI_rgb_alpha);
    if (!ptr) return false;
    tile.width = w; tile.height = h;
    tile.rgbaData.assign(ptr, ptr + w*h*4);
    stbi_image_free(ptr);
    tile.loaded = true;
    saveTileToFile(tile);
    return true;
}

void MapManager::workerThreadFunc() {
    while (m_workerRunning) {
        TileRequest req;
        {
            std::lock_guard<std::mutex> qlock(m_queueMutex);
            if (m_requestQueue.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            req = m_requestQueue.front();
            m_requestQueue.pop();
        }

        std::string key = std::to_string(req.z) + "/" + std::to_string(req.x) + "/" + std::to_string(req.y);

        {
            std::lock_guard<std::mutex> clock(m_cacheMutex);
            auto it = m_tileCache.find(key);
            if (it != m_tileCache.end() && it->second.loaded) {
                continue;
            }
        }

        // Добавьте задержку перед запросом (200 мс)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        Tile newTile;
        newTile.z = req.z; newTile.x = req.x; newTile.y = req.y;
        if (loadTileFromFile(newTile) || downloadTile(newTile)) {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            m_tileCache[key] = std::move(newTile);
            // удалить из запрошенных
            std::lock_guard<std::mutex> lockReq(m_requestedMutex);
            m_requestedTiles.erase(key);
        }
    }
}

void MapManager::clearCache() {
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_tileCache.clear();
    std::lock_guard<std::mutex> qlock(m_queueMutex);
    while (!m_requestQueue.empty()) m_requestQueue.pop();
}

// ----------------------------------------------------------------------------
// Координатные преобразования (тайлы, градусы)
// ----------------------------------------------------------------------------
double MapManager::lonToTileX(double lon, int z) {
    return (lon + 180.0) / 360.0 * (1 << z);
}
double MapManager::latToTileY(double lat, int z) {
    double latRad = lat * DEG_TO_RAD;
    return (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / PI) / 2.0 * (1 << z);
}
double MapManager::tileXToLon(double tx, int z) {
    return tx / (1 << z) * 360.0 - 180.0;
}
double MapManager::tileYToLat(double ty, int z) {
    double n = PI - 2.0 * PI * ty / (1 << z);
    return std::atan(std::sinh(n)) * 180.0 / PI;
}

// ----------------------------------------------------------------------------
// Преобразование метров в пиксели (для тепловой карты)
// ----------------------------------------------------------------------------
double MapManager::metersToPixels(double meters, double centerLat, int zoom, int winH) {
    double metersPerPixel = 156543.03392 * std::cos(centerLat * DEG_TO_RAD) / (1 << zoom);
    return meters / metersPerPixel;
}

// ----------------------------------------------------------------------------
// Преобразование значения RSRP в цвет
// ----------------------------------------------------------------------------
void MapManager::getColorForValue(float value, uint8_t& r, uint8_t& g, uint8_t& b) {
    if (value > -80)      { r=255; g=0;   b=0;   }
    else if (value > -90) { r=255; g=165; b=0;   }
    else if (value > -100){ r=255; g=255; b=0;   }
    else if (value > -110){ r=0;   g=255; b=0;   }
    else                  { r=0;   g=0;   b=128; }
}

double MapManager::haversineDistance(double lat1, double lon1, double lat2, double lon2) const {
    double dlat = (lat2 - lat1) * DEG_TO_RAD;
    double dlon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dlat/2)*sin(dlat/2) + cos(lat1*DEG_TO_RAD)*cos(lat2*DEG_TO_RAD)*sin(dlon/2)*sin(dlon/2);
    return 6371000.0 * 2 * atan2(sqrt(a), sqrt(1-a));
}

// ----------------------------------------------------------------------------
// Синхронное создание текстуры тепловой карты (круги)
// ----------------------------------------------------------------------------
void MapManager::generateHeatmapTexture(const std::vector<MapPoint>& points,
                                        double centerLat, double centerLon, int zoom,
                                        int winW, int winH,
                                        float radiusPixels, float radiusMeters,
                                        int criterion) {
    // Создаём пустую текстуру (прозрачный фон)
    std::vector<uint8_t> pixels(winW * winH * 4, 0);

    double lonMin = centerLon - 180.0 / (1 << zoom);
    double lonMax = centerLon + 180.0 / (1 << zoom);
    double latMin = centerLat - 90.0 / (1 << zoom);
    double latMax = centerLat + 90.0 / (1 << zoom);

    // Фильтруем точки, попадающие в расширенную область (с запасом radiusMeters)
    double marginLat = radiusMeters / 111319.0; // 1 градус ~111 км
    double marginLon = marginLat / std::cos(centerLat * DEG_TO_RAD);
    double latMinExt = latMin - marginLat;
    double latMaxExt = latMax + marginLat;
    double lonMinExt = lonMin - marginLon;
    double lonMaxExt = lonMax + marginLon;
    std::vector<MapPoint> relevantPoints;
    for (const auto& p : points) {
        if (p.lat >= latMinExt && p.lat <= latMaxExt && p.lon >= lonMinExt && p.lon <= lonMaxExt) {
            relevantPoints.push_back(p);
        }
    }
    if (relevantPoints.empty()) {
        // Пустая текстура
        if (m_heatmapTexture != 0) glDeleteTextures(1, &m_heatmapTexture);
        glGenTextures(1, &m_heatmapTexture);
        glBindTexture(GL_TEXTURE_2D, m_heatmapTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, winW, winH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        return;
    }

    // IDW для каждого пикселя
    for (int y = 0; y < winH; ++y) {
        double lat = latMax - (double)y / winH * (latMax - latMin);
        for (int x = 0; x < winW; ++x) {
            double lon = lonMin + (double)x / winW * (lonMax - lonMin);
            double sumW = 0.0, sumWV = 0.0;
            for (const auto& p : relevantPoints) {
                double d = haversineDistance(lat, lon, p.lat, p.lon);
                if (d > radiusMeters) continue;

                // Выбираем значение в зависимости от критерия
                float val;
                switch (criterion) {
                    case 0: val = p.rsrp; break;    // RSRP
                    case 1: val = p.rsrq; break;    // RSRQ
                    case 2: val = p.rssi; break;    // RSSI
                    case 3: val = p.altitude; break; // Altitude
                    default: val = p.rsrp; break;
                }

                if (d < 1e-3) {
                    sumW = 1.0; sumWV = val; break;
                }
                double w = 1.0 / (d * d);
                sumW += w;
                sumWV += w * val;
            }
            float value = (sumW > 0) ? (float)(sumWV / sumW) : -140.0f;
            uint8_t r, g, b;
            getColorForValue(value, r, g, b);
            int idx = (y * winW + x) * 4;
            pixels[idx+0] = r;
            pixels[idx+1] = g;
            pixels[idx+2] = b;
            pixels[idx+3] = 200;
        }
    }

    // Удаляем старую текстуру
    if (m_heatmapTexture != 0) {
        glDeleteTextures(1, &m_heatmapTexture);
        m_heatmapTexture = 0;
    }
    // Создаём новую текстуру
    glGenTextures(1, &m_heatmapTexture);
    glBindTexture(GL_TEXTURE_2D, m_heatmapTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, winW, winH, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    m_heatmapTexW = winW;
    m_heatmapTexH = winH;

    // Сохраняем в PNG в отдельную папку
    std::string dir = "heatmap_cache";
    fs::create_directories(dir);
    std::string path = dir + "/heatmap_zoom_" + std::to_string(zoom) + ".png";
    stbi_write_png(path.c_str(), winW, winH, 4, pixels.data(), winW*4);
    std::cout << "Saving heatmap to " << path << std::endl;
}

// ----------------------------------------------------------------------------
// Основная отрисовка карты (тайлы, тепловая карта, точки)
// ----------------------------------------------------------------------------
void MapManager::renderMap(int winW, int winH,
                           double centerLat, double centerLon, int zoom,
                           const std::vector<MapPoint>& currentPoints,
                           const std::vector<MapPoint>& aggregatedPoints,
                           bool showHeatmap, float heatmapRadiusPixels, float heatmapRadiusMeters,
                           int criterion, int earfcnFilter) {
    const double lonMin = centerLon - 180.0 / (1 << zoom);
    const double lonMax = centerLon + 180.0 / (1 << zoom);
    const double latMin = centerLat - 90.0 / (1 << zoom);
    const double latMax = centerLat + 90.0 / (1 << zoom);

    // Вычисляем видимые тайлы OSM
    double centerTileX = lonToTileX(centerLon, zoom);
    double centerTileY = latToTileY(centerLat, zoom);
    int tilesWide = std::ceil(winW / 256.0) + 2;
    int tilesHigh = std::ceil(winH / 256.0) + 2;
    int startTileX = (int)centerTileX - tilesWide/2;
    int startTileY = (int)centerTileY - tilesHigh/2;
    int maxTile = (1 << zoom) - 1;

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2 winPos = ImGui::GetCursorScreenPos();

    // ---- 1. Рисуем тайлы OSM ----
    for (int dy = 0; dy <= tilesHigh; ++dy) {
        int tileY = startTileY + dy;
        if (tileY < 0 || tileY > maxTile) continue;
        double tileTopLat = tileYToLat(tileY, zoom);
        double tileBottomLat = tileYToLat(tileY+1, zoom);
        for (int dx = 0; dx <= tilesWide; ++dx) {
            int tileX = startTileX + dx;
            if (tileX < 0 || tileX > maxTile) continue;
            double tileLeftLon = tileXToLon(tileX, zoom);
            double tileRightLon = tileXToLon(tileX+1, zoom);

            double pxLeft = (tileLeftLon - lonMin) / (lonMax - lonMin) * winW;
            double pxRight = (tileRightLon - lonMin) / (lonMax - lonMin) * winW;
            double pyTop = (latMax - tileTopLat) / (latMax - latMin) * winH;
            double pyBottom = (latMax - tileBottomLat) / (latMax - latMin) * winH;

            Tile* tile = getTile(zoom, tileX, tileY);
            if (tile && tile->loaded && !tile->rgbaData.empty()) {
                if (tile->texture == 0) {
                    glGenTextures(1, &tile->texture);
                    glBindTexture(GL_TEXTURE_2D, tile->texture);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tile->width, tile->height, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, tile->rgbaData.data());
                }
                draw->AddImage((void*)(intptr_t)tile->texture,
                               ImVec2(winPos.x + (float)pxLeft, winPos.y + (float)pyTop),
                               ImVec2(winPos.x + (float)pxRight, winPos.y + (float)pyBottom));
            } else {
                requestTile(zoom, tileX, tileY);
            }
        }
    }

    // ---- 2. Тепловая карта (если включена) ----
    if (showHeatmap) {
        std::vector<MapPoint> allPoints;
        for (const auto& p : currentPoints) {
            if (earfcnFilter == 0 || p.earfcn == earfcnFilter)
                allPoints.push_back(p);
        }
        for (const auto& p : aggregatedPoints) {
            if (earfcnFilter == 0 || p.earfcn == earfcnFilter)
                allPoints.push_back(p);
        }
        requestHeatmapUpdate(centerLat, centerLon, zoom, winW, winH, heatmapRadiusMeters, criterion, allPoints);
    } else {
        // Если тепловая карта выключена, удаляем текстуру
        if (m_heatmapTexture != 0) {
            glDeleteTextures(1, &m_heatmapTexture);
            m_heatmapTexture = 0;
        }
    }

    // отрисовка: использовать кэшированную текстуру
    if (m_heatmapReady) {
        std::lock_guard<std::mutex> lock(m_heatmapMutex);
        if (m_heatmapTexture == 0) glGenTextures(1, &m_heatmapTexture);
        glBindTexture(GL_TEXTURE_2D, m_heatmapTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_lastHeatmapW, m_lastHeatmapH, 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, m_lastHeatmapPixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        draw->AddImage((void*)(intptr_t)m_heatmapTexture, winPos, ImVec2(winPos.x+winW, winPos.y+winH));
    }

    // ---- 3. Рисуем обычные точки (поверх тепловой карты) ----
    auto drawPoints = [&](const std::vector<MapPoint>& pts, ImU32 color, float radius) {
        for (const auto& p : pts) {
            double px = (p.lon - lonMin) / (lonMax - lonMin) * winW;
            double py = (latMax - p.lat) / (latMax - latMin) * winH;
            if (px >= 0 && px <= winW && py >= 0 && py <= winH) {
                draw->AddCircleFilled(ImVec2(winPos.x + (float)px, winPos.y + (float)py), radius, color);
            }
        }
    };
    drawPoints(currentPoints, IM_COL32(255,0,0,255), 5.0f);
    drawPoints(aggregatedPoints, IM_COL32(0,255,0,255), 3.0f);
}

// Проверяет, изменились ли параметры, и если да – ставит задачу в очередь.
void MapManager::requestHeatmapUpdate(double centerLat, double centerLon, int zoom,
                                      int winW, int winH, float radiusMeters, int criterion,
                                      const std::vector<MapPoint>& allPoints) {
    // быстрое вычисление хэша точек (просто размер + несколько первых значений)
    size_t hash = allPoints.size();
    if (!allPoints.empty()) {
        hash ^= std::hash<double>{}(allPoints[0].lat) ^ std::hash<double>{}(allPoints[0].lon);
    }
    bool changed = (centerLat != m_lastCenterLat) ||
                   (centerLon != m_lastCenterLon) ||
                   (zoom != m_lastZoom) ||
                   (winW != m_lastWinW) || (winH != m_lastWinH) ||
                   (radiusMeters != m_lastRadiusMeters) ||
                   (criterion != m_lastCriterion) ||
                   (hash != m_lastPointsHash);
    if (!changed) return;
    
    // сохраняем параметры и точки для фонового потока
    {
        std::lock_guard<std::mutex> lock(m_heatmapMutex);
        m_lastCenterLat = centerLat; m_lastCenterLon = centerLon;
        m_lastZoom = zoom; m_lastWinW = winW; m_lastWinH = winH;
        m_lastRadiusMeters = radiusMeters; m_lastCriterion = criterion;
        m_lastPointsHash = hash;
        m_pendingPoints = allPoints;  // нужно добавить поле m_pendingPoints
        m_heatmapPending = true;
        m_heatmapReady = false;
    }
}


void MapManager::heatmapWorkerFunc() {
    while (m_workerRunning) {
        if (!m_heatmapPending) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        // копируем данные
        double centerLat, centerLon; int zoom, winW, winH; float radiusMeters; int criterion;
        std::vector<MapPoint> points;
        {
            std::lock_guard<std::mutex> lock(m_heatmapMutex);
            centerLat = m_lastCenterLat; centerLon = m_lastCenterLon;
            zoom = m_lastZoom; winW = m_lastWinW; winH = m_lastWinH;
            radiusMeters = m_lastRadiusMeters; criterion = m_lastCriterion;
            points = m_pendingPoints;
            m_heatmapPending = false;
        }
        // генерация текстуры (используем существующую generateHeatmapTexture без создания OpenGL-текстуры)
        std::vector<uint8_t> pixels = computeHeatmapPixels(points, centerLat, centerLon, zoom, winW, winH, radiusMeters, criterion);
        if (!pixels.empty()) {
            // сохраняем в кэш
            {
                std::lock_guard<std::mutex> lock(m_heatmapMutex);
                m_lastHeatmapPixels.swap(pixels);
                m_lastHeatmapW = winW; m_lastHeatmapH = winH;
                m_heatmapReady = true;
            }
            // сохраняем PNG (не каждый кадр, а только после генерации)
            std::string dir = "heatmap_cache";
            fs::create_directories(dir);
            std::string path = dir + "/heatmap_zoom_" + std::to_string(zoom) + ".png";
            stbi_write_png(path.c_str(), winW, winH, 4, m_lastHeatmapPixels.data(), winW*4);
        }
    }
}

std::vector<uint8_t> MapManager::computeHeatmapPixels(const std::vector<MapPoint>& points,
                                                      double centerLat, double centerLon, int zoom,
                                                      int winW, int winH,
                                                      float radiusMeters, int criterion) {
    std::vector<uint8_t> pixels(winW * winH * 4, 0);
    double lonMin = centerLon - 180.0 / (1 << zoom);
    double lonMax = centerLon + 180.0 / (1 << zoom);
    double latMin = centerLat - 90.0 / (1 << zoom);
    double latMax = centerLat + 90.0 / (1 << zoom);

    // Фильтруем точки
    double marginLat = radiusMeters / 111319.0;
    double marginLon = marginLat / std::cos(centerLat * DEG_TO_RAD);
    double latMinExt = latMin - marginLat;
    double latMaxExt = latMax + marginLat;
    double lonMinExt = lonMin - marginLon;
    double lonMaxExt = lonMax + marginLon;
    std::vector<MapPoint> relevantPoints;
    for (const auto& p : points) {
        if (p.lat >= latMinExt && p.lat <= latMaxExt && p.lon >= lonMinExt && p.lon <= lonMaxExt) {
            relevantPoints.push_back(p);
        }
    }
    if (relevantPoints.empty()) {
        return pixels;  // пустая текстура
    }

    // IDW для каждого пикселя
    for (int y = 0; y < winH; ++y) {
        double lat = latMax - (double)y / winH * (latMax - latMin);
        for (int x = 0; x < winW; ++x) {
            double lon = lonMin + (double)x / winW * (lonMax - lonMin);
            double sumW = 0.0, sumWV = 0.0;
            for (const auto& p : relevantPoints) {
                double d = haversineDistance(lat, lon, p.lat, p.lon);
                if (d > radiusMeters) continue;

                // Выбор значения в зависимости от критерия
                float val;
                switch (criterion) {
                    case 0: val = p.rsrp; break;      // RSRP
                    case 1: val = p.rsrq; break;      // RSRQ
                    case 2: val = p.rssi; break;      // RSSI
                    case 3: val = p.altitude; break;  // Altitude
                    default: val = p.rsrp; break;
                }

                if (d < 1e-3) {
                    sumW = 1.0; sumWV = val; break;
                }
                double w = 1.0 / (d * d);
                sumW += w;
                sumWV += w * val;
            }
            float value = (sumW > 0) ? (float)(sumWV / sumW) : -140.0f;
            uint8_t r, g, b;
            getColorForValue(value, r, g, b);
            int idx = (y * winW + x) * 4;
            pixels[idx+0] = r;
            pixels[idx+1] = g;
            pixels[idx+2] = b;
            pixels[idx+3] = 200;
        }
    }
    return pixels;
}