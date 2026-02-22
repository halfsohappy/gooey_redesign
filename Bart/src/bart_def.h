#define SDO 37
#define SCK 36
#define SDI 35
#define CS_IMU 42
#define CS_MAG 39
#define CS_UWB 38
#define CS_BAR 48
#define INT_IMU 41
#define INT_BAR 34
#define NEO 21
#define UMON 7
#define BMON 8
#define SEL13 11
#define SEL46 12
#define CC_EN1 13
#define CC_EN2 14
#define CC_PWM1 9
#define CC_PWM2 10

void begin_pins(bool b13, bool b46, bool cen1, bool cen2){
  pinMode(CS_MAG, OUTPUT); pinMode(CS_IMU, OUTPUT);
  pinMode(SEL13, OUTPUT); pinMode(SEL46, OUTPUT);
  pinMode(CC_EN1, OUTPUT);pinMode(CC_EN2, OUTPUT);
  digitalWrite(SEL13, b13); digitalWrite(SEL46, b46);
  digitalWrite(CC_EN1, cen1); digitalWrite(CC_EN2, cen2);
}