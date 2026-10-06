/*
 * Copyright (C) 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "SomeipVehicleHardware"

#include "SomeipVehicleHardware.h"

#include <VehicleHalTypes.h>
#include <VehicleUtils.h>
#include <utils/Log.h>
#include <utils/SystemClock.h>
#include <iostream>
#include "ApvpProtocol.h"

#define BATTERY_SERVICE_ID    0x1234
#define BATTERY_INSTANCE_ID   0x5678
#define BATTERY_TEMP_EVENT    0x8001
#define BATTERY_EVENTGROUP    0x01
#define BATTERY_HEALTH_METHOD 0x0421

#define VENDOR_BATTERY_COOLING_FAN_SPEED 0x21400101 // 557842689
#define SET_FAN_SPEED_METHOD             0x0502

namespace android::hardware::automotive::vehicle {

SomeipVehicleHardware::SomeipVehicleHardware() {
    ALOGI("Starting SomeipVehicleHardware initialization...");

    // Khoi tao gia tri mac dinh cho cac Property trong In-Memory Store
    int64_t now = elapsedRealtimeNano();
    {
        std::lock_guard<std::mutex> lock(mValueLock);

        // 1. Nhiet do pin (35.0 C)
        aidlvhal::VehiclePropValue battTemp;
        battTemp.prop = 0x21600100;
        battTemp.timestamp = now;
        battTemp.value.floatValues = { 35.0f };
        mServerLocalStore[battTemp.prop] = battTemp;

        // 2. Toc do quat pin (0 - Tat)
        aidlvhal::VehiclePropValue fanSpeed;
        fanSpeed.prop = VENDOR_BATTERY_COOLING_FAN_SPEED;
        fanSpeed.timestamp = now;
        fanSpeed.value.int32Values = { 0 };
        mServerLocalStore[fanSpeed.prop] = fanSpeed;

        // 3. Toc do xe (0.0 km/h)
        aidlvhal::VehiclePropValue speed;
        speed.prop = toInt(aidlvhal::VehicleProperty::PERF_VEHICLE_SPEED);
        speed.timestamp = now;
        speed.value.floatValues = { 0.0f };
        mServerLocalStore[speed.prop] = speed;

        // 4. Vi tri can so (4 = GEAR_PARK theo VehicleGear.aidl)
        aidlvhal::VehiclePropValue gear;
        gear.prop = toInt(aidlvhal::VehicleProperty::GEAR_SELECTION);
        gear.timestamp = now;
        gear.value.int32Values = { 4 }; // 0x0004 = GEAR_PARK
        mServerLocalStore[gear.prop] = gear;

        // 5. Vong tua dong co (0 RPM)
        aidlvhal::VehiclePropValue rpm;
        rpm.prop = toInt(aidlvhal::VehicleProperty::ENGINE_RPM);
        rpm.timestamp = now;
        rpm.value.floatValues = { 0.0f };
        mServerLocalStore[rpm.prop] = rpm;

        // 6. Muc nhien lieu / Pin (80.0%)
        aidlvhal::VehiclePropValue fuel;
        fuel.prop = toInt(aidlvhal::VehicleProperty::FUEL_LEVEL);
        fuel.timestamp = now;
        fuel.value.floatValues = { 80.0f };
        mServerLocalStore[fuel.prop] = fuel;

        // 7. Che do ban dem (0 = Ban ngay)
        aidlvhal::VehiclePropValue nightMode;
        nightMode.prop = toInt(aidlvhal::VehicleProperty::NIGHT_MODE);
        nightMode.timestamp = now;
        nightMode.value.int32Values = { 0 };
        mServerLocalStore[nightMode.prop] = nightMode;

        // 8. Nhiet do dieu hoa cai dat (22.0 C - HVAC_TEMPERATURE_SET: 0x15600503)
        aidlvhal::VehiclePropValue hvacTemp;
        hvacTemp.prop = toInt(aidlvhal::VehicleProperty::HVAC_TEMPERATURE_SET);
        hvacTemp.timestamp = now;
        hvacTemp.areaId = 0; // Global/Row 1
        hvacTemp.value.floatValues = { 22.0f };
        mServerLocalStore[hvacTemp.prop] = hvacTemp;

        // 9. Trang thai dong/mo cua xe (0 = Dong hoan toan - DOOR_POS: 0x16400B00)
        aidlvhal::VehiclePropValue doorPos;
        doorPos.prop = toInt(aidlvhal::VehicleProperty::DOOR_POS);
        doorPos.timestamp = now;
        doorPos.areaId = 0; // Global/Driver door
        doorPos.value.int32Values = { 0 };
        mServerLocalStore[doorPos.prop] = doorPos;
    }

    initSomeip();
}

SomeipVehicleHardware::~SomeipVehicleHardware() {
    ALOGI("Stopping SomeipVehicleHardware...");
    if (mApp) {
        mApp->stop();
    }
    if (mSomeipThread.joinable()) {
        mSomeipThread.join();
    }
}

void SomeipVehicleHardware::initSomeip() {
    mRtm = vsomeip::runtime::get();
    mApp = mRtm->create_application("AndroidVhalClient");

    if (!mApp->init()) {
        ALOGE("Failed to initialize vsomeip application!");
        return;
    }

    // 1. State Handler (when client registers with vsomeip runtime)
    mApp->register_state_handler(
        std::bind(&SomeipVehicleHardware::onSomeipStateChange, this, std::placeholders::_1));

    // 2. Availability Handler (track Service Discovery: ECU Online / Offline)
    mApp->register_availability_handler(
        BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID,
        std::bind(&SomeipVehicleHardware::onSomeipAvailability, this,
                  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // 3. Message Handler for Publish / Subscribe (Event 0x8001: Battery Temperature)
    mApp->register_message_handler(
        BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_TEMP_EVENT,
        std::bind(&SomeipVehicleHardware::onSomeipMessage, this, std::placeholders::_1));

    // 4. Message Handler for Request / Response (Method 0x0421: Battery Health response)
    mApp->register_message_handler(
        BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_HEALTH_METHOD,
        std::bind(&SomeipVehicleHardware::onBatteryHealthResponse, this, std::placeholders::_1));

    // Run vsomeip event loop in a dedicated background thread
    mSomeipThread = std::thread([this]() {
        ALOGI("vsomeip worker thread started.");
        mApp->start();
        ALOGI("vsomeip worker thread exited.");
    });
}

void SomeipVehicleHardware::onSomeipStateChange(vsomeip::state_type_e state) {
    if (state == vsomeip::state_type_e::ST_REGISTERED) {
        ALOGI("vsomeip client registered with runtime! Requesting Battery Service discovery...");
        mApp->request_service(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID);
    }
}

void SomeipVehicleHardware::onSomeipAvailability(vsomeip::service_t service,
                                                 vsomeip::instance_t instance,
                                                 bool isAvailable) {
    if (isAvailable) {
        ALOGI("Service [0x%04x.0x%04x] is AVAILABLE (ONLINE)!", service, instance);
        std::cout << "\n[VHAL Client] [AVAILABILITY] Detected ECU Service [0x"
                  << std::hex << service << "] is ONLINE!" << std::dec << std::endl;

        // Subscribe to event now that service is online
        mApp->request_event(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_TEMP_EVENT,
                            { BATTERY_EVENTGROUP }, vsomeip::event_type_e::ET_FIELD);
        mApp->subscribe(BATTERY_SERVICE_ID, BATTERY_INSTANCE_ID, BATTERY_EVENTGROUP);
        std::cout << "[VHAL Client] Subscribed to Battery Temperature event (Event ID: 0x8001)." << std::endl;

        // Fire an immediate Request for Battery Health (RPC Request/Response)
        requestBatteryHealth();
    } else {
        ALOGW("Service [0x%04x.0x%04x] is NOT AVAILABLE (OFFLINE)!", service, instance);
        std::cout << "\n[VHAL Client] [AVAILABILITY] WARNING: ECU Service [0x"
                  << std::hex << service << "] went OFFLINE!" << std::dec << std::endl;
    }
}

void SomeipVehicleHardware::requestBatteryHealth() {
    if (!mApp || !mRtm) return;

    ALOGI("Sending Request for Battery Health (Method ID: 0x%04x)...", BATTERY_HEALTH_METHOD);
    std::cout << "[VHAL Client] [REQUEST] Sending Request for Battery Health to ECU (Method ID: 0x0421)..." << std::endl;

    std::shared_ptr<vsomeip::message> request = mRtm->create_request();
    request->set_service(BATTERY_SERVICE_ID);
    request->set_instance(BATTERY_INSTANCE_ID);
    request->set_method(BATTERY_HEALTH_METHOD);

    mApp->send(request);
}

void SomeipVehicleHardware::onBatteryHealthResponse(const std::shared_ptr<vsomeip::message>& response) {
    if (!response) return;

    auto payload = response->get_payload();
    if (!payload || payload->get_length() < sizeof(float)) {
        ALOGW("Received Battery Health Response with invalid payload length");
        return;
    }

    float health = *reinterpret_cast<const float*>(payload->get_data());
    ALOGI("Received Battery Health Response from ECU: %.1f%%", health);
    std::cout << "[VHAL Client] [RESPONSE] <-- Received Battery Health from ECU: "
              << health << "% (Optimal condition)" << std::endl;
}

void SomeipVehicleHardware::onSomeipMessage(const std::shared_ptr<vsomeip::message>& msg) {
    if (!msg) return;

    auto payload = msg->get_payload();
    if (!payload || payload->get_length() < sizeof(float)) {
        ALOGW("Received SOME/IP message with invalid payload length");
        return;
    }

    size_t len = payload->get_length();
    const uint8_t* data = payload->get_data();
    int64_t now = elapsedRealtimeNano();
    std::vector<aidlvhal::VehiclePropValue> events;

    // TH1: Gói tin chuẩn doanh nghiệp APVP / VDDM Frame (Header 12 bytes + N Properties)
    if (len >= sizeof(ApvpFrameHeader)) {
        const auto* header = reinterpret_cast<const ApvpFrameHeader*>(data);
        if (header->magic == VDDM_MAGIC_HEADER) {
            size_t offset = sizeof(ApvpFrameHeader);
            std::lock_guard<std::mutex> lock(mValueLock);

            for (uint16_t i = 0; i < header->numOfProps; ++i) {
                // Bounds check an toàn tuyệt đối chống lỗi "Range is out of bounds"
                if (offset + sizeof(VddmPropertyEntry) > len) {
                    ALOGW("[APVP VDDM] Payload truncated, stopping parse at prop index %d", i);
                    break;
                }

                const auto* entry = reinterpret_cast<const VddmPropertyEntry*>(data + offset);
                aidlvhal::VehiclePropValue propValue;
                propValue.timestamp = now;
                propValue.areaId = entry->areaId;
                propValue.prop = entry->propId;

                if (entry->dataType == static_cast<uint8_t>(VddmDataType::FLOAT)) {
                    propValue.value.floatValues = { entry->value.floatVal };
                } else {
                    propValue.value.int32Values = { entry->value.intVal };
                }

                mServerLocalStore[propValue.prop] = propValue;
                events.push_back(propValue);
                offset += sizeof(VddmPropertyEntry);
            }

            // Dispatch tất cả properties lên Android CarService
            if (!events.empty()) {
                std::lock_guard<std::mutex> cbLock(mCallbackLock);
                if (mPropChangeCallback) {
                    (*mPropChangeCallback)(events);
                }
            }
            return;
        }
    }

    // TH2: Gói tin Compact (N * 8 bytes: [PropId | Value])
    if (len >= 8 && (len % 8 == 0)) {
        size_t offset = 0;
        std::lock_guard<std::mutex> lock(mValueLock);

        while (offset + 8 <= len) {
            uint32_t propId = *reinterpret_cast<const uint32_t*>(data + offset);
            aidlvhal::VehiclePropValue propValue;
            propValue.timestamp = now;
            propValue.areaId = 0;
            propValue.prop = propId;

            if (propId == toInt(aidlvhal::VehicleProperty::PERF_VEHICLE_SPEED) ||
                propId == toInt(aidlvhal::VehicleProperty::ENGINE_RPM) ||
                propId == toInt(aidlvhal::VehicleProperty::FUEL_LEVEL) ||
                propId == toInt(aidlvhal::VehicleProperty::HVAC_TEMPERATURE_SET) ||
                propId == 0x21600100 /* VENDOR_BATTERY_TEMP */) {
                float fVal = *reinterpret_cast<const float*>(data + offset + 4);
                propValue.value.floatValues = { fVal };
            } else {
                int32_t iVal = *reinterpret_cast<const int32_t*>(data + offset + 4);
                propValue.value.int32Values = { iVal };
            }

            mServerLocalStore[propId] = propValue;
            events.push_back(propValue);
            offset += 8;
        }
    } else {
        // TH3: Gói tin 4 byte đơn lẻ (Nhiệt độ pin)
        float batteryTemp = *reinterpret_cast<const float*>(data);
        aidlvhal::VehiclePropValue propValue;
        propValue.timestamp = now;
        propValue.areaId = 0;
        propValue.prop = 0x21600100;
        propValue.value.floatValues = { batteryTemp };

        {
            std::lock_guard<std::mutex> lock(mValueLock);
            mServerLocalStore[propValue.prop] = propValue;
        }
        events.push_back(propValue);
    }

    if (!events.empty()) {
        std::lock_guard<std::mutex> lock(mCallbackLock);
        if (mPropChangeCallback) {
            (*mPropChangeCallback)(events);
        }
    }
}


std::vector<aidlvhal::VehiclePropConfig> SomeipVehicleHardware::getAllPropertyConfigs() const {
    // 1. Thuoc tinh Nhiet do Pin (Vendor)
    aidlvhal::VehiclePropConfig customBatteryConfig;
    customBatteryConfig.prop = 0x21600100; // 559939840 - VENDOR_CUSTOM_BATTERY_TEMP
    customBatteryConfig.access = aidlvhal::VehiclePropertyAccess::READ_WRITE;
    customBatteryConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;
    
    // 2. Thuoc tinh Toc do Quat Pin (Vendor)
    aidlvhal::VehiclePropConfig fanSpeedConfig;
    fanSpeedConfig.prop = VENDOR_BATTERY_COOLING_FAN_SPEED;
    fanSpeedConfig.access = aidlvhal::VehiclePropertyAccess::READ_WRITE;
    fanSpeedConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;

    // 3. Toc do xe (Standard AOSP - PERF_VEHICLE_SPEED: 0x11600207)
    aidlvhal::VehiclePropConfig speedConfig;
    speedConfig.prop = toInt(aidlvhal::VehicleProperty::PERF_VEHICLE_SPEED);
    speedConfig.access = aidlvhal::VehiclePropertyAccess::READ;
    speedConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::CONTINUOUS;
    speedConfig.minSampleRate = 1.0f;
    speedConfig.maxSampleRate = 50.0f;

    // 4. Vi tri can so P, R, N, D (Standard AOSP - GEAR_SELECTION: 0x11400400)
    aidlvhal::VehiclePropConfig gearConfig;
    gearConfig.prop = toInt(aidlvhal::VehicleProperty::GEAR_SELECTION);
    gearConfig.access = aidlvhal::VehiclePropertyAccess::READ;
    gearConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;

    // 5. Vong tua dong co (Standard AOSP - ENGINE_RPM: 0x11600305)
    aidlvhal::VehiclePropConfig rpmConfig;
    rpmConfig.prop = toInt(aidlvhal::VehicleProperty::ENGINE_RPM);
    rpmConfig.access = aidlvhal::VehiclePropertyAccess::READ;
    rpmConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::CONTINUOUS;
    rpmConfig.minSampleRate = 1.0f;
    rpmConfig.maxSampleRate = 50.0f;

    // 6. Muc nhien lieu / Pin (Standard AOSP - FUEL_LEVEL: 0x11600505)
    aidlvhal::VehiclePropConfig fuelConfig;
    fuelConfig.prop = toInt(aidlvhal::VehicleProperty::FUEL_LEVEL);
    fuelConfig.access = aidlvhal::VehiclePropertyAccess::READ;
    fuelConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;

    // 7. Che do ban dem (Standard AOSP - NIGHT_MODE: 0x11200407)
    aidlvhal::VehiclePropConfig nightModeConfig;
    nightModeConfig.prop = toInt(aidlvhal::VehicleProperty::NIGHT_MODE);
    nightModeConfig.access = aidlvhal::VehiclePropertyAccess::READ;
    nightModeConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;

    // 8. Nhiet do dieu hoa (Standard AOSP - HVAC_TEMPERATURE_SET: 0x15600503) - READ_WRITE
    aidlvhal::VehiclePropConfig hvacConfig;
    hvacConfig.prop = toInt(aidlvhal::VehicleProperty::HVAC_TEMPERATURE_SET);
    hvacConfig.access = aidlvhal::VehiclePropertyAccess::READ_WRITE;
    hvacConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;

    // 9. Trang thai dong/mo cua xe (Standard AOSP - DOOR_POS: 0x16400B00) - READ_WRITE
    aidlvhal::VehiclePropConfig doorConfig;
    doorConfig.prop = toInt(aidlvhal::VehicleProperty::DOOR_POS);
    doorConfig.access = aidlvhal::VehiclePropertyAccess::READ_WRITE;
    doorConfig.changeMode = aidlvhal::VehiclePropertyChangeMode::ON_CHANGE;

    return {
        customBatteryConfig,
        fanSpeedConfig,
        speedConfig,
        gearConfig,
        rpmConfig,
        fuelConfig,
        nightModeConfig,
        hvacConfig,
        doorConfig
    }; 
}

aidlvhal::StatusCode SomeipVehicleHardware::setValues(
        std::shared_ptr<const SetValuesCallback> callback,
        const std::vector<aidlvhal::SetValueRequest>& requests) {
    std::vector<aidlvhal::SetValueResult> results;
    for (const auto& request : requests) {
        
        // 1. Neu la lenh chinh quat pin:
        if (request.value.prop == VENDOR_BATTERY_COOLING_FAN_SPEED) {
            if (!request.value.value.int32Values.empty()) {
                int32_t fanSpeed = request.value.value.int32Values[0];
                ALOGI("[VHAL CONTROL] Gui SOME/IP lenh chinh quat cap do %d", fanSpeed);
                // Lưu giá trị mới vào Cache
                {
                    std::lock_guard<std::mutex> lock(mValueLock);
                    mServerLocalStore[VENDOR_BATTERY_COOLING_FAN_SPEED] = request.value;
                }
                // Đóng gói SOME/IP Method Request sang ECU
                auto reqMsg = mRtm->create_request();
                reqMsg->set_service(BATTERY_SERVICE_ID);
                reqMsg->set_instance(BATTERY_INSTANCE_ID);
                reqMsg->set_method(SET_FAN_SPEED_METHOD);

                auto payload = mRtm->create_payload();
                std::vector<vsomeip::byte_t> data(
                    reinterpret_cast<uint8_t*>(&fanSpeed),
                    reinterpret_cast<uint8_t*>(&fanSpeed) + sizeof(int32_t));
                payload->set_data(data);
                reqMsg->set_payload(payload);
                
                mApp->send(reqMsg);
                // Bắn sự kiện ngược lại để báo cho các App khác biết quạt đã đổi
                std::lock_guard<std::mutex> lock(mCallbackLock);
                if (mPropChangeCallback) {
                    (*mPropChangeCallback)({ request.value });
                }
            }
        } else if (request.value.prop == toInt(aidlvhal::VehicleProperty::HVAC_TEMPERATURE_SET) ||
                   request.value.prop == toInt(aidlvhal::VehicleProperty::DOOR_POS) ||
                   request.value.prop == 0x21600100) {
            // Cho phep SET cac thuoc tinh READ_WRITE: HVAC, Door, Battery Temp
            {
                std::lock_guard<std::mutex> lock(mValueLock);
                mServerLocalStore[request.value.prop] = request.value;
            }
            std::lock_guard<std::mutex> lock(mCallbackLock);
            if (mPropChangeCallback) {
                (*mPropChangeCallback)({ request.value });
            }
            ALOGI("[VHAL CONTROL] Updated and dispatched property 0x%08x to CarService", request.value.prop);
        }

        
        
        // Here we could pack and send SOME/IP requests to external ECUs
        results.push_back({
            .requestId = request.requestId,
            .status = aidlvhal::StatusCode::OK,
        });
    }

    if (callback) {
        (*callback)(results);
    }
    return aidlvhal::StatusCode::OK;
}

aidlvhal::StatusCode SomeipVehicleHardware::getValues(
        std::shared_ptr<const GetValuesCallback> callback,
        const std::vector<aidlvhal::GetValueRequest>& requests) const {
    std::vector<aidlvhal::GetValueResult> results;
    std::lock_guard<std::mutex> lock(mValueLock);
    for (const auto& request : requests) {
        auto it = mServerLocalStore.find(request.prop.prop);
        if (it != mServerLocalStore.end()) {
            results.push_back({
                .requestId = request.requestId,
                .status = aidlvhal::StatusCode::OK,
                .prop = it->second,
            });
        } else {
            results.push_back({
                .requestId = request.requestId,
                .status = aidlvhal::StatusCode::NOT_AVAILABLE,
            });
        }
    }


    if (callback) {
        (*callback)(results);
    }
    return aidlvhal::StatusCode::OK;
}

DumpResult SomeipVehicleHardware::dump(const std::vector<std::string>& /*options*/) {
    return {
        .callerShouldDumpState = false,
        .buffer = "SomeipVehicleHardware status: ACTIVE, Connected to SOME/IP daemon.\n",
    };
}

aidlvhal::StatusCode SomeipVehicleHardware::checkHealth() {
    return aidlvhal::StatusCode::OK;
}

void SomeipVehicleHardware::registerOnPropertyChangeEvent(
        std::unique_ptr<const PropertyChangeCallback> callback) {
    std::lock_guard<std::mutex> lock(mCallbackLock);
    mPropChangeCallback = std::move(callback);
}

void SomeipVehicleHardware::registerOnPropertySetErrorEvent(
        std::unique_ptr<const PropertySetErrorCallback> callback) {
    std::lock_guard<std::mutex> lock(mCallbackLock);
    mPropSetErrorCallback = std::move(callback);
}

}  // namespace android::hardware::automotive::vehicle
