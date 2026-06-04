#ifndef SERVER_CORE_H
#define SERVER_CORE_H

#include <memory>
#include <atomic>
#include <thread>
#include <mutex>
#include <vector>
#include <libpq-fe.h>
#include "DataModels.h"

class ServerCore {
public:
    ServerCore();
    ~ServerCore();

    // Запуск ZMQ сервера (REP) и инициализация БД
    bool start();
    void stop();

    // Получить последнее измерение и историю (для GUI)
    Measurement getLastMeasurement() const;
    std::vector<Measurement> getHistory() const;
    bool hasNewData();

    // Загрузить агрегированные точки из БД (каждые AGGREGATION_STEP записей)
    std::vector<AggregatedPoint> loadAggregatedPoints();

private:
    void serverLoop();
    bool initDatabase();
    bool createTable();
    bool saveToDatabase(const Measurement& m);
    void saveToJsonFile(const Measurement& m);
    Measurement parseJson(const std::string& jsonStr);

    // ZMQ
    std::atomic<bool> m_running{false};
    std::unique_ptr<std::thread> m_thread;
    void* m_zmqSocket = nullptr;   // void* чтобы не тянуть zmq.hpp в заголовок
    void* m_zmqContext = nullptr;

    // Данные
    mutable std::mutex m_mutex;
    Measurement m_lastMeasurement;
    std::vector<Measurement> m_history;
    std::atomic<bool> m_hasNewData{false};

    // PostgreSQL
    PGconn* m_dbConn = nullptr;
};

#endif