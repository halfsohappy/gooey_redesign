// =============================================================================
// bart_hardware.cpp — Bart board hardware driver implementation
// =============================================================================

#include <Preferences.h>
#include "bart_hardware.h"
#include <math.h>

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------

Adafruit_BMP5xx bmp;
Preferences preferences;
SlimeIMU slime;

/// Baseline altitude captured at boot — centre point for baro normalisation.
float baro_baseline_alt = 0.0f;

/// Range in metres that maps to [0, 1] around the baseline altitude.
static constexpr float BARO_RANGE_M = 5.0f;

// Cache for latest IMU readings
static Quat    cached_quat;
static Vector3 cached_accel;
static Vector3 cached_gyro;

// ---------------------------------------------------------------------------
// Pin initialisation
// ---------------------------------------------------------------------------

void begin_pins(bool b13, bool b46, bool cen1, bool cen2) {
    pinMode(CS_IMU, OUTPUT);
    pinMode(CS_MAG, OUTPUT);
    digitalWrite(CS_MAG, HIGH);  // deselect mag
    pinMode(SEL13, OUTPUT);
    pinMode(SEL46, OUTPUT);
    pinMode(CC_EN1, OUTPUT);
    pinMode(CC_EN2, OUTPUT);
    digitalWrite(SEL13, b13);
    digitalWrite(SEL46, b46);
    digitalWrite(CC_EN1, cen1);
    digitalWrite(CC_EN2, cen2);
}

// ---------------------------------------------------------------------------
// Barometer initialisation (BMP5xx, separate SPI device)
// ---------------------------------------------------------------------------

void begin_baro(uint16_t BCS) {
    bmp.begin(BCS, &SPI);
    bmp.setTemperatureOversampling(BMP5XX_OVERSAMPLING_1X);
    bmp.setPressureOversampling(BMP5XX_OVERSAMPLING_32X);
    bmp.setIIRFilterCoeff(BMP5XX_IIR_FILTER_COEFF_15);
    bmp.setOutputDataRate(BMP5XX_ODR_45_HZ);
    bmp.setPowerMode(BMP5XX_POWERMODE_CONTINUOUS);
    bmp.enablePressure(true);
    bmp.configureInterrupt(BMP5XX_INTERRUPT_LATCHED, BMP5XX_INTERRUPT_ACTIVE_HIGH, BMP5XX_INTERRUPT_PUSH_PULL, BMP5XX_INTERRUPT_DATA_READY, true);
    delay(5);

    // Capture baseline altitude for relative normalisation.
    baro_baseline_alt = bmp.readAltitude(SEALEVELPRESSURE_HPA);
    Serial.print(F("[BARO] Baseline altitude: "));
    Serial.print(baro_baseline_alt, 1);
    Serial.println(F(" m"));
}

// ---------------------------------------------------------------------------
// IMU initialisation (ISM330DHCX via SlimeIMU over SPI)
// ---------------------------------------------------------------------------

void begin_imu() {
    // SlimeIMU reads PIN_IMU_CS, PIN_SPI_SCK/MISO/MOSI from build_flags.
    // It auto-detects the ISM330DHCX on the SPI bus and runs VQF sensor fusion.
    //
    // I2C SDA/SCL are passed for any secondary I2C sensor (unused here).
    if (!slime.begin(21, 22)) {
        Serial.println(F("[IMU] SlimeIMU failed to initialise ISM330DHCX over SPI!"));
        Serial.println(F("[IMU] Check SPI wiring (CS=42, SCK=36, MOSI=35, MISO=37).  Halting."));
        while (1) { delay(100); }
    }

    Serial.print(F("[IMU] ISM330DHCX initialised via SlimeIMU ("));
    Serial.print(slime.getSensorName(0));
    Serial.println(F(")."));
}

// ---------------------------------------------------------------------------
// Data access helpers
// ---------------------------------------------------------------------------

bool imu_data_available() {
    slime.update();
    if (slime.hasNewData(0)) {
        cached_quat  = slime.getQuaternion(0);
        cached_accel = slime.getLinearAcceleration(0);
        cached_gyro  = slime.getAngularVelocity(0);
        return true;
    }
    return false;
}

void imu_get_quat(float &qi, float &qj, float &qk, float &qr) {
    qi = cached_quat.x;
    qj = cached_quat.y;
    qk = cached_quat.z;
    qr = cached_quat.w;
}

void imu_get_accel(float &ax, float &ay, float &az) {
    ax = cached_accel.x;
    ay = cached_accel.y;
    az = cached_accel.z;
}

void imu_get_gyro(float &gx, float &gy, float &gz) {
    gx = cached_gyro.x;
    gy = cached_gyro.y;
    gz = cached_gyro.z;
}

// ---------------------------------------------------------------------------
// Barometer — read and normalise to [0, 1]
// ---------------------------------------------------------------------------
//
// Returns a value in [0, 1] representing relative altitude change from the
// baseline captured at boot.  ±BARO_RANGE_M (±5 m) maps to the full range;
// 0.5 = at baseline altitude.

float read_baro_normalized() {
    float alt = bmp.readAltitude(SEALEVELPRESSURE_HPA);
    float delta = alt - baro_baseline_alt;
    return constrain((delta + BARO_RANGE_M) / (2.0f * BARO_RANGE_M), 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Quaternion → Euler conversion
// ---------------------------------------------------------------------------
//
// Returns roll (X), pitch (Y), yaw (Z) in degrees.

void quat_to_euler(float qi, float qj, float qk, float qr,
                   float &roll, float &pitch, float &yaw) {
    // Roll (X-axis rotation)
    float sinr_cosp = 2.0f * (qr * qi + qj * qk);
    float cosr_cosp = 1.0f - 2.0f * (qi * qi + qj * qj);
    roll = atan2f(sinr_cosp, cosr_cosp) * (180.0f / PI);

    // Pitch (Y-axis rotation)
    float sinp = 2.0f * (qr * qj - qk * qi);
    if (fabsf(sinp) >= 1.0f)
        pitch = copysignf(90.0f, sinp);   // clamp at ±90°
    else
        pitch = asinf(sinp) * (180.0f / PI);

    // Yaw (Z-axis rotation)
    float siny_cosp = 2.0f * (qr * qk + qi * qj);
    float cosy_cosp = 1.0f - 2.0f * (qj * qj + qk * qk);
    yaw = atan2f(siny_cosp, cosy_cosp) * (180.0f / PI);
}
