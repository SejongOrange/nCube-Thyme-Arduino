#include <Arduino.h>
#include <SPI.h>

/**
   Copyright (c) 2018, OCEAN
   All rights reserved.
   Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
   1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
   2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
   3. The name of the author may not be used to endorse or promote products derived from this software without specific prior written permission.
   THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <WiFi101.h>
#include <WiFiMDNSResponder.h>

#include "OneM2MClient.h"
#include "m0_ota.h"

const int ledPin = 13; // LED pin for connectivity status indicator

uint8_t USE_WIFI = 1;

#define WINC_CS   8
#define WINC_IRQ  7
#define WINC_RST  4
#define WINC_EN   2

#define WIFI_INIT 1
#define WIFI_CONNECT 2
#define WIFI_CONNECTED 3
#define WIFI_RECONNECT 4
uint8_t WIFI_State = WIFI_INIT;

unsigned long wifi_previousMillis = 0;
const long wifi_interval = 30; // count
const long wifi_led_interval = 100; // ms
uint16_t wifi_wait_count = 0;

unsigned long req_previousMillis = 0;
const long req_interval = 1500; // ms

unsigned long chk_previousMillis = 0;
const long chk_interval = 1000; // ms

#define UPLOAD_UPLOADING 2
#define UPLOAD_UPLOADED 3
unsigned long uploading_previousMillis = 0;
const long uploading_interval = 100; // ms
uint8_t UPLOAD_State = UPLOAD_UPLOADING;

// for MQTT
WiFiClient wifiClient;
PubSubClient mqtt;

char req_id[10];
String state = "create_ae";
uint8_t sequence = 0;

String strRef[8];
int strRef_length = 0;

#define QUEUE_SIZE 8
typedef struct _queue_t {
    uint8_t pop_idx;
    uint8_t push_idx;
    String ref[QUEUE_SIZE];
    String con[QUEUE_SIZE];
    String rqi[QUEUE_SIZE];
} queue_t;

queue_t noti_q;
queue_t upload_q;

// -----------------------------------------------------------------------------
// User Define
// Period of Sensor Data, can make more
unsigned long generate_previousMillis = 0;
const long generate_interval = 5000; // ms

// Information of CSE as Mobius with MQTT
const String FIRMWARE_VERSION = "1.0.0.0";
const String AE_NAME = "air0";
const String AE_ID = "S" + AE_NAME;
const String MOBIUS_MQTT_BROKER_IP = "203.253.128.161";
const uint16_t MOBIUS_MQTT_BROKER_PORT = 1883;

OneM2MClient nCube(MOBIUS_MQTT_BROKER_IP, MOBIUS_MQTT_BROKER_PORT, AE_ID); // AE-ID

// add TAS(Thing Adaptation Layer) for Sensor
#include "TasLED.h"

TasLED tasLed;

// build tree of resource of oneM2M
void buildResource() {
    nCube.configResource(2, "/Mobius", AE_NAME);                    // AE resource

    nCube.configResource(3, "/Mobius/"+AE_NAME, "update");          // Container resource
    nCube.configResource(3, "/Mobius/"+AE_NAME, "co2");             // Container resource
    nCube.configResource(3, "/Mobius/"+AE_NAME, "led");             // Container resource
    nCube.configResource(3, "/Mobius/"+AE_NAME, "temp");            // Container resource
    nCube.configResource(3, "/Mobius/"+AE_NAME, "tvoc");            // Container resource

    nCube.configResource(23, "/Mobius/"+AE_NAME+"/update", "sub");  // Subscription resource
    nCube.configResource(23, "/Mobius/"+AE_NAME+"/led", "sub");     // Subscription resource
}

// Period of generating sensor data
void generateProcess() {
    unsigned long currentMillis = millis();
    if (currentMillis - generate_previousMillis >= generate_interval) {
        generate_previousMillis = currentMillis;
        if (state == "create_cin") {
            rand_str(req_id, 8);
            int con = (double) rand() / RAND_MAX * 7;

            upload_q.ref[upload_q.push_idx] = "/Mobius/"+AE_NAME+"/led";
            upload_q.con[upload_q.push_idx] = String(con);
            upload_q.rqi[upload_q.push_idx] = String(req_id);
            upload_q.push_idx++;
            if(upload_q.push_idx >= QUEUE_SIZE) {
                upload_q.push_idx = 0;
            }
            if(upload_q.push_idx == upload_q.pop_idx) {
                upload_q.pop_idx++;
            }
        }
    }
}

// Process notification of Mobius for control
void notiProcess() {
    if(noti_q.pop_idx != noti_q.push_idx) {
        Split(noti_q.ref[noti_q.pop_idx], '/');
        if(strRef[strRef_length-1] == "led") {
            tasLed.setLED(noti_q.con[noti_q.pop_idx]);

            String resp_body = "";
            resp_body += "{\"rsc\":\"2000\",\"to\":\"\",\"fr\":\"" + nCube.getAeid() + "\",\"pc\":\"\",\"rqi\":\"" + noti_q.rqi[noti_q.pop_idx] + "\"}";
            nCube.response(resp_body);
        }
        else if(strRef[strRef_length-1] == "update") {
            if (noti_q.con[noti_q.pop_idx] == "active") {
                OTAClient.start();   // active OTAClient upgrad process

                String resp_body = "";
                resp_body += "{\"rsc\":\"2000\",\"to\":\"\",\"fr\":\"" + nCube.getAeid() + "\",\"pc\":\"\",\"rqi\":\"" + noti_q.rqi[noti_q.pop_idx] + "\"}";
                nCube.response(resp_body);
            }
        }

        noti_q.pop_idx++;
        if(noti_q.pop_idx >= QUEUE_SIZE) {
            noti_q.pop_idx = 0;
        }
    }
}
//------------------------------------------------------------------------------

void setup() {
    // configure the LED pin for output mode
    pinMode(ledPin, OUTPUT);

    //Initialize serial:
    Serial.begin(9600);

    noti_q.pop_idx = 0;
    noti_q.push_idx = 0;
    upload_q.pop_idx = 0;
    upload_q.push_idx = 0;

    WiFi_init();

    delay(1000);

    nCube.setCallback(resp_callback, noti_callback);

    delay(500);

    buildResource();
    //init OTA client
    OTAClient.begin(AE_NAME, FIRMWARE_VERSION);

    tasLed.init();
    delay(500);
    tasLed.setLED("0");
}

void loop() {
    WiFi_chkconnect();
    nCube.MQTT_chkconnect();

    chkState();
    publisher();
    notiProcess();
    otaProcess();
    generateProcess();
    uploadProcess();
}

void rand_str(char *dest, size_t length) {
    char charset[] = "0123456789"
            "abcdefghijklmnopqrstuvwxyz"
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

    while (length-- > 0) {
        size_t index = (double) rand() / RAND_MAX * (sizeof charset - 1);
        *dest++ = charset[index];
    }
    *dest = '\0';
}

void WiFi_init() {
    if(USE_WIFI) {
        // begin WiFi
        digitalWrite(ledPin, HIGH);
        WiFi.setPins(WINC_CS, WINC_IRQ, WINC_RST, WINC_EN);
        if (WiFi.status() == WL_NO_SHIELD) { // check for the presence of the shield:
            Serial.println("WiFi shield not present");
            // don't continue:
            while (true) {
                digitalWrite(ledPin, HIGH);
                delay(100);
                digitalWrite(ledPin, LOW);
                delay(100);
            }
        }
        digitalWrite(ledPin, LOW);
    }

    WIFI_State = WIFI_INIT;
}

void WiFi_chkconnect() {
    if(USE_WIFI) {
        if(WIFI_State == WIFI_INIT) {
            digitalWrite(ledPin, HIGH);

            Serial.println("beginProvision - WIFI_INIT");
            WiFi.beginProvision();

            WIFI_State = WIFI_CONNECT;
            wifi_previousMillis = 0;
            wifi_wait_count = 0;
            noti_q.pop_idx = 0;
            noti_q.push_idx = 0;
            upload_q.pop_idx = 0;
            upload_q.push_idx = 0;

        }
        else if(WIFI_State == WIFI_CONNECTED) {
            if (WiFi.status() == WL_CONNECTED) {
                return;
            }

            wifi_wait_count = 0;
            if(WIFI_State == WIFI_CONNECTED) {
                WIFI_State = WIFI_RECONNECT;
                wifi_previousMillis = 0;
                wifi_wait_count = 0;
                noti_q.pop_idx = 0;
                noti_q.push_idx = 0;
                upload_q.pop_idx = 0;
                upload_q.push_idx = 0;
            }
            else {
                WIFI_State = WIFI_CONNECT;
                wifi_previousMillis = 0;
                wifi_wait_count = 0;
                noti_q.pop_idx = 0;
                noti_q.push_idx = 0;
                upload_q.pop_idx = 0;
                upload_q.push_idx = 0;
            }
            nCube.MQTT_init();
        }
        else if(WIFI_State == WIFI_CONNECT) {
            unsigned long currentMillis = millis();
            if (currentMillis - wifi_previousMillis >= wifi_led_interval) {
                wifi_previousMillis = currentMillis;
                if(wifi_wait_count++ >= wifi_interval) {
                    wifi_wait_count = 0;
                    if (WiFi.status() != WL_CONNECTED) {
                        Serial.println("Provisioning......");
                    }
                }
                else {
                    if(wifi_wait_count % 2) {
                        digitalWrite(ledPin, HIGH);
                    }
                    else {
                        digitalWrite(ledPin, LOW);
                    }
                }
            }
            else {
                if (WiFi.status() == WL_CONNECTED) {
                    // you're connected now, so print out the status:
                    printWiFiStatus();

                    uint8_t mac[6];
                    WiFi.macAddress(mac);

                    PubSubClient _mqtt(wifiClient);

                    char ip[16];
                    MOBIUS_MQTT_BROKER_IP.toCharArray(ip, 16);

                    nCube.MQTT_ready(_mqtt, ip, MOBIUS_MQTT_BROKER_PORT, mac);

                    digitalWrite(ledPin, LOW);

                    WIFI_State = WIFI_CONNECTED;
                    wifi_previousMillis = 0;
                    wifi_wait_count = 0;
                    noti_q.pop_idx = 0;
                    noti_q.push_idx = 0;
                    upload_q.pop_idx = 0;
                    upload_q.push_idx = 0;
                }
            }
        }
        else if(WIFI_State == WIFI_RECONNECT) {
            digitalWrite(ledPin, HIGH);

            unsigned long currentMillis = millis();
            if (currentMillis - wifi_previousMillis >= wifi_led_interval) {
                wifi_previousMillis = currentMillis;
                if(wifi_wait_count++ >= wifi_interval) {
                    wifi_wait_count = 0;
                    if (WiFi.status() != WL_CONNECTED) {
                        Serial.print("Attempting to connect to SSID: ");
                        Serial.println("previous SSID");

                        WiFi.begin();
                    }
                }
                else {
                    if(wifi_wait_count % 2) {
                        digitalWrite(ledPin, HIGH);
                    }
                    else {
                        digitalWrite(ledPin, LOW);
                    }
                }
            }
            else {
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.println("Connected to wifi");
                    printWiFiStatus();

                    digitalWrite(ledPin, LOW);

                    WIFI_State = WIFI_CONNECTED;
                }
            }
        }
    }
}

void printWiFiStatus() {
    // print the SSID of the network you're attached to:
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());

    // print your WiFi shield's IP address:
    IPAddress ip = WiFi.localIP();
    Serial.print("IP Address: ");
    Serial.println(ip);

    // print the received signal strength:
    long rssi = WiFi.RSSI();
    Serial.print("signal strength (RSSI):");
    Serial.print(rssi);
    Serial.println(" dBm");
}

void otaProcess() {
    if(WIFI_State == WIFI_CONNECTED && nCube.MQTT_State == _MQTT_CONNECTED) {
        if (OTAClient.finished()) {
        }
        else {
            OTAClient.poll();
        }
    }
}

void Split(String sData, char cSeparator)
{
	int nCount = 0;
	int nGetIndex = 0 ;
    strRef_length = 0;

	String sTemp = "";
	String sCopy = sData;

	while(true) {
		nGetIndex = sCopy.indexOf(cSeparator);

		if(-1 != nGetIndex) {
			sTemp = sCopy.substring(0, nGetIndex);
			strRef[strRef_length++] = sTemp;
			sCopy = sCopy.substring(nGetIndex + 1);
		}
		else {
            strRef[strRef_length++] = sCopy;
			break;
		}
		++nCount;
	}
}

void resp_callback(String topic, JsonObject &root) {
    int response_code = root["rsc"];
    String request_id = String(req_id);
    String response_id = root["rqi"];

    Serial.println(response_code);

    if (request_id == response_id) {
        if (response_code == 2000 || response_code == 2001 || response_code == 2002 || response_code == 4105 || response_code == 4004) {
            if (state == "create_ae") {
                sequence++;
                if(sequence >= nCube.getAeCount()) {
                    state = "create_cnt";
                    sequence = 0;
                }
            }
            else if (state == "create_cnt") {
                sequence++;
                if(sequence >= nCube.getCntCount()) {
                    state = "delete_sub";
                    sequence = 0;
                }
            }
            else if(state == "delete_sub") {
                sequence++;
                if(sequence >= nCube.getSubCount()) {
                    state = "create_sub";
                    sequence = 0;
                }
            }
            else if (state == "create_sub") {
                sequence++;
                if(sequence >= nCube.getSubCount()) {
                    state = "create_cin";
                    sequence = 0;
                }
            }
        }
        digitalWrite(ledPin, LOW);
    }
}

void noti_callback(String topic, JsonObject &root) {
    if (state == "create_cin") {
        String sur = root["pc"]["m2m:sgn"]["sur"];
        Serial.println(sur);
        if(sur.charAt(0) != '/') {
            sur = '/' + sur;
            Serial.println(sur);
        }

        String valid_sur = nCube.validSur(sur);
        if (valid_sur != "empty") {
            const char *rqi = root["rqi"];
            String con = root["pc"]["m2m:sgn"]["nev"]["rep"]["m2m:cin"]["con"];

            noti_q.ref[noti_q.push_idx] = valid_sur;
            noti_q.con[noti_q.push_idx] = con;
            noti_q.rqi[noti_q.push_idx] = String(rqi);
            noti_q.push_idx++;
            if(noti_q.push_idx >= QUEUE_SIZE) {
                noti_q.push_idx = 0;
            }
            if(noti_q.push_idx == noti_q.pop_idx) {
                noti_q.pop_idx++;
            }
        }
    }
}

void publisher() {
    unsigned long currentMillis = millis();
    if (currentMillis - req_previousMillis >= req_interval) {
        req_previousMillis = currentMillis;

        if(WIFI_State == WIFI_CONNECTED && nCube.MQTT_State == _MQTT_CONNECTED) {
            if (state == "create_ae") {
                Serial.print(state + " - ");
                rand_str(req_id, 8);
                Serial.print(String(sequence));
                Serial.print(" - ");
                Serial.println(String(req_id));
                nCube.createAE(req_id, 0, "3.14");
                digitalWrite(ledPin, HIGH);
            }
            else if (state == "create_cnt") {
                Serial.print(state + " - ");
                rand_str(req_id, 8);
                Serial.print(String(sequence));
                Serial.print(" - ");
                Serial.println(String(req_id));
                nCube.createCnt(req_id, sequence);
                digitalWrite(ledPin, HIGH);
            }
            else if (state == "delete_sub") {
                Serial.print(state + " - ");
                rand_str(req_id, 8);
                Serial.print(String(sequence));
                Serial.print(" - ");
                Serial.println(String(req_id));
                nCube.deleteSub(req_id, sequence);
                digitalWrite(ledPin, HIGH);
            }
            else if (state == "create_sub") {
                Serial.print(state + " - ");
                rand_str(req_id, 8);
                Serial.print(String(sequence));
                Serial.print(" - ");
                Serial.println(String(req_id));
                nCube.createSub(req_id, sequence);
                digitalWrite(ledPin, HIGH);
            }
            else if (state == "create_cin") {
                //Serial.print(state + " - ");
            }
        }
    }
}

void chkState() {
    unsigned long currentMillis = millis();
    if (currentMillis - chk_previousMillis >= chk_interval) {
        chk_previousMillis = currentMillis;

        if(WIFI_State == WIFI_CONNECT) {
            Serial.println("WIFI_CONNECT");
        }
        else if(WIFI_State == WIFI_RECONNECT) {
            Serial.println("WIFI_RECONNECT");
        }

        if(nCube.MQTT_State == _MQTT_CONNECT) {
            Serial.println("_MQTT_CONNECT");
        }
    }
}

void uploadProcess() {
    if(WIFI_State == WIFI_CONNECTED && nCube.MQTT_State == _MQTT_CONNECTED) {
        unsigned long currentMillis = millis();
        if (currentMillis - uploading_previousMillis >= uploading_interval) {
            uploading_previousMillis = currentMillis;

            if (state == "create_cin") {
                UPLOAD_State = UPLOAD_UPLOADED;
            }
        }

        if((UPLOAD_State == UPLOAD_UPLOADED) && (upload_q.pop_idx != upload_q.push_idx)) {
            nCube.createCin(upload_q.rqi[upload_q.pop_idx], upload_q.ref[upload_q.pop_idx], upload_q.con[upload_q.pop_idx]);
            digitalWrite(ledPin, HIGH);

            uploading_previousMillis = currentMillis;
            UPLOAD_State = UPLOAD_UPLOADING;

            upload_q.pop_idx++;
            if(upload_q.pop_idx >= QUEUE_SIZE) {
                upload_q.pop_idx = 0;
            }
        }
    }
}