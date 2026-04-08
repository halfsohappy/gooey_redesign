// =============================================================================
// main.cpp — Entry point for the TheaterGWD firmware
// =============================================================================
//
// BOOT SEQUENCE:
//   1. Initialise serial, GPIO pins, sensors.
//      - Bart: barometer (BMP5xx), IMU (LSM6DSV16XTR via SlimeIMU)
//      - ab7:  BNO085 IMU (SPI via SlimeIMU), SK6812 status LED, two buttons;
//              no physical barometer and BARO is forced to 1.0
//   2. Check if the device has been provisioned (WiFi credentials stored).
//      - Yes → connect to WiFi, start UDP listener.
//      - No  → launch the captive-portal provisioner and wait.
//   3. Create a FreeRTOS task that continuously reads sensors (or, for
//      Bart development, fills data_streams[] with simulated sine waves).
//   4. Enter the main loop, which polls for incoming OSC messages and
//      dispatches them through osc_handle_message().
//
// Build with -DAB7_BUILD for the ab7 board.  Without it the Bart board is
// targeted.
// =============================================================================

#include "main.h"

// Current quaternion — shared with osc_commands.h for ori save commands
float cur_qi = 0.0f, cur_qj = 0.0f, cur_qk = 0.0f, cur_qr = 1.0f;

// Tare reference quaternion — identity = no tare applied
float tare_qi = 0.0f, tare_qj = 0.0f, tare_qk = 0.0f, tare_qr = 1.0f;
bool  tare_active = false;

// Euler decomposition order — auto-selected at tare time.
// 0 = ZYX (default, singular on Y/pitch)
// 1 = ZXY (singular on X/roll — chosen when device Y-axis is most vertical)
int euler_order = 0;

// Swing-twist decomposition axes — auto-selected at tare time.
// twist_n = device axis most horizontal at tare (the "arm" axis).
// tare_up = device axis most vertical at tare (the "up" direction).
float twist_nx = 1.0f, twist_ny = 0.0f, twist_nz = 0.0f;
float tare_up_x = 0.0f, tare_up_y = 0.0f, tare_up_z = 1.0f;

#ifdef AB7_BUILD

// SK6812 LED (one pixel)
#define NUM_LEDS 1
static CRGB leds[NUM_LEDS];

// Button debounce + hold-to-record state
static unsigned long btn_a_last       = 0;     // last release time (debounce)
static unsigned long btn_a_pressed_at = 0;     // when A went down
static bool          btn_a_down       = false; // A is currently pressed
static bool          btn_a_held       = false; // entered hold/record mode
static unsigned long btn_b_last       = 0;
static constexpr unsigned long BTN_DEBOUNCE_MS = 80;
static constexpr unsigned long BTN_HOLD_MS     = 300; // ms to trigger record
#endif // AB7_BUILD

/// True once the device has connected to WiFi and UDP is ready for OSC.
static bool network_ready = false;

#ifndef PIO_UNIT_TESTING
void setup() {
    Serial.begin(115200);
    delay(1000);  // brief pause for serial monitor to connect
    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
#ifdef AB7_BUILD
    Serial.println(F("  TheaterGWD ab7 — booting..."));
#else
    Serial.println(F("  TheaterGWD — booting..."));
#endif
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.print(F("  Chip     : "));
    Serial.println(ESP.getChipModel());
    Serial.print(F("  CPU      : "));
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(F(" MHz"));
    Serial.print(F("  Free heap: "));
    Serial.print(ESP.getFreeHeap());
    Serial.println(F(" bytes"));
    Serial.println();

    // --- Constant streams (never written by sensor task) --------------------
    data_streams[CONST_ZERO] = 0.0f;
    data_streams[CONST_ONE]  = 1.0f;

    // --- Hardware initialisation --------------------------------------------
#ifdef AB7_BUILD
    Serial.println(F("[BOOT] Initialising GPIO pins..."));
    begin_pins();

    Serial.println(F("[BOOT] Initialising SK6812 LED..."));
    FastLED.addLeds<SK6812, STATUS_LED_PIN, GRB>(leds, NUM_LEDS);
    leds[0] = CRGB(40, 0, 40);  // dim purple = booting
    FastLED.show();

    Serial.println(F("[BOOT] Initialising BNO085 via SlimeIMU (SPI)..."));
    begin_imu();
#else
    Serial.println(F("[BOOT] Initialising GPIO pins..."));
    begin_pins(0, 0, 0, 0);

    Serial.println(F("[BOOT] Initialising SPI bus..."));
    SPI.begin(SCK_PIN, SDO_PIN, SDI_PIN, CS_IMU);

    Serial.println(F("[BOOT] Initialising barometer (BMP5xx)..."));
    begin_baro(CS_BAR);

    Serial.println(F("[BOOT] Initialising IMU (LSM6DSV16XTR) via SlimeIMU (SPI)..."));
    begin_imu();
#endif

    Serial.println(F("[BOOT] Hardware initialised."));
    Serial.println();

    // --- Provisioning / network ---------------------------------------------
    preferences.begin("device_config", true);  // read-only

    if (preferences.getBool("provisioned", false)) {
        Serial.println(F("[BOOT] Device is provisioned — loading config..."));

        String ssid     = preferences.getString("ssid", "");
        String password = preferences.getString("net_pass", "");
        bool   use_dhcp = preferences.getBool("use_dhcp", true);
        String ip_str   = use_dhcp ? "dhcp" : preferences.getString("static_ip", "dhcp");
        int    port     = preferences.getInt("port", 8000);

        device_adr = preferences.getString("device_adr", "");

        Serial.print(F("  SSID       : "));
        Serial.println(ssid);
        Serial.print(F("  IP mode    : "));
        Serial.println(use_dhcp ? F("DHCP") : ip_str);
        Serial.print(F("  Port       : "));
        Serial.println(port);
        Serial.print(F("  Device name: "));
        Serial.println(device_adr);

        // Normalise: must start with '/' and not end with '/'.
        if (device_adr.length() > 0) {
            if (!device_adr.startsWith("/")) device_adr = "/" + device_adr;
            if (device_adr.endsWith("/"))    device_adr.remove(device_adr.length() - 1);
        }

        preferences.end();

        Serial.println();
        begin_udp(ip_str, ssid, password, port);
        network_ready = true;

#ifdef AB7_BUILD
        leds[0] = CRGB(0, 40, 0);  // green = connected
        FastLED.show();
#endif

        Serial.print(F("[BOOT] Device address: "));
        Serial.println(device_adr);
    } else {
        preferences.end();

        Serial.println(F("[BOOT] Not provisioned — starting captive portal..."));
        Serial.println(F("[BOOT] Connect to WiFi AP 'annieData Setup' and open a browser."));
        Serial.println();

#ifdef AB7_BUILD
        leds[0] = CRGB(40, 20, 0);  // orange = provisioning
        FastLED.show();
#endif

        // Initialise WiFi STA first so the WiFiProvisioner's internal
        // WiFi.disconnect() call does not trigger the ESP-IDF error
        // "[E][STA.cpp:530] disconnect(): STA not started!".
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(true);
        delay(100);

        network_config();
        // network_config() calls ESP.restart() on success, so we only reach
        // here if provisioning is still in progress (blocking call).
    }

    // --- Sensor reading task ------------------------------------------------

#ifdef AB7_BUILD
    // ab7: real IMU data, pinned to core 1 (SPI interrupt handler on core 1).
    xTaskCreatePinnedToCore([](void*) {
        OriTracker& ot = ori_tracker();
        bool first_data = true;
        unsigned long no_data_count = 0;
        unsigned long total_reads = 0;
        unsigned long last_heartbeat_ms = 0;
        static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 10000;

        for (;;) {
            if (imu_data_available()) {
                no_data_count = 0;
                total_reads++;

                if (first_data) {
                    first_data = false;
                    Serial.println(F("[IMU] First sensor data received."));
                }

                // ── Rotation vector (quaternion) ───────────────────────
                float qi, qj, qk, qr;
                imu_get_quat(qi, qj, qk, qr);

                // Store globally for ori save commands.
                cur_qi = qi;
                cur_qj = qj;
                cur_qk = qk;
                cur_qr = qr;

                // Apply tare: compute orientation relative to reference pose.
                // q_rel = q_tare_conj ⊗ q_current  (conjugate = inverse for unit quat)
                float eq_i = qi, eq_j = qj, eq_k = qk, eq_r = qr;
                if (tare_active) {
                    eq_r =  tare_qr*qr + tare_qi*qi + tare_qj*qj + tare_qk*qk;
                    eq_i =  tare_qr*qi - tare_qi*qr - tare_qj*qk + tare_qk*qj;
                    eq_j =  tare_qr*qj + tare_qi*qk - tare_qj*qr - tare_qk*qi;
                    eq_k =  tare_qr*qk - tare_qi*qj + tare_qj*qi - tare_qk*qr;
                }

                // Convert to Euler angles in degrees, then normalise to [0, 1].
                // Decomposition is auto-selected at tare time to avoid gimbal lock.
                float roll, pitch, yaw;
                if (euler_order == 1) {
                    // ZXY — singular on X/roll; chosen when device Y is most vertical.
                    quat_to_euler_zxy(eq_i, eq_j, eq_k, eq_r, roll, pitch, yaw);
                    data_streams[ROLL]   = (roll  + 90.0f)  / 180.0f;  // asin [-90,+90]
                    data_streams[PITCH]  = (pitch + 180.0f) / 360.0f;
                    data_streams[YAW]    = (yaw   + 180.0f) / 360.0f;
                } else {
                    // ZYX (default) — singular on Y/pitch.
                    quat_to_euler(eq_i, eq_j, eq_k, eq_r, roll, pitch, yaw);
                    data_streams[ROLL]   = (roll  + 180.0f) / 360.0f;
                    data_streams[PITCH]  = (pitch + 90.0f)  / 180.0f;  // asin [-90,+90]
                    data_streams[YAW]    = (yaw   + 180.0f) / 360.0f;
                }

                // ── Swing-twist decomposition ─────────────────────────
                // Splits tare-relative rotation into twist (around arm axis)
                // and swing (where the arm points).  Uses rotate_vec() from
                // ori_tracker.h — same helper the ori system uses.
                {
                    float qw = eq_r, qx = eq_i, qy = eq_j, qz = eq_k;
                    float nx = twist_nx, ny = twist_ny, nz = twist_nz;

                    // Project quaternion vector part onto twist axis
                    float proj = qx*nx + qy*ny + qz*nz;
                    float qt_len = sqrtf(qw*qw + proj*proj);

                    float twist_deg;
                    if (qt_len < 1e-6f) {
                        twist_deg = 0.0f;  // degenerate: pure 180° swing
                    } else {
                        twist_deg = 2.0f * atan2f(proj / qt_len, qw / qt_len)
                                    * (180.0f / (float)M_PI);
                    }
                    data_streams[TWIST] = (twist_deg + 180.0f) / 360.0f;

                    // Swing: rotate twist axis by the tare-relative quaternion
                    // to see where the arm now points in the reference frame.
                    float vx, vy, vz;
                    rotate_vec(eq_i, eq_j, eq_k, eq_r, nx, ny, nz, vx, vy, vz);

                    // Tilt = elevation angle (dot with up axis)
                    float v_up = vx*tare_up_x + vy*tare_up_y + vz*tare_up_z;
                    float tilt_deg = asinf(constrain(v_up, -1.0f, 1.0f))
                                     * (180.0f / (float)M_PI);
                    data_streams[TILT] = (tilt_deg + 90.0f) / 180.0f;

                    // Heading = azimuth in horizontal plane.
                    // Reference forward = twist axis projected horizontal at tare.
                    float n_up = nx*tare_up_x + ny*tare_up_y + nz*tare_up_z;
                    float fx = nx - n_up*tare_up_x;
                    float fy = ny - n_up*tare_up_y;
                    float fz = nz - n_up*tare_up_z;
                    float flen = sqrtf(fx*fx + fy*fy + fz*fz);
                    if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }
                    // Right = up × forward
                    float rx = tare_up_y*fz - tare_up_z*fy;
                    float ry = tare_up_z*fx - tare_up_x*fz;
                    float rz = tare_up_x*fy - tare_up_y*fx;
                    // Remove vertical component from v
                    float hx = vx - v_up*tare_up_x;
                    float hy = vy - v_up*tare_up_y;
                    float hz = vz - v_up*tare_up_z;
                    float heading_deg = atan2f(hx*rx + hy*ry + hz*rz,
                                               hx*fx + hy*fy + hz*fz)
                                        * (180.0f / (float)M_PI);
                    data_streams[HEADING] = (heading_deg + 180.0f) / 360.0f;
                }

                // ── Linear acceleration (gravity-free, m/s²) ──────────
                float ax, ay, az;
                imu_get_accel(ax, ay, az);

                const float ACCEL_SCALE = 4.0f;
                data_streams[ACCELX]      = constrain((ax / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELY]      = constrain((ay / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELZ]      = constrain((az / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                float accel_len = sqrtf(ax * ax + ay * ay + az * az);
                data_streams[ACCELLENGTH] = constrain(accel_len / ACCEL_SCALE, 0.0f, 1.0f);

                // ── Global-frame (rotation-compensated) acceleration ───
                // Rotate body-frame linear accel into global frame using
                // the quaternion: v_global = q * v_body * q^-1.
                float gax = (1.0f - 2.0f*(qj*qj + qk*qk))*ax + 2.0f*(qi*qj - qr*qk)*ay + 2.0f*(qi*qk + qr*qj)*az;
                float gay = 2.0f*(qi*qj + qr*qk)*ax + (1.0f - 2.0f*(qi*qi + qk*qk))*ay + 2.0f*(qj*qk - qr*qi)*az;
                float gaz = 2.0f*(qi*qk - qr*qj)*ax + 2.0f*(qj*qk + qr*qi)*ay + (1.0f - 2.0f*(qi*qi + qj*qj))*az;

                data_streams[GACCELX]      = constrain((gax / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GACCELY]      = constrain((gay / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GACCELZ]      = constrain((gaz / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GACCELLENGTH] = constrain(accel_len / ACCEL_SCALE, 0.0f, 1.0f);

                // ── Swing-twist frame acceleration ────────────────────
                // Project global-frame accel onto arm coordinate frame:
                //   armFwd  = along arm direction (swing-rotated twist axis)
                //   armLat  = perpendicular horizontal (right of arm)
                //   armVert = vertical (up axis)
                {
                    float avx, avy, avz;
                    rotate_vec(eq_i, eq_j, eq_k, eq_r,
                               twist_nx, twist_ny, twist_nz, avx, avy, avz);
                    float a_up = avx*tare_up_x + avy*tare_up_y + avz*tare_up_z;
                    // Arm forward (horizontal projection of arm direction)
                    float afx = avx - a_up*tare_up_x;
                    float afy = avy - a_up*tare_up_y;
                    float afz = avz - a_up*tare_up_z;
                    float aflen = sqrtf(afx*afx + afy*afy + afz*afz);
                    if (aflen > 1e-6f) { afx /= aflen; afy /= aflen; afz /= aflen; }
                    // Arm right = up × forward
                    float arx = tare_up_y*afz - tare_up_z*afy;
                    float ary = tare_up_z*afx - tare_up_x*afz;
                    float arz = tare_up_x*afy - tare_up_y*afx;
                    // Project global accel onto arm basis
                    float af = gax*afx + gay*afy + gaz*afz;
                    float ar = gax*arx + gay*ary + gaz*arz;
                    float av = gax*tare_up_x + gay*tare_up_y + gaz*tare_up_z;
                    data_streams[LIMB_FWD]   = constrain((af / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                    data_streams[LIMB_LAT]   = constrain((ar / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                    data_streams[LIMB_VERT]  = constrain((av / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                    float limb_len = sqrtf(af*af + ar*ar + av*av);
                    data_streams[TWITCH]     = constrain(limb_len / ACCEL_SCALE, 0.0f, 1.0f);
                }

                // ── Quaternion components (raw, untared) ───────────────
                // Normalised via *0.5+0.5 → [0,1].  Set low:-1 high:1 on the
                // OscMessage to recover the native [-1,1] quaternion range.
                data_streams[QUAT_I] = qi * 0.5f + 0.5f;
                data_streams[QUAT_J] = qj * 0.5f + 0.5f;
                data_streams[QUAT_K] = qk * 0.5f + 0.5f;
                data_streams[QUAT_R] = qr * 0.5f + 0.5f;

                // ── Gyroscope (rad/s) ─────────────────────────────────
                float gx, gy, gz;
                imu_get_gyro(gx, gy, gz);

                const float GYRO_SCALE = 4.0f;
                data_streams[GYROX]       = constrain((gx / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROY]       = constrain((gy / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROZ]       = constrain((gz / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                float gyro_len = sqrtf(gx * gx + gy * gy + gz * gz);
                data_streams[GYROLENGTH]  = constrain(gyro_len / GYRO_SCALE, 0.0f, 1.0f);

                // ── Barometer — not present on ab7; fixed high sentinel ─
                data_streams[BARO] = 1.0f;

                // ── Update orientation tracker ────────────────────────
                ot.update(qi, qj, qk, qr, gyro_len);

                // ── Ori-watch: push active-ori changes to status config ──
                if (ot.ori_watch_enabled) {
                    static String _watch_prev;
                    static unsigned long _watch_ms = 0;
                    String cur = ot.active_ori_name;
                    unsigned long now_w = millis();
                    if (cur != _watch_prev && now_w - _watch_ms >= 100) {
                        _watch_ms   = now_w;
                        _watch_prev = cur;
                        StatusReporter& sr = status_reporter();
                        if (sr.dest_port > 0) {
                            String watch_adr = "/reply" + device_adr + "/ori/active";
                            String payload   = cur.length() > 0 ? cur : "(none)";
                            xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
                            osc.setDestination(sr.dest_ip, sr.dest_port);
                            osc.sendString(watch_adr.c_str(), payload.c_str());
                            xSemaphoreGive(osc_send_mutex());
                        }
                    }
                }
            } else {
                no_data_count++;
                // Warn once after ~5 seconds of no data (500 × 10ms).
                if (no_data_count == 500) {
                    Serial.print(F("[IMU] WARNING: No sensor data for 5 seconds — "));
                    Serial.println(F("Check SPI wiring (CS=10, MOSI=11, SCK=12, MISO=13, INT=4, RST=5, WAKE=6)."));
                }
            }

            // ── Periodic heartbeat — print sensor summary every 10 s ──
            unsigned long now = millis();
            if (now - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
                last_heartbeat_ms = now;
                Serial.print(F("[IMU] Heartbeat — reads: "));
                Serial.print(total_reads);
                Serial.print(F("  aX:"));
                Serial.print((float)data_streams[ACCELX], 3);
                Serial.print(F("  gX:"));
                Serial.print((float)data_streams[GYROX], 3);
                Serial.print(F("  eX:"));
                Serial.println((float)data_streams[ROLL], 3);
            }

            vTaskDelay(pdMS_TO_TICKS(10));  // ~100 Hz update rate
        }
    }, "sensor_task", 16384, nullptr, 1, nullptr, 1);  // 16 KB — BNO085 driver  |  pinned to core 1

    Serial.println(F("[BOOT] Sensor task started (BNO085 real data)."));
#else
    // Bart: real IMU data via SlimeIMU (LSM6DSV16XTR + VQF fusion).
    xTaskCreate([](void*) {
        OriTracker& ot = ori_tracker();
        bool first_data = true;
        unsigned long no_data_count = 0;
        unsigned long total_reads = 0;
        unsigned long last_heartbeat_ms = 0;
        static constexpr unsigned long HEARTBEAT_INTERVAL_MS = 10000;

        for (;;) {
            if (imu_data_available()) {
                no_data_count = 0;
                total_reads++;

                if (first_data) {
                    first_data = false;
                    Serial.println(F("[IMU] First sensor data received."));
                }

                // ── Rotation vector (quaternion) ───────────────────────
                float qi, qj, qk, qr;
                imu_get_quat(qi, qj, qk, qr);

                // Store globally for ori save commands.
                cur_qi = qi;
                cur_qj = qj;
                cur_qk = qk;
                cur_qr = qr;

                // Apply tare: compute orientation relative to reference pose.
                // q_rel = q_tare_conj ⊗ q_current  (conjugate = inverse for unit quat)
                float eq_i = qi, eq_j = qj, eq_k = qk, eq_r = qr;
                if (tare_active) {
                    eq_r =  tare_qr*qr + tare_qi*qi + tare_qj*qj + tare_qk*qk;
                    eq_i =  tare_qr*qi - tare_qi*qr - tare_qj*qk + tare_qk*qj;
                    eq_j =  tare_qr*qj + tare_qi*qk - tare_qj*qr - tare_qk*qi;
                    eq_k =  tare_qr*qk - tare_qi*qj + tare_qj*qi - tare_qk*qr;
                }

                // Convert to Euler angles in degrees, then normalise to [0, 1].
                // Decomposition is auto-selected at tare time to avoid gimbal lock.
                float roll, pitch, yaw;
                if (euler_order == 1) {
                    // ZXY — singular on X/roll; chosen when device Y is most vertical.
                    quat_to_euler_zxy(eq_i, eq_j, eq_k, eq_r, roll, pitch, yaw);
                    data_streams[ROLL]   = (roll  + 90.0f)  / 180.0f;  // asin [-90,+90]
                    data_streams[PITCH]  = (pitch + 180.0f) / 360.0f;
                    data_streams[YAW]    = (yaw   + 180.0f) / 360.0f;
                } else {
                    // ZYX (default) — singular on Y/pitch.
                    quat_to_euler(eq_i, eq_j, eq_k, eq_r, roll, pitch, yaw);
                    data_streams[ROLL]   = (roll  + 180.0f) / 360.0f;
                    data_streams[PITCH]  = (pitch + 90.0f)  / 180.0f;  // asin [-90,+90]
                    data_streams[YAW]    = (yaw   + 180.0f) / 360.0f;
                }

                // ── Swing-twist decomposition ─────────────────────────
                // Splits tare-relative rotation into twist (around arm axis)
                // and swing (where the arm points).  Uses rotate_vec() from
                // ori_tracker.h — same helper the ori system uses.
                {
                    float qw = eq_r, qx = eq_i, qy = eq_j, qz = eq_k;
                    float nx = twist_nx, ny = twist_ny, nz = twist_nz;

                    // Project quaternion vector part onto twist axis
                    float proj = qx*nx + qy*ny + qz*nz;
                    float qt_len = sqrtf(qw*qw + proj*proj);

                    float twist_deg;
                    if (qt_len < 1e-6f) {
                        twist_deg = 0.0f;  // degenerate: pure 180° swing
                    } else {
                        twist_deg = 2.0f * atan2f(proj / qt_len, qw / qt_len)
                                    * (180.0f / (float)M_PI);
                    }
                    data_streams[TWIST] = (twist_deg + 180.0f) / 360.0f;

                    // Swing: rotate twist axis by the tare-relative quaternion
                    // to see where the arm now points in the reference frame.
                    float vx, vy, vz;
                    rotate_vec(eq_i, eq_j, eq_k, eq_r, nx, ny, nz, vx, vy, vz);

                    // Tilt = elevation angle (dot with up axis)
                    float v_up = vx*tare_up_x + vy*tare_up_y + vz*tare_up_z;
                    float tilt_deg = asinf(constrain(v_up, -1.0f, 1.0f))
                                     * (180.0f / (float)M_PI);
                    data_streams[TILT] = (tilt_deg + 90.0f) / 180.0f;

                    // Heading = azimuth in horizontal plane.
                    // Reference forward = twist axis projected horizontal at tare.
                    float n_up = nx*tare_up_x + ny*tare_up_y + nz*tare_up_z;
                    float fx = nx - n_up*tare_up_x;
                    float fy = ny - n_up*tare_up_y;
                    float fz = nz - n_up*tare_up_z;
                    float flen = sqrtf(fx*fx + fy*fy + fz*fz);
                    if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }
                    // Right = up × forward
                    float rx = tare_up_y*fz - tare_up_z*fy;
                    float ry = tare_up_z*fx - tare_up_x*fz;
                    float rz = tare_up_x*fy - tare_up_y*fx;
                    // Remove vertical component from v
                    float hx = vx - v_up*tare_up_x;
                    float hy = vy - v_up*tare_up_y;
                    float hz = vz - v_up*tare_up_z;
                    float heading_deg = atan2f(hx*rx + hy*ry + hz*rz,
                                               hx*fx + hy*fy + hz*fz)
                                        * (180.0f / (float)M_PI);
                    data_streams[HEADING] = (heading_deg + 180.0f) / 360.0f;
                }

                // ── Linear acceleration (gravity-free, m/s²) ──────────
                float ax, ay, az;
                imu_get_accel(ax, ay, az);

                const float ACCEL_SCALE = 4.0f;
                data_streams[ACCELX]      = constrain((ax / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELY]      = constrain((ay / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[ACCELZ]      = constrain((az / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                float accel_len = sqrtf(ax * ax + ay * ay + az * az);
                data_streams[ACCELLENGTH] = constrain(accel_len / ACCEL_SCALE, 0.0f, 1.0f);

                // ── Global-frame (rotation-compensated) acceleration ───
                // Rotate body-frame linear accel into global frame using
                // the quaternion: v_global = q * v_body * q^-1.
                float gax = (1.0f - 2.0f*(qj*qj + qk*qk))*ax + 2.0f*(qi*qj - qr*qk)*ay + 2.0f*(qi*qk + qr*qj)*az;
                float gay = 2.0f*(qi*qj + qr*qk)*ax + (1.0f - 2.0f*(qi*qi + qk*qk))*ay + 2.0f*(qj*qk - qr*qi)*az;
                float gaz = 2.0f*(qi*qk - qr*qj)*ax + 2.0f*(qj*qk + qr*qi)*ay + (1.0f - 2.0f*(qi*qi + qj*qj))*az;

                data_streams[GACCELX]      = constrain((gax / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GACCELY]      = constrain((gay / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GACCELZ]      = constrain((gaz / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GACCELLENGTH] = constrain(accel_len / ACCEL_SCALE, 0.0f, 1.0f);

                // ── Swing-twist frame acceleration ────────────────────
                // Project global-frame accel onto arm coordinate frame:
                //   armFwd  = along arm direction (swing-rotated twist axis)
                //   armLat  = perpendicular horizontal (right of arm)
                //   armVert = vertical (up axis)
                {
                    float avx, avy, avz;
                    rotate_vec(eq_i, eq_j, eq_k, eq_r,
                               twist_nx, twist_ny, twist_nz, avx, avy, avz);
                    float a_up = avx*tare_up_x + avy*tare_up_y + avz*tare_up_z;
                    // Arm forward (horizontal projection of arm direction)
                    float afx = avx - a_up*tare_up_x;
                    float afy = avy - a_up*tare_up_y;
                    float afz = avz - a_up*tare_up_z;
                    float aflen = sqrtf(afx*afx + afy*afy + afz*afz);
                    if (aflen > 1e-6f) { afx /= aflen; afy /= aflen; afz /= aflen; }
                    // Arm right = up × forward
                    float arx = tare_up_y*afz - tare_up_z*afy;
                    float ary = tare_up_z*afx - tare_up_x*afz;
                    float arz = tare_up_x*afy - tare_up_y*afx;
                    // Project global accel onto arm basis
                    float af = gax*afx + gay*afy + gaz*afz;
                    float ar = gax*arx + gay*ary + gaz*arz;
                    float av = gax*tare_up_x + gay*tare_up_y + gaz*tare_up_z;
                    data_streams[LIMB_FWD]   = constrain((af / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                    data_streams[LIMB_LAT]   = constrain((ar / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                    data_streams[LIMB_VERT]  = constrain((av / ACCEL_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                    float limb_len = sqrtf(af*af + ar*ar + av*av);
                    data_streams[TWITCH]     = constrain(limb_len / ACCEL_SCALE, 0.0f, 1.0f);
                }

                // ── Quaternion components (raw, untared) ───────────────
                // Normalised via *0.5+0.5 → [0,1].  Set low:-1 high:1 on the
                // OscMessage to recover the native [-1,1] quaternion range.
                data_streams[QUAT_I] = qi * 0.5f + 0.5f;
                data_streams[QUAT_J] = qj * 0.5f + 0.5f;
                data_streams[QUAT_K] = qk * 0.5f + 0.5f;
                data_streams[QUAT_R] = qr * 0.5f + 0.5f;

                // ── Gyroscope (rad/s) ─────────────────────────────────
                float gx, gy, gz;
                imu_get_gyro(gx, gy, gz);

                const float GYRO_SCALE = 4.0f;
                data_streams[GYROX]       = constrain((gx / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROY]       = constrain((gy / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                data_streams[GYROZ]       = constrain((gz / GYRO_SCALE) * 0.5f + 0.5f, 0.0f, 1.0f);
                float gyro_len = sqrtf(gx * gx + gy * gy + gz * gz);
                data_streams[GYROLENGTH]  = constrain(gyro_len / GYRO_SCALE, 0.0f, 1.0f);

                // ── Update orientation tracker ────────────────────────
                ot.update(qi, qj, qk, qr, gyro_len);

                // ── Ori-watch: push active-ori changes to status config ──
                if (ot.ori_watch_enabled) {
                    static String _watch_prev;
                    static unsigned long _watch_ms = 0;
                    String cur = ot.active_ori_name;
                    unsigned long now_w = millis();
                    if (cur != _watch_prev && now_w - _watch_ms >= 100) {
                        _watch_ms   = now_w;
                        _watch_prev = cur;
                        StatusReporter& sr = status_reporter();
                        if (sr.dest_port > 0) {
                            String watch_adr = "/reply" + device_adr + "/ori/active";
                            String payload   = cur.length() > 0 ? cur : "(none)";
                            xSemaphoreTake(osc_send_mutex(), portMAX_DELAY);
                            osc.setDestination(sr.dest_ip, sr.dest_port);
                            osc.sendString(watch_adr.c_str(), payload.c_str());
                            xSemaphoreGive(osc_send_mutex());
                        }
                    }
                }

                // ── Barometer — read from BMP5xx ──────────────────────
                // TODO: integrate barometer reading here if needed.
                data_streams[BARO] = 0.5f;
            } else {
                no_data_count++;
                if (no_data_count == 500) {
                    Serial.println(F("[IMU] WARNING: No sensor data for 5 seconds — check SPI wiring."));
                }
            }

            // ── Periodic heartbeat — print sensor summary every 10 s ──
            unsigned long now = millis();
            if (now - last_heartbeat_ms >= HEARTBEAT_INTERVAL_MS) {
                last_heartbeat_ms = now;
                Serial.print(F("[IMU] Heartbeat — reads: "));
                Serial.print(total_reads);
                Serial.print(F("  aX:"));
                Serial.print((float)data_streams[ACCELX], 3);
                Serial.print(F("  gX:"));
                Serial.print((float)data_streams[GYROX], 3);
                Serial.print(F("  eX:"));
                Serial.println((float)data_streams[ROLL], 3);
            }

            vTaskDelay(pdMS_TO_TICKS(10));  // ~100 Hz update rate
        }
    }, "sensor_task", 16384, nullptr, 1, nullptr);

    Serial.println(F("[BOOT] Sensor task started (LSM6DSV16XTR real data via SlimeIMU)."));
#endif // AB7_BUILD

    Serial.println();
    Serial.println(F("════════════════════════════════════════════════"));
    if (network_ready) {
#ifdef AB7_BUILD
        Serial.println(F("  TheaterGWD ab7 ready."));
#else
        Serial.println(F("  TheaterGWD ready."));
#endif
        Serial.print(F("  Listening for OSC on /annieData"));
        Serial.print(device_adr);
        Serial.println(F("/..."));
    } else {
#ifdef AB7_BUILD
        Serial.println(F("  TheaterGWD ab7 ready (no network)."));
#else
        Serial.println(F("  TheaterGWD ready (no network)."));
#endif
    }
    Serial.println(F("  Type 'help' in serial monitor for debug commands."));
#ifdef AB7_BUILD
    Serial.println(F("  BTN_A: add range point to selected ori  |  BTN_B: cycle to next ori"));
#endif
    Serial.println(F("════════════════════════════════════════════════"));
    Serial.println();
}

void loop() {
    // Process serial debug commands (non-blocking).
    serial_process();

    // Process any incoming OSC message.  MicroOsc calls osc_handle_message()
    // for each complete message received on the UDP port.
    if (network_ready) {
        osc.onOscMessageReceived(osc_handle_message);
    }

#ifdef AB7_BUILD
    // --- Button handling (ab7 only) -----------------------------------------
    //
    // Button A: add a range point to the currently selected ori.
    // Button B: cycle to the next ori (LED shows its color).
    //
    // Workflow: create oris via OSC, then use the buttons on-site to add
    // range samples without needing a computer.

    OriTracker& ot = ori_tracker();

    // Button A (GPIO 0):
    //   Short tap  (<300 ms): instant single-sample save into the selected ori.
    //   Hold       (≥300 ms): start a timed recording session (LED pulses red);
    //                         release → finalize (auto-axis detect + subsample).
    {
        bool a_low = (digitalRead(BTN_A) == LOW);

        // ── Press detection ──────────────────────────────────────────────────
        if (a_low && !btn_a_down
            && millis() - btn_a_last > BTN_DEBOUNCE_MS) {
            btn_a_down       = true;
            btn_a_pressed_at = millis();
            btn_a_held       = false;
        }

        // ── Hold threshold → start recording ────────────────────────────────
        if (btn_a_down && !btn_a_held
            && millis() - btn_a_pressed_at >= BTN_HOLD_MS) {
            btn_a_held = true;
            const SavedOri* sel = ot.selected_ori();
            if (sel) {
                if (ot.start_recording(sel->name)) {
                    Serial.println("[BTN_A] HOLD — recording started for '"
                                   + sel->name + "'");
                    status_reporter().info("ori", "Recording started for '"
                                          + sel->name + "'");
                } else {
                    Serial.println("[BTN_A] HOLD — already recording '"
                                   + ot.session.name + "'");
                }
            } else {
                Serial.println(F("[BTN_A] HOLD — no ori selected"));
                // Double red flash.
                leds[0] = CRGB(80, 0, 0); FastLED.show(); delay(80);
                leds[0] = CRGB(0,  0, 0); FastLED.show(); delay(60);
                leds[0] = CRGB(80, 0, 0); FastLED.show(); delay(80);
                leds[0] = CRGB(0, 20, 0); FastLED.show();
            }
        }

        // ── LED pulse while recording ────────────────────────────────────────
        if (btn_a_held && ot.session.active) {
            uint8_t pulse = ((millis() / 150) % 2 == 0) ? 60 : 0;
            leds[0] = CRGB(pulse, 0, 0);
            FastLED.show();
        }

        // ── Release ──────────────────────────────────────────────────────────
        if (!a_low && btn_a_down) {
            btn_a_down = false;
            btn_a_last = millis();

            if (btn_a_held) {
                // End of hold — finalize recording.
                btn_a_held = false;
                if (ot.session.active) {
                    String rec_name = ot.session.name;
                    int n = ot.stop_recording();
                    Serial.println("[BTN_A] Recording done: '" + rec_name
                                   + "', " + String(n) + " samples");
                    status_reporter().info("ori", "Recorded '" + rec_name
                                         + "' (" + String(n) + " samples)");
                    int idx = ot.find(rec_name);
                    if (idx >= 0) {
                        // Green flash.
                        leds[0] = CRGB(0, 80, 0);
                        FastLED.show();
                        delay(150);
                        leds[0] = CRGB(0, 0, 0);
                        FastLED.show();
                    }
                }
            } else {
                // Short tap — instant single-sample save.
                const SavedOri* sel = ot.selected_ori();
                if (sel) {
                    int idx = ot.save(sel->name, cur_qi, cur_qj, cur_qk, cur_qr);
                    if (idx >= 0) {
                        uint8_t sc = ot.oris[idx].sample_count;
                        Serial.println("[BTN_A] TAP — saved sample " + String(sc)
                                       + " to '" + sel->name + "'");
                        status_reporter().info("ori", "Saved sample to '"
                                             + sel->name + "' (" + String(sc) + ")");
                        // White flash.
                        leds[0] = CRGB(100, 100, 100);
                        FastLED.show();
                        delay(120);
                        leds[0] = CRGB(0, 0, 0);
                        FastLED.show();
                    }
                } else {
                    Serial.println(F("[BTN_A] TAP — no ori selected"));
                    // Double red flash.
                    leds[0] = CRGB(80, 0, 0); FastLED.show(); delay(80);
                    leds[0] = CRGB(0,  0, 0); FastLED.show(); delay(60);
                    leds[0] = CRGB(80, 0, 0); FastLED.show(); delay(80);
                    leds[0] = CRGB(0, 20, 0); FastLED.show();
                }
            }
        }
    }

    // Button B (GPIO 14): Cycle to the next ori (including unsampled slots).
    //   • Unsampled (pre-registered) slots: LED double-blinks the color to
    //     signal "not yet captured — press A to sample now".
    //   • Sampled slots: LED shows the color steadily (dimmed).
    if (digitalRead(BTN_B) == LOW && millis() - btn_b_last > BTN_DEBOUNCE_MS) {
        btn_b_last = millis();
        int idx = ot.select_next();
        if (idx >= 0) {
            const SavedOri& o = ot.oris[idx];
            if (o.sample_count == 0) {
                Serial.println("[BTN_B] Selected PENDING ori: '" + o.name
                    + "' (pre-registered, not yet sampled) — press A to capture");
                // Double-blink: signals "needs sampling".
                leds[0] = CRGB(40, 40, 40); FastLED.show(); delay(100);
                leds[0] = CRGB(0, 0, 0);    FastLED.show(); delay(80);
                leds[0] = CRGB(40, 40, 40); FastLED.show();
            } else {
                Serial.println("[BTN_B] Selected ori: '" + o.name
                    + "' (" + String(o.sample_count) + " samples)");
                // Steady dim white.
                leds[0] = CRGB(40, 40, 40);
                FastLED.show();
            }
        } else {
            Serial.println(F("[BTN_B] No oris. Register oris via Gooey first."));
            // Double red flash.
            leds[0] = CRGB(80, 0, 0); FastLED.show(); delay(80);
            leds[0] = CRGB(0,  0, 0); FastLED.show(); delay(60);
            leds[0] = CRGB(80, 0, 0); FastLED.show(); delay(80);
            leds[0] = CRGB(0, 20, 0); FastLED.show();
        }
    }
#endif // AB7_BUILD
}
#endif // PIO_UNIT_TESTING

