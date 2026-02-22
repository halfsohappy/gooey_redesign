#include <MicroOsc.h>
#include <MicroOscUdp.h>
#include <WiFi.h>
#include <WiFiUdp.h>

String name = "/annieData/test/";
char ssid[] = "NETGEAR20";          // your network SSID (name)
char pass[] = "wateryshrub305";                    // your network password
IPAddress local_IP(192, 168, 1, 105);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);
const IPAddress out_ip(192,168,1,9);        // remote IP of your computer
const unsigned int out_port = 9999;          // remote port to receive OSC
const unsigned int local_port = 8888;        // local port to listen for OSC packets (actually not used for sending)

WiFiUDP Udp;                                // A UDP instance to let us send and receive packets over UDP


MicroOscUdp<1024> osc(&Udp, out_ip, out_port);

void begin_udp(){
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {delay(500); Serial.print(".");}

  Serial.println("WiFi connected, IP address:");
  Serial.println(WiFi.localIP());
  Serial.println("Starting UDP");
  Udp.begin(local_port);
  Serial.print("Local port: ");
  Serial.println(local_port);
}


void myOnOscMessageReceived( MicroOscMessage & oscMessage ) {



}

// bool accel_en;
// bool gyro_en;

// void myOnOscMessageReceived( MicroOscMessage & osc_msg ) {
//   if(osc_msg.checkOscAddress((name + String("/acceleration")).c_str(), "s")){
//     const char * boo = osc_msg.nextAsString();
//     if(strcmp(boo,"t")){accel_en = true;}
//     else if(strcmp(boo,"f")){accel_en = false;}
//   }
//   else if(osc_msg.checkOscAddress((name + String("/gyroscope")).c_str(), "s")){
//     const char * boo = osc_msg.nextAsString();
//     if(strcmp(boo,"t")){gyro_en = true;}
//     else if(strcmp(boo,"f")){gyro_en = false;}
//   }
//   else if(osc_msg.checkOscAddress((name + String("/punch")).c_str())){
//     punch.setRGB(osc_msg.nextAsInt(),osc_msg.nextAsInt(),osc_msg.nextAsInt());
//   }
// }



    //osc.sendFloat("/annieData/test/elevation", bmp.readAltitude(SEALEVELPRESSURE_HPA));
    //"/annieData/test/acceleration" XYZ (make -1 to 1)
    //"/annieData/test/gyroscope" XYZ    (make -1 to 1)
    //"/annieData/test/magnetometer" XYZ (make -1 to 1)
    //"/annieData/test/quaternion"       (make -1 to 1)
    //"/annieData/test/orientation" XYZ euler angles
    //