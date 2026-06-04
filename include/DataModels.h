#ifndef DATA_MODELS_H
#define DATA_MODELS_H

#include <string>
#include <vector>
#include <cstdint>

// -------- Модель данных от Android (соответствует JSON) --------
struct LocationDto {
    double latitude = 0.0;
    double longitude = 0.0;
    double altitude = 0.0;
    int64_t timestamp = 0;
    float speed = 0.0f;
    float accuracy = 0.0f;
};

struct LteCellDto {
    int band = 0;
    int earfcn = 0;
    int mcc = 0;
    int mnc = 0;
    int pci = 0;
    int tac = 0;
    int rsrp = -140;
    int rsrq = -20;
    int rssnr = 0;
    int cqi = 0;
    int timingAdvance = 0;
};

struct GsmCellDto {
    int cid = 0;
    int lac = 0;
    int mcc = 0;
    int mnc = 0;
    int bsic = 0;
    int arfcn = 0;
    int rssi = -120;
    int timingAdvance = 0;
};

struct NrCellDto {
    int64_t nci = 0;
    int nrarfcn = 0;
    int tac = 0;
    int mcc = 0;
    int mnc = 0;
    int ssRsrp = -140;
    int ssRsrq = -20;
    int ssSinr = 0;
};

struct TrafficDto {
    int64_t totalRxBytes = 0;
    int64_t totalTxBytes = 0;
    std::vector<std::pair<std::string, int64_t>> topApps;
};

struct Measurement {
    LocationDto location;
    std::vector<LteCellDto> lteCells;
    std::vector<GsmCellDto> gsmCells;
    std::vector<NrCellDto> nrCells;
    TrafficDto traffic;
    std::string deviceId = "unknown";
    int64_t receivedTime = 0;          // время получения сервером (ms)
};

// -------- Точка для отображения на карте и для IDW --------
struct MapPoint {
    double lat = 0.0;
    double lon = 0.0;
    float value = 0.0f;     // RSRP, RSRQ, RSSI или Altitude
    int pci = 0;
    bool isCurrent = false;  // true – только что полученное, false – из БД (агрегированное)
};

// -------- Агрегированная точка из БД --------
struct AggregatedPoint {
    double lat = 0.0;
    double lon = 0.0;
    float rsrp = 0.0f;
    int count = 0;
};

#endif