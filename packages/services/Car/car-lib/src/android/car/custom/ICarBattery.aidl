package android.car.custom;

/** @hide */
interface ICarBattery {
    float getBatteryTemperature();
    boolean isOverheating();
    void setBatteryWarningThreshold(float tempThreshold);

}