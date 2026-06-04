#include "ServerCore.h"
#include "Config.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <chrono>
#include <ctime>

using json = nlohmann::json;

ServerCore::ServerCore() {}
ServerCore::~ServerCore() { stop(); }

bool ServerCore::start() {
    if (m_running) return false;
    if (!initDatabase()) {
        std::cerr << "Database init failed, continuing without DB" << std::endl;
    }
    // Создаём ZMQ сокет (REP)
    m_zmqContext = new zmq::context_t(1);
    zmq::socket_t* sock = new zmq::socket_t(*static_cast<zmq::context_t*>(m_zmqContext), ZMQ_REP);
    std::string addr = "tcp://" + ZMQ_HOST + ":" + std::to_string(ZMQ_PORT);
    sock->bind(addr);
    m_zmqSocket = sock; // сохраняем как void*, в потоке приведём обратно
    m_running = true;
    m_thread = std::make_unique<std::thread>(&ServerCore::serverLoop, this);
    return true;
    sock->set(zmq::sockopt::rcvtimeo, 1000);
}

void ServerCore::stop() {
    m_running = false;
    if (m_thread && m_thread->joinable()) m_thread->join();
    if (m_zmqSocket) {
        delete static_cast<zmq::socket_t*>(m_zmqSocket);
        m_zmqSocket = nullptr;
    }
    if (m_zmqContext) {
        delete static_cast<zmq::context_t*>(m_zmqContext);
        m_zmqContext = nullptr;
    }
    if (m_dbConn) {
        PQfinish(m_dbConn);
        m_dbConn = nullptr;
    }
}

void ServerCore::serverLoop() {
    zmq::socket_t* sock = static_cast<zmq::socket_t*>(m_zmqSocket);
    if (!sock) return;
    
    // Устанавливаем таймаут на приём (1000 мс), чтобы поток мог проверять m_running
    sock->set(zmq::sockopt::rcvtimeo, 1000);
    
    while (m_running) {
        zmq::message_t request;
        try {
            auto res = sock->recv(request, zmq::recv_flags::none);
            if (!res) {
                // Таймаут или ошибка – просто проверим m_running и продолжим
                continue;
            }
            std::cout << "DEBUG: Received " << request.size() << " bytes" << std::endl;
            std::string jsonStr(static_cast<char*>(request.data()), request.size());
            // Парсим JSON в Measurement
            Measurement m = parseJson(jsonStr);
            m.receivedTime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            // Сохраняем в память
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_history.push_back(m);
                if (m_history.size() > MAX_HISTORY_SIZE)
                    m_history.erase(m_history.begin());
                m_lastMeasurement = m;
                m_hasNewData = true;
            }
            // В БД
            if (m_dbConn) saveToDatabase(m);
            // В JSON файл (бэкап)
            saveToJsonFile(m);

            // Отправляем ответ
            std::string reply = "OK";
            sock->send(zmq::buffer(reply), zmq::send_flags::none);
        } catch (const zmq::error_t& e) {
            // Игнорируем таймаут (EAGAIN), иначе выводим ошибку
            if (e.num() != EAGAIN) {
                std::cerr << "ZMQ error: " << e.what() << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Server loop error: " << e.what() << std::endl;
        }
    }
}

Measurement ServerCore::parseJson(const std::string& jsonStr) {
    Measurement m;
    try {
        json j = json::parse(jsonStr);
        if (j.contains("location")) {
            auto& loc = j["location"];
            m.location.latitude = loc.value("latitude", 0.0);
            m.location.longitude = loc.value("longitude", 0.0);
            m.location.altitude = loc.value("altitude", 0.0);
            m.location.timestamp = loc.value("timestamp", 0LL);
            m.location.speed = loc.value("speed", 0.0f);
            m.location.accuracy = loc.value("accuracy", 0.0f);
        }
        if (j.contains("lteCells") && j["lteCells"].is_array()) {
            for (auto& cell : j["lteCells"]) {
                LteCellDto lte;
                lte.band = cell.value("band", 0);
                lte.earfcn = cell.value("earfcn", 0);
                lte.mcc = cell.value("mcc", 0);
                lte.mnc = cell.value("mnc", 0);
                lte.pci = cell.value("pci", 0);
                lte.tac = cell.value("tac", 0);
                lte.rsrp = cell.value("rsrp", -140);
                lte.rsrq = cell.value("rsrq", -20);
                lte.rssnr = cell.value("rssnr", 0);
                lte.cqi = cell.value("cqi", 0);
                lte.timingAdvance = cell.value("timingAdvance", 0);
                m.lteCells.push_back(lte);
            }
        }
        // Аналогично для gsmCells, nrCells, traffic ...
        m.deviceId = j.value("deviceId", "unknown");
    } catch (...) {}
    return m;
}

bool ServerCore::initDatabase() {
    std::string connInfo = "host=" + DB_HOST + " port=" + DB_PORT +
                           " dbname=" + DB_NAME + " user=" + DB_USER +
                           " password=" + DB_PASS;
    m_dbConn = PQconnectdb(connInfo.c_str());
    if (PQstatus(m_dbConn) != CONNECTION_OK) {
        std::cerr << "DB connection failed: " << PQerrorMessage(m_dbConn) << std::endl;
        return false;
    }
    return createTable();
}

bool ServerCore::createTable() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS measurements (
            id SERIAL PRIMARY KEY,
            timestamp BIGINT,
            latitude DOUBLE PRECISION,
            longitude DOUBLE PRECISION,
            altitude DOUBLE PRECISION,
            accuracy REAL,
            speed REAL,
            device_id TEXT,
            lte_pci INT,
            lte_rsrp INT,
            lte_rsrq INT,
            lte_rssnr INT,
            lte_earfcn INT,
            lte_tac INT,
            received_time BIGINT
        )
    )";
    PGresult* res = PQexec(m_dbConn, sql);
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);
    if (!ok) std::cerr << "Failed to create table: " << PQerrorMessage(m_dbConn) << std::endl;
    return ok;
}

bool ServerCore::saveToDatabase(const Measurement& m) {
    if (!m_dbConn) return false;

    // Экранирование device_id
    char deviceIdEsc[256];
    int error = 0;
    PQescapeStringConn(m_dbConn, deviceIdEsc, m.deviceId.c_str(), m.deviceId.size(), &error);
    if (error) {
        std::cerr << "Escape error: " << error << std::endl;
        return false;
    }

    // Числовые поля
    char ts[32], lat[32], lon[32], alt[32], acc[32], spd[32], recv[32];
    snprintf(ts, sizeof(ts), "%lld", (long long)m.location.timestamp);
    snprintf(lat, sizeof(lat), "%f", m.location.latitude);
    snprintf(lon, sizeof(lon), "%f", m.location.longitude);
    snprintf(alt, sizeof(alt), "%f", m.location.altitude);
    snprintf(acc, sizeof(acc), "%f", m.location.accuracy);
    snprintf(spd, sizeof(spd), "%f", m.location.speed);
    snprintf(recv, sizeof(recv), "%lld", (long long)m.receivedTime);

    const LteCellDto* lte = m.lteCells.empty() ? nullptr : &m.lteCells[0];
    char pci_buf[16] = "0", rsrp_buf[16] = "0", rsrq_buf[16] = "0";
    char rssnr_buf[16] = "0", earfcn_buf[16] = "0", tac_buf[16] = "0";
    if (lte) {
        if (lte->pci != -1) snprintf(pci_buf, sizeof(pci_buf), "%d", lte->pci);
        if (lte->rsrp != -140) snprintf(rsrp_buf, sizeof(rsrp_buf), "%d", lte->rsrp);
        if (lte->rsrq != -20) snprintf(rsrq_buf, sizeof(rsrq_buf), "%d", lte->rsrq);
        if (lte->rssnr != 0) snprintf(rssnr_buf, sizeof(rssnr_buf), "%d", lte->rssnr);
        if (lte->earfcn != 0) snprintf(earfcn_buf, sizeof(earfcn_buf), "%d", lte->earfcn);
        if (lte->tac != 0) snprintf(tac_buf, sizeof(tac_buf), "%d", lte->tac);
    }

    const char* paramValues[14] = {
        ts, lat, lon, alt, acc, spd, deviceIdEsc,
        pci_buf, rsrp_buf, rsrq_buf, rssnr_buf, earfcn_buf, tac_buf, recv
    };
    const char* sql = "INSERT INTO measurements "
        "(timestamp, latitude, longitude, altitude, accuracy, speed, device_id, "
        "lte_pci, lte_rsrp, lte_rsrq, lte_rssnr, lte_earfcn, lte_tac, received_time) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, $13, $14)";

    PGresult* res = PQexecParams(m_dbConn, sql, 14, NULL, paramValues, NULL, NULL, 0);
    bool ok = (PQresultStatus(res) == PGRES_COMMAND_OK);
    if (!ok) {
        std::cerr << "DB insert error: " << PQerrorMessage(m_dbConn) << std::endl;
    }
    PQclear(res);
    return ok;
}

void ServerCore::saveToJsonFile(const Measurement& m) {
    std::ofstream file(JSON_LOG_FILE, std::ios::app);
    if (file.is_open()) {
        json j = {
            {"timestamp", m.location.timestamp},
            {"lat", m.location.latitude},
            {"lon", m.location.longitude},
            {"rsrp", m.lteCells.empty() ? 0 : m.lteCells[0].rsrp}
        };
        file << j.dump() << std::endl;
    }
}

std::vector<AggregatedPoint> ServerCore::loadAggregatedPoints() {
    std::vector<AggregatedPoint> result;
    if (!m_dbConn) return result;

    const char* sql = "SELECT latitude, longitude, lte_rsrp FROM measurements WHERE latitude IS NOT NULL AND lte_rsrp IS NOT NULL ORDER BY timestamp";
    PGresult* res = PQexec(m_dbConn, sql);
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::cerr << "loadAggregatedPoints query failed: " << PQerrorMessage(m_dbConn) << std::endl;
        PQclear(res);
        return result;
    }

    int rows = PQntuples(res);
    // Берём каждую AGGREGATION_STEP-ю запись (по умолчанию 10)
    for (int i = 0; i < rows; i += AGGREGATION_STEP) {
        AggregatedPoint p;
        p.lat = atof(PQgetvalue(res, i, 0));
        p.lon = atof(PQgetvalue(res, i, 1));
        p.rsrp = (float)atoi(PQgetvalue(res, i, 2));
        p.count = 1;
        result.push_back(p);
        if (result.size() >= MAX_AGGREGATED_POINTS) break;
    }
    PQclear(res);
    return result;
}

Measurement ServerCore::getLastMeasurement() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastMeasurement;
}

std::vector<Measurement> ServerCore::getHistory() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_history;
}

bool ServerCore::hasNewData() {
    return m_hasNewData.exchange(false);
}