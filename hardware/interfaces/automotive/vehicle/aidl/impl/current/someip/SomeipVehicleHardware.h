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

#pragma once

#include <IVehicleHardware.h>
#include <vsomeip/vsomeip.hpp>

#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace android::hardware::automotive::vehicle {

namespace aidlvhal = ::aidl::android::hardware::automotive::vehicle;

class SomeipVehicleHardware : public IVehicleHardware {
public:
    SomeipVehicleHardware();
    ~SomeipVehicleHardware() override;

    // IVehicleHardware Pure Virtual Methods
    std::vector<aidlvhal::VehiclePropConfig> getAllPropertyConfigs() const override;

    aidlvhal::StatusCode setValues(
            std::shared_ptr<const SetValuesCallback> callback,
            const std::vector<aidlvhal::SetValueRequest>& requests) override;

    aidlvhal::StatusCode getValues(
            std::shared_ptr<const GetValuesCallback> callback,
            const std::vector<aidlvhal::GetValueRequest>& requests) const override;

    DumpResult dump(const std::vector<std::string>& options) override;

    aidlvhal::StatusCode checkHealth() override;

    void registerOnPropertyChangeEvent(
            std::unique_ptr<const PropertyChangeCallback> callback) override;

    void registerOnPropertySetErrorEvent(
            std::unique_ptr<const PropertySetErrorCallback> callback) override;

    // Public method to trigger Request/Response over SOME/IP
    void requestBatteryHealth();

private:
    void initSomeip();
    void onSomeipMessage(const std::shared_ptr<vsomeip::message>& msg);
    void onSomeipStateChange(vsomeip::state_type_e state);
    void onSomeipAvailability(vsomeip::service_t service, vsomeip::instance_t instance, bool isAvailable);
    void onBatteryHealthResponse(const std::shared_ptr<vsomeip::message>& response);

    std::shared_ptr<vsomeip::runtime> mRtm;
    std::shared_ptr<vsomeip::application> mApp;
    std::thread mSomeipThread;

    mutable std::mutex mCallbackLock;
    std::unique_ptr<const PropertyChangeCallback> mPropChangeCallback;
    std::unique_ptr<const PropertySetErrorCallback> mPropSetErrorCallback;

    mutable std::mutex mValueLock;
    //Cache save new values 
    std::unordered_map<int32_t, aidlvhal::VehiclePropValue> mServerLocalStore;
};

}  // namespace android::hardware::automotive::vehicle
