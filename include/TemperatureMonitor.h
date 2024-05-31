#include <driver/temp_sensor.h>
#include <esp_adc_cal.h>

class TemperatureMonitor {
public:
    static void begin() {
        temp_sensor_start();
    }

    static float getTemperature() {
        float celsius;
        temp_sensor_read_celsius(&celsius);
        return celsius;
    }
};
