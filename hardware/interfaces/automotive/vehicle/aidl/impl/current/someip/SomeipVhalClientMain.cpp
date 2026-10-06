#include "SomeipVehicleHardware.h"
#include <iostream>
#include <thread>
#include <chrono>

using namespace android::hardware::automotive::vehicle;

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "  ANDROID VHAL SOME/IP CLIENT TEST RUNNER         " << std::endl;
    std::cout << "==================================================" << std::endl;

    // 1. Initialize SomeipVehicleHardware
    auto hardware = std::make_unique<SomeipVehicleHardware>();

    // 2. Register callback to handle vehicle property change events
    hardware->registerOnPropertyChangeEvent(
        std::make_unique<const IVehicleHardware::PropertyChangeCallback>(
            [](const std::vector<aidlvhal::VehiclePropValue>& events) {
                for (const auto& event : events) {
                    std::cout << "\n[VHAL CALLBACK] New property event from SOME/IP!" << std::endl;
                    std::cout << " -> Property ID         : " << event.prop << std::endl;
                    if (!event.value.floatValues.empty()) {
                        std::cout << " -> Battery Temperature : " << event.value.floatValues[0] << " deg C" << std::endl;
                    }
                    if (event.prop == 559939840) {
                        std::cout << " (VENDOR_CUSTOM_BATTERY_TEMP)";
                    }
                    std::cout << std::endl;
                }
            }
        )
    );

    std::cout << "[VHAL Client] Listening for telemetry from ECU over SOME/IP..." << std::endl;
    std::cout << "[VHAL Client] Periodic Battery Health requests will be sent every 10 seconds." << std::endl;
    std::cout << "[VHAL Client] Press Ctrl+C to exit." << std::endl;

    int counter = 0;
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        counter++;
        if (counter % 10 == 0) {
            std::cout << "\n[VHAL Client] Triggering periodic Battery Health query (RPC Request)..." << std::endl;
            hardware->requestBatteryHealth();
        }
    }

    return 0;
}
