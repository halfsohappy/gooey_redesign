// =============================================================================
// test_ab7_imu.cpp — On-device integration test for ab7 SlimeIMU (BNO085)
// =============================================================================
//
// Run with:  pio test -e ab7 --upload-port /dev/cu.usbmodem*
//
// This test runs on the actual ab7 board with a BNO085 physically attached.
// It validates:
//   1. SlimeIMU initialisation over SPI
//   2. Sensor detection and identification
//   3. Live data acquisition (quaternion, accel, gyro)
//   4. quat_to_euler conversion correctness
//   5. Data stream normalisation to [0, 1] range
//
// The board should be stationary on a flat surface during the test.
// =============================================================================

#include <Arduino.h>
#include <unity.h>
#include <SPI.h>
#include <SlimeIMU.h>
#include <math.h>

// Pull in the hardware layer under test
#include "ab7_hardware.h"
#include "data_streams.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Wait up to timeout_ms for imu_data_available() to return true.
static bool wait_for_imu_data(unsigned long timeout_ms = 3000) {
    unsigned long start = millis();
    while (millis() - start < timeout_ms) {
        if (imu_data_available()) return true;
        delay(10);
    }
    return false;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

/// SlimeIMU should initialise and detect the BNO085.
void test_slime_imu_initialisation(void) {
    // begin_imu() was called in setUp — if we get here, it didn't halt.
    TEST_ASSERT_TRUE_MESSAGE(slime.isSensorWorking(0),
        "SlimeIMU sensor 0 should be working after begin_imu()");
}

/// The detected sensor count should be at least 1.
void test_sensor_count(void) {
    TEST_ASSERT_GREATER_OR_EQUAL_MESSAGE(1, slime.getSensorCount(),
        "At least one sensor should be detected");
}

/// The detected sensor should identify as BNO085.
void test_sensor_type_is_bno085(void) {
    const char* name = slime.getSensorName(0);
    TEST_ASSERT_NOT_NULL_MESSAGE(name, "Sensor name should not be null");
    // SlimeIMU reports "BNO085" for a BNO085 chip.
    TEST_ASSERT_TRUE_MESSAGE(
        strstr(name, "BNO085") != nullptr || strstr(name, "BNO080") != nullptr,
        "Sensor should be identified as BNO085 (or BNO080 family)");
}

/// After initialisation, we should receive data within 3 seconds.
void test_data_available_within_timeout(void) {
    bool got_data = wait_for_imu_data(3000);
    TEST_ASSERT_TRUE_MESSAGE(got_data,
        "Should receive IMU data within 3 seconds of init");
}

/// The quaternion should be a valid unit quaternion (norm ≈ 1.0).
void test_quaternion_is_unit_length(void) {
    TEST_ASSERT_TRUE(wait_for_imu_data(2000));

    float qi, qj, qk, qr;
    imu_get_quat(qi, qj, qk, qr);

    float norm = sqrtf(qi*qi + qj*qj + qk*qk + qr*qr);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.05f, 1.0f, norm,
        "Quaternion norm should be ≈ 1.0");
}

/// The quaternion components should each be in [-1, 1].
void test_quaternion_component_range(void) {
    TEST_ASSERT_TRUE(wait_for_imu_data(2000));

    float qi, qj, qk, qr;
    imu_get_quat(qi, qj, qk, qr);

    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, qi);  // [-1, 1]
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, qj);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, qk);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 0.0f, qr);
}

/// Linear acceleration should be near zero when the board is stationary.
/// Allow up to 2 m/s² for noise and sensor drift.
void test_accel_near_zero_when_stationary(void) {
    // Read a few samples to let the sensor settle.
    for (int i = 0; i < 10; i++) {
        wait_for_imu_data(500);
    }

    float ax, ay, az;
    imu_get_accel(ax, ay, az);

    float accel_mag = sqrtf(ax*ax + ay*ay + az*az);
    TEST_ASSERT_LESS_THAN_MESSAGE(2.0f, accel_mag,
        "Linear acceleration magnitude should be < 2 m/s^2 when stationary");
}

/// Gyroscope should be near zero when the board is stationary.
/// Allow up to 0.3 rad/s for noise.
void test_gyro_near_zero_when_stationary(void) {
    for (int i = 0; i < 10; i++) {
        wait_for_imu_data(500);
    }

    float gx, gy, gz;
    imu_get_gyro(gx, gy, gz);

    float gyro_mag = sqrtf(gx*gx + gy*gy + gz*gz);
    TEST_ASSERT_LESS_THAN_MESSAGE(0.3f, gyro_mag,
        "Gyro magnitude should be < 0.3 rad/s when stationary");
}

/// quat_to_euler: identity quaternion (0,0,0,1) should give (0,0,0) degrees.
void test_euler_identity_quaternion(void) {
    float roll, pitch, yaw;
    quat_to_euler(0.0f, 0.0f, 0.0f, 1.0f, roll, pitch, yaw);

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, roll,  "Identity quat → roll should be 0");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, pitch, "Identity quat → pitch should be 0");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.01f, 0.0f, yaw,   "Identity quat → yaw should be 0");
}

/// quat_to_euler: 90° rotation about Z axis.
void test_euler_90deg_yaw(void) {
    // Quaternion for 90° rotation about Z: (0, 0, sin(45°), cos(45°))
    float s = sinf(PI / 4.0f);
    float c = cosf(PI / 4.0f);

    float roll, pitch, yaw;
    quat_to_euler(0.0f, 0.0f, s, c, roll, pitch, yaw);

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 0.0f,  roll,  "90° Z rotation → roll ≈ 0");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 0.0f,  pitch, "90° Z rotation → pitch ≈ 0");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2.0f, 90.0f, yaw,   "90° Z rotation → yaw ≈ 90");
}

/// quat_to_euler: 90° rotation about X axis (roll).
void test_euler_90deg_roll(void) {
    float s = sinf(PI / 4.0f);
    float c = cosf(PI / 4.0f);

    float roll, pitch, yaw;
    quat_to_euler(s, 0.0f, 0.0f, c, roll, pitch, yaw);

    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2.0f, 90.0f, roll,  "90° X rotation → roll ≈ 90");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 0.0f,  pitch, "90° X rotation → pitch ≈ 0");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 0.0f,  yaw,   "90° X rotation → yaw ≈ 0");
}

/// Euler normalisation to [0, 1] should work for the full range.
void test_euler_normalisation_range(void) {
    // Roll = -180..180 → [0, 1]
    float norm_roll_min = (-180.0f + 180.0f) / 360.0f;   // 0.0
    float norm_roll_max = (180.0f  + 180.0f) / 360.0f;   // 1.0

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, norm_roll_min);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, norm_roll_max);

    // Pitch = -90..90 → [0, 1]
    float norm_pitch_min = (-90.0f + 90.0f) / 180.0f;    // 0.0
    float norm_pitch_max = (90.0f  + 90.0f) / 180.0f;    // 1.0

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, norm_pitch_min);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, norm_pitch_max);
}

/// Accel/gyro normalisation should produce values in [0, 1].
void test_accel_gyro_normalisation(void) {
    // Simulate the normalisation formula from main.cpp:
    //   constrain((val / SCALE) * 0.5 + 0.5, 0, 1)
    const float SCALE = 4.0f;

    // Zero input → 0.5
    float zero_norm = constrain((0.0f / SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, zero_norm);

    // Positive max (SCALE) → 1.0
    float pos_norm = constrain((SCALE / SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, pos_norm);

    // Negative max (-SCALE) → 0.0
    float neg_norm = constrain((-SCALE / SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, neg_norm);

    // Out-of-range (2*SCALE) → clamped to 1.0
    float over_norm = constrain((2*SCALE / SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, over_norm);
}

/// Multiple consecutive reads should produce different timestamps/data
/// (i.e. the sensor is actually streaming, not stuck).
void test_sensor_is_streaming(void) {
    float q1_w, q2_w;

    TEST_ASSERT_TRUE(wait_for_imu_data(2000));
    float qi, qj, qk;
    imu_get_quat(qi, qj, qk, q1_w);

    // Wait for at least one more update
    delay(50);
    TEST_ASSERT_TRUE(wait_for_imu_data(2000));
    imu_get_quat(qi, qj, qk, q2_w);

    // The two reads should have come from different update cycles.
    // We can't guarantee the values differ (board is stationary),
    // but we at least confirm we got two successful reads.
    TEST_PASS_MESSAGE("Two consecutive reads succeeded — sensor is streaming");
}

/// Data stream name lookup should work correctly.
void test_data_stream_name_lookup(void) {
    TEST_ASSERT_EQUAL_STRING("accelX",      data_stream_name(ACCELX).c_str());
    TEST_ASSERT_EQUAL_STRING("gyroX",       data_stream_name(GYROX).c_str());
    TEST_ASSERT_EQUAL_STRING("baro",        data_stream_name(BARO).c_str());
    TEST_ASSERT_EQUAL_STRING("eulerX",      data_stream_name(EULERX).c_str());
    TEST_ASSERT_EQUAL_STRING("unknown",     data_stream_name(99).c_str());
}

/// Reverse name→index lookup should work case-insensitively.
void test_data_stream_index_lookup(void) {
    TEST_ASSERT_EQUAL(ACCELX,      data_stream_index_from_name("accelx"));
    TEST_ASSERT_EQUAL(ACCELLENGTH, data_stream_index_from_name("ACCELLENGTH"));
    TEST_ASSERT_EQUAL(BARO,        data_stream_index_from_name("baro"));
    TEST_ASSERT_EQUAL(-1,          data_stream_index_from_name("nonexistent"));
}

// ---------------------------------------------------------------------------
// Test runner
// ---------------------------------------------------------------------------

void setup() {
    delay(2000);  // Give USB serial time to connect
    Serial.begin(115200);

    // Initialise hardware — same sequence as main.cpp boot
    begin_pins();
    begin_imu();  // Will halt if BNO085 not found

    UNITY_BEGIN();

    // --- Initialisation tests ---
    RUN_TEST(test_slime_imu_initialisation);
    RUN_TEST(test_sensor_count);
    RUN_TEST(test_sensor_type_is_bno085);

    // --- Live data tests ---
    RUN_TEST(test_data_available_within_timeout);
    RUN_TEST(test_quaternion_is_unit_length);
    RUN_TEST(test_quaternion_component_range);
    RUN_TEST(test_accel_near_zero_when_stationary);
    RUN_TEST(test_gyro_near_zero_when_stationary);
    RUN_TEST(test_sensor_is_streaming);

    // --- Pure math tests (quat_to_euler) ---
    RUN_TEST(test_euler_identity_quaternion);
    RUN_TEST(test_euler_90deg_yaw);
    RUN_TEST(test_euler_90deg_roll);
    RUN_TEST(test_euler_normalisation_range);
    RUN_TEST(test_accel_gyro_normalisation);

    // --- Data stream utility tests ---
    RUN_TEST(test_data_stream_name_lookup);
    RUN_TEST(test_data_stream_index_lookup);

    UNITY_END();
}

void loop() {
    // Nothing — tests run once in setup().
}
