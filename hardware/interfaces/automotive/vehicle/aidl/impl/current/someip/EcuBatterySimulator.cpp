#include <vsomeip/vsomeip.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include "ApvpProtocol.h"

#define BATTERY_SERVICE_ID    0x1234
#define BATTERY_INSTANCE_ID   0x5678
#define BATTERY_TEMP_EVENT    0x8001
#define BATTERY_EVENTGROUP    0x01
#define BATTERY_HEALTH_METHOD 0x0421
#define SET_FAN_SPEED_METHOD  0x0502

// Định nghĩa mã thuộc tính chuẩn AOSP & Vendor
#define PROP_PERF_VEHICLE_SPEED   0x11600207
#define PROP_ENGINE_RPM           0x11600305
#define PROP_GEAR_SELECTION       0x11400400
#define PROP_FUEL_LEVEL           0x11600505
#define PROP_HVAC_TEMPERATURE_SET 0x15600503
#define PROP_DOOR_POS             0x16400B00
#define PROP_NIGHT_MODE           0x11200407
#define PROP_VENDOR_BATTERY_TEMP  0x21600100

// Area ID theo quy chuẩn xe
#define AREA_GLOBAL               0x00000000
#define AREA_DOOR_ROW_1_LEFT      0x00000001 // Cửa trước bên lái
#define AREA_SEAT_ROW_1_LEFT      0x00000001 // Vùng điều hòa ghế lái

std::shared_ptr<vsomeip::application> app;

void sendVehicleVddmTelemetryLoop() {
    float currentSpeed = 0.0f;  // m/s
    float currentTemp  = 35.0f; // deg C
    float currentFuel  = 80.0f; // ml / %
    float hvacTemp     = 22.0f; // deg C
    int32_t currentGear = 4;    // 4 = GEAR_PARK
    int32_t doorPos    = 0;     // 0 = Cửa đóng
    int32_t nightMode  = 0;     // 0 = Ban ngày
    uint16_t seqCounter = 0;

    auto startTime = std::chrono::steady_clock::now();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // 1. Giả lập xe đang lăn bánh thực tế trên đường
        currentSpeed += 2.0f;
        if (currentSpeed > 25.0f) currentSpeed = 0.0f;

        if (currentSpeed > 0.0f) {
            currentGear = 8; // 8 = GEAR_DRIVE (Xe chạy)
            doorPos = 0;     // Đang chạy cửa phải đóng
        } else {
            currentGear = 4; // 4 = GEAR_PARK (Xe dừng)
        }

        float currentRpm = (currentGear == 8) ? (1000.0f + currentSpeed * 100.0f) : 800.0f;
        currentTemp += 1.0f;
        if (currentTemp > 60.0f) currentTemp = 35.0f;
        currentFuel -= 0.1f;
        if (currentFuel < 10.0f) currentFuel = 80.0f;

        // 2. Tính timestamp uptime của xe
        auto now = std::chrono::steady_clock::now();
        uint32_t uptimeMs = static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count());

        // 3. ĐÓNG GÓI CHUẨN APVP / VDDM FRAME:
        // A. Header (12 bytes)
        ApvpFrameHeader header;
        header.magic = VDDM_MAGIC_HEADER;
        header.seqCounter = seqCounter++;
        header.numOfProps = 8;
        header.timestamp = uptimeMs;

        // B. Danh sách 8 VDDM Property Entries (mỗi entry 16 bytes)
        std::vector<VddmPropertyEntry> entries(8);

        // [0] Tốc độ xe (m/s)
        entries[0].propId = PROP_PERF_VEHICLE_SPEED;
        entries[0].areaId = AREA_GLOBAL;
        entries[0].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[0].dataType = static_cast<uint8_t>(VddmDataType::FLOAT);
        entries[0].dataLen = 4;
        entries[0].value.floatVal = currentSpeed;

        // [1] Vòng tua máy (RPM)
        entries[1].propId = PROP_ENGINE_RPM;
        entries[1].areaId = AREA_GLOBAL;
        entries[1].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[1].dataType = static_cast<uint8_t>(VddmDataType::FLOAT);
        entries[1].dataLen = 4;
        entries[1].value.floatVal = currentRpm;

        // [2] Cần số (P/R/N/D)
        entries[2].propId = PROP_GEAR_SELECTION;
        entries[2].areaId = AREA_GLOBAL;
        entries[2].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[2].dataType = static_cast<uint8_t>(VddmDataType::INT32);
        entries[2].dataLen = 4;
        entries[2].value.intVal = currentGear;

        // [3] Mức nhiên liệu / Pin
        entries[3].propId = PROP_FUEL_LEVEL;
        entries[3].areaId = AREA_GLOBAL;
        entries[3].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[3].dataType = static_cast<uint8_t>(VddmDataType::FLOAT);
        entries[3].dataLen = 4;
        entries[3].value.floatVal = currentFuel;

        // [4] Nhiệt độ điều hòa ghế lái
        entries[4].propId = PROP_HVAC_TEMPERATURE_SET;
        entries[4].areaId = AREA_SEAT_ROW_1_LEFT;
        entries[4].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[4].dataType = static_cast<uint8_t>(VddmDataType::FLOAT);
        entries[4].dataLen = 4;
        entries[4].value.floatVal = hvacTemp;

        // [5] Cửa trước bên lái
        entries[5].propId = PROP_DOOR_POS;
        entries[5].areaId = AREA_DOOR_ROW_1_LEFT;
        entries[5].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[5].dataType = static_cast<uint8_t>(VddmDataType::INT32);
        entries[5].dataLen = 4;
        entries[5].value.intVal = doorPos;

        // [6] Chế độ ban đêm
        entries[6].propId = PROP_NIGHT_MODE;
        entries[6].areaId = AREA_GLOBAL;
        entries[6].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[6].dataType = static_cast<uint8_t>(VddmDataType::INT32);
        entries[6].dataLen = 4;
        entries[6].value.intVal = nightMode;

        // [7] Nhiệt độ pin (Vendor)
        entries[7].propId = PROP_VENDOR_BATTERY_TEMP;
        entries[7].areaId = AREA_GLOBAL;
        entries[7].status = static_cast<uint8_t>(VddmStatus::AVAILABLE);
        entries[7].dataType = static_cast<uint8_t>(VddmDataType::FLOAT);
        entries[7].dataLen = 4;
        entries[7].value.floatVal = currentTemp;

        // C. Ghép Header (12B) + Mảng Entries (128B) thành 140 bytes
        std::vector<uint8_t> frameBuffer(sizeof(ApvpFrameHeader) + entries.size() * sizeof(VddmPropertyEntry));
        std::memcpy(frameBuffer.data(), &header, sizeof(ApvpFrameHeader));
        std::memcpy(frameBuffer.data() + sizeof(ApvpFrameHeader), entries.data(), entries.size() * sizeof(VddmPropertyEntry));

        // D. Đóng gói vào vsomeip payload và gửi đi
        auto payload = vsomeip::runtime::get()->create_payload();
        payload->set_data(frameBuffer);

        std::cout << "[ECU VDDM FRAME #" << header.seqCounter << "] Sent 8 Props | Speed: " 
                  << (currentSpeed * 3.6f) << " km/h | Gear: " << (currentGear == 8 ? "D" : "P")
                  << " | Battery: " << currentTemp << " C | Total: " << frameBuffer.size() << " bytes" << std::endl;

        app->notify(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_TEMP_EVENT, payload);
    }
}

void onBatteryHealthRequest(const std::shared_ptr<vsomeip::message>& request) {
    std::cout << "\n[VEHICLE ECU] [REQUEST RECEIVED] VHAL requested Battery Health (Method: 0x0421)!" << std::endl;
    auto response = vsomeip::runtime::get()->create_response(request);

    float batteryHealth = 98.5f;
    auto payload = vsomeip::runtime::get()->create_payload();
    std::vector<vsomeip::byte_t> data(reinterpret_cast<uint8_t*>(&batteryHealth),
                                      reinterpret_cast<uint8_t*>(&batteryHealth) + sizeof(float));
    payload->set_data(data);
    response->set_payload(payload);
    app->send(response);
    std::cout << "[VEHICLE ECU] [RESPONSE SENT] Responded with Battery Health: " << batteryHealth << "%\n" << std::endl;
}

void onSetFanSpeedCommand(const std::shared_ptr<vsomeip::message>& request) {
    auto payload = request->get_payload();
    if (payload && payload->get_length() >= sizeof(int32_t)) {
        int32_t fanSpeed = *reinterpret_cast<const int32_t*>(payload->get_data());
        std::cout << "\n=========================================================" << std::endl;
        std::cout << " [ECU Car Turn on Fan] Android yeu cau toc do quat: " << fanSpeed << std::endl;
        std::cout << (fanSpeed == 0 ? " -> Trang thai: DA TAT QUAT LAM MAT PIN." 
                                    : " -> Trang thai: DANG CHAY QUAT CAP DO " + std::to_string(fanSpeed) + "/5.") << std::endl;
        std::cout << "=========================================================\n" << std::endl;
        auto response = vsomeip::runtime::get()->create_response(request);
        app->send(response);
    }
}

int main() {
    app = vsomeip::runtime::get()->create_application("EcuBatteryService");
    app->init();

    app->register_message_handler(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, SET_FAN_SPEED_METHOD, onSetFanSpeedCommand);
    app->register_message_handler(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_HEALTH_METHOD, onBatteryHealthRequest);

    std::set<vsomeip::eventgroup_t> groups = { BATTERY_EVENTGROUP };
    app->offer_event(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_TEMP_EVENT, groups);
    app->offer_service(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID);

    std::cout << "==================================================" << std::endl;
    std::cout << "  ECU APVP/VDDM TELEMETRY SERVER RUNNING          " << std::endl;
    std::cout << "  - Service ID: 0x1234, Instance ID: 0x5678       " << std::endl;
    std::cout << "  - Protocol: APVP / VDDM Frame (Header + Props)  " << std::endl;
    std::cout << "==================================================" << std::endl;

    std::thread senderThread(sendVehicleVddmTelemetryLoop);
    app->start();

    senderThread.join();
    return 0;
}