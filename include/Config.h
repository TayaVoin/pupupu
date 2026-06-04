#ifndef CONFIG_H
#define CONFIG_H

#include <string>

// ---------- ZMQ ----------
const std::string ZMQ_HOST = "*";
const int ZMQ_PORT = 5000;

// ---------- PostgreSQL ----------
const std::string DB_HOST = "localhost";
const std::string DB_PORT = "5432";
const std::string DB_NAME = "cell_data";
const std::string DB_USER = "visual";
const std::string DB_PASS = "visualprog";

// ---------- Пути к файлам ----------
const std::string TILE_CACHE_DIR = "tile_cache";         // OSM тайлы
const std::string HEATMAP_PNG_FILE = "build/heatmap.png"; // тепловая карта (одна общая)
const std::string JSON_LOG_FILE = "measurements.json";    // резервное логирование

// ---------- Параметры тепловой карты (IDW) ----------
const float IDW_SEARCH_RADIUS_METERS = 40.0f;   // 10..40 метров по заданию
const float IDW_POWER = 2.0f;
const int HEATMAP_WIDTH = 600;                  // размер окна карты (фиксированный)
const int HEATMAP_HEIGHT = 600;

// ---------- Лимиты и агрегация ----------
const int MAX_HISTORY_SIZE = 100000;            // точек в памяти
const int AGGREGATION_STEP = 10;                // каждую 10-ю точку из БД
const int MAX_AGGREGATED_POINTS = 5000;

#endif