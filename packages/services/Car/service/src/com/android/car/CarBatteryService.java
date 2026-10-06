package com.android.car;

import android.car.Car;
import android.car.custom.ICarBattery;
import android.car.hardware.CarPropertyValue;
import android.content.Context;
import android.os.Binder;
import android.util.Slog;
import android.util.proto.ProtoOutputStream;

import com.android.car.internal.util.IndentingPrintWriter;

public class CarBatteryService extends ICarBattery.Stub implements CarServiceBase {
    private static final String TAG = "CarBatteryService";
    
    // Property ID VHAL được đóng gói PRIVATE chuẩn Clean Architecture
    // VENDOR(0x20000000) | GLOBAL(0x01000000) | FLOAT(0x00600000) | ID(0x0100) = 0x21600100
    private static final int VENDOR_CUSTOM_BATTERY_TEMP = 0x21600100;

    private final Context mContext;
    private final CarPropertyService mCarPropertyService;
    private float mWarningThreshold = 55.0f;

    public CarBatteryService(Context context, CarPropertyService carPropertyService) {
        mContext = context;
        mCarPropertyService = carPropertyService;
    }

    @Override
    public void init() {
        Slog.i(TAG, "CarBatteryService initialized!");
    }

    @Override
    public void release() {}

    @Override
    public void dump(IndentingPrintWriter writer) {
        writer.println("*CarBatteryService*");
        writer.println("  Current Battery Temp: " + getBatteryTemperature() + " °C");
        writer.println("  Warning Threshold: " + mWarningThreshold + " °C");
        writer.println("  Is Overheating: " + isOverheating());
    }

    @Override
    public void dumpProto(ProtoOutputStream proto) {}

    @Override
    public float getBatteryTemperature() {
        mContext.enforceCallingOrSelfPermission(
            Car.PERMISSION_READ_CAR_BATTERY, 
            "Denied access to CarBatteryService"
        );

        final long token = Binder.clearCallingIdentity();
        try {
            CarPropertyValue propValue = mCarPropertyService.getProperty(
                VENDOR_CUSTOM_BATTERY_TEMP, 
                0
            );
            if (propValue != null && propValue.getValue() != null) {
                return (Float) propValue.getValue();
            }
        } catch (Exception e) {
            Slog.e(TAG, "Failed to read VHAL temp: " + e.getMessage(), e);
        } finally {
            Binder.restoreCallingIdentity(token);
        }
        return 0.0f;
    }

    @Override
    public boolean isOverheating() {
        mContext.enforceCallingOrSelfPermission(
            Car.PERMISSION_READ_CAR_BATTERY, 
            "Denied access to CarBatteryService"
        );
        return getBatteryTemperature() > mWarningThreshold;
    }

    @Override
    public void setBatteryWarningThreshold(float tempThreshold) {
        mContext.enforceCallingOrSelfPermission(
            Car.PERMISSION_CONTROL_CAR_CLIMATE, 
            "Denied control threshold"
        );
        mWarningThreshold = tempThreshold;
    }
}
