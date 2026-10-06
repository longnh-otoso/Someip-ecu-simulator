/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "VehicleService"

#include <DefaultVehicleHal.h>
#include <FakeVehicleHardware.h>
#include <SomeipVehicleHardware.h>

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <utils/Log.h>

using ::android::hardware::automotive::vehicle::DefaultVehicleHal;
using ::android::hardware::automotive::vehicle::DumpResult;
using ::android::hardware::automotive::vehicle::IVehicleHardware;
using ::android::hardware::automotive::vehicle::SetValueErrorEvent;
using ::android::hardware::automotive::vehicle::fake::FakeVehicleHardware;
using ::android::hardware::automotive::vehicle::SomeipVehicleHardware;
namespace aidlvhal = ::aidl::android::hardware::automotive::vehicle;

// Hybrid Vehicle Hardware: Kết hợp cả FakeVehicleHardware (Google) và SomeipVehicleHardware (SOME/IP)
class HybridVehicleHardware : public IVehicleHardware {
public:
    HybridVehicleHardware()
        : mFakeHardware(std::make_unique<FakeVehicleHardware>()),
          mSomeipHardware(std::make_unique<SomeipVehicleHardware>()) {}

    ~HybridVehicleHardware() override = default;

    // Gộp tất cả property configs của Google Fake và SOME/IP
    std::vector<aidlvhal::VehiclePropConfig> getAllPropertyConfigs() const override {
        auto configs = mFakeHardware->getAllPropertyConfigs();
        auto someipConfigs = mSomeipHardware->getAllPropertyConfigs();
        configs.insert(configs.end(), someipConfigs.begin(), someipConfigs.end());
        return configs;
    }

    // Điều hướng các request đọc dữ liệu
    aidlvhal::StatusCode getValues(
            std::shared_ptr<const GetValuesCallback> callback,
            const std::vector<aidlvhal::GetValueRequest>& requests) const override {
        std::vector<aidlvhal::GetValueRequest> someipReqs;
        std::vector<aidlvhal::GetValueRequest> fakeReqs;

        for (const auto& req : requests) {
            if (req.prop.prop == 0x21600100 || req.prop.prop == 0x21400101) {
                someipReqs.push_back(req);
            } else {
                fakeReqs.push_back(req);
            }
        }

        if (!someipReqs.empty()) {
            mSomeipHardware->getValues(callback, someipReqs);
        }
        if (!fakeReqs.empty()) {
            mFakeHardware->getValues(callback, fakeReqs);
        }
        return aidlvhal::StatusCode::OK;
    }

    // Điều hướng các request ghi dữ liệu
    aidlvhal::StatusCode setValues(
            std::shared_ptr<const SetValuesCallback> callback,
            const std::vector<aidlvhal::SetValueRequest>& requests) override {
        std::vector<aidlvhal::SetValueRequest> someipReqs;
        std::vector<aidlvhal::SetValueRequest> fakeReqs;

        for (const auto& req : requests) {
            if (req.value.prop == 0x21600100 || req.value.prop == 0x21400101) {
                someipReqs.push_back(req);
            } else {
                fakeReqs.push_back(req);
            }
        }

        if (!someipReqs.empty()) {
            mSomeipHardware->setValues(callback, someipReqs);
        }
        if (!fakeReqs.empty()) {
            mFakeHardware->setValues(callback, fakeReqs);
        }
        return aidlvhal::StatusCode::OK;
    }

    void registerOnPropertyChangeEvent(
            std::unique_ptr<const PropertyChangeCallback> callback) override {
        auto sharedCb = std::shared_ptr<const PropertyChangeCallback>(std::move(callback));
        mFakeHardware->registerOnPropertyChangeEvent(
            std::make_unique<const PropertyChangeCallback>([sharedCb](const auto& events) {
                if (sharedCb) (*sharedCb)(events);
            }));
        mSomeipHardware->registerOnPropertyChangeEvent(
            std::make_unique<const PropertyChangeCallback>([sharedCb](const auto& events) {
                if (sharedCb) (*sharedCb)(events);
            }));
    }

    void registerOnPropertySetErrorEvent(
            std::unique_ptr<const PropertySetErrorCallback> callback) override {
        mFakeHardware->registerOnPropertySetErrorEvent(std::move(callback));
    }

    DumpResult dump(const std::vector<std::string>& options) override {
        auto result = mFakeHardware->dump(options);
        auto someipResult = mSomeipHardware->dump(options);
        result.buffer += "\n" + someipResult.buffer;
        return result;
    }

    aidlvhal::StatusCode checkHealth() override {
        auto fakeStatus = mFakeHardware->checkHealth();
        if (fakeStatus != aidlvhal::StatusCode::OK) {
            return fakeStatus;
        }
        return mSomeipHardware->checkHealth();
    }

private:
    std::unique_ptr<FakeVehicleHardware> mFakeHardware;
    std::unique_ptr<SomeipVehicleHardware> mSomeipHardware;
};

int main(int /* argc */, char* /* argv */[]) {
    ALOGI("Starting thread pool...");
    if (!ABinderProcess_setThreadPoolMaxThreadCount(4)) {
        ALOGE("%s", "failed to set thread pool max thread count");
        return 1;
    }
    ABinderProcess_startThreadPool();

    std::unique_ptr<HybridVehicleHardware> hardware = std::make_unique<HybridVehicleHardware>();
    std::shared_ptr<DefaultVehicleHal> vhal =
            ::ndk::SharedRefBase::make<DefaultVehicleHal>(std::move(hardware));

    ALOGI("Registering as service...");
    binder_exception_t err = AServiceManager_addService(
            vhal->asBinder().get(), "android.hardware.automotive.vehicle.IVehicle/default");
    if (err != EX_NONE) {
        ALOGE("failed to register android.hardware.automotive.vehicle service, exception: %d", err);
        return 1;
    }

    ALOGI("Hybrid Vehicle Service (Fake + SOME/IP) Ready");

    ABinderProcess_joinThreadPool();

    ALOGI("Vehicle Service Exiting");

    return 0;
}
