package android.car.custom;

import android.annotation.RequiresPermission;
import android.annotation.SuppressLint;
import android.car.Car;
import android.car.CarManagerBase;
import android.os.IBinder;
import android.os.RemoteException;
import android.util.Log;

@SuppressLint("UnflaggedApi")
public final class CarBatteryManager extends CarManagerBase {
    private static final String TAG = "CarBatteryManager";
    private final ICarBattery mService;

    /** @hide */
    public CarBatteryManager(Car car, IBinder service) {
        super(car);
        mService = ICarBattery.Stub.asInterface(service);
    }

    @SuppressLint("UnflaggedApi")
    @RequiresPermission(Car.PERMISSION_READ_CAR_BATTERY)
    public float getBatteryTemperature() {
        try {
            return mService.getBatteryTemperature();
        } catch (RemoteException e) {
            return handleRemoteExceptionFromCarService(e, 0.0f);
        }
    }

    @SuppressLint("UnflaggedApi")
    @RequiresPermission(Car.PERMISSION_READ_CAR_BATTERY)
    public boolean isOverheating() {
        try {
            return mService.isOverheating();
        } catch (RemoteException e) {
            return handleRemoteExceptionFromCarService(e, false);
        }
    }

    /** @hide */
    @Override
    public void onCarDisconnected() {}
}
