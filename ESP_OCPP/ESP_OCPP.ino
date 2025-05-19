#include <WiFi.h>
#include <MicroOcpp.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>

#define DHTPIN 33
//L247N278
#define DHTTYPE DHT11

#define SS_PIN 21    // SDA
#define RST_PIN 22   // RST
#define LED_PIN 2    // Optional: LED toggled when charging

MFRC522 rfid(SS_PIN, RST_PIN);
DHT dht(DHTPIN, DHTTYPE);

// Replace with your card's UID
byte validUID[4] = {0x21, 0xF5, 0xA2, 0x26};

const char* ssid = "Nothing";
const char* password = "123456789";
const char* ocppServer = "wss://ocppserver.myekigai.com/ESP32CP";

String inputData = "";
String idTag = "0123456789ABCD"; // This will be used for OCPP transaction

unsigned long lastDHTRead = 0;
const unsigned long dhtInterval = 10000; // 10 seconds

void setup() {
  Serial.begin(115200);
  dht.begin();

  SPI.begin(18, 19, 23);  // SCK, MISO, MOSI for ESP32
  rfid.PCD_Init();

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");

  // Initialize OCPP
  mocpp_initialize(ocppServer, "ESP32CP");
  Serial.println("MicroOcpp initialized");
  Serial.println("Scan your card...");
}

void loop() {
  mocpp_loop(); // MicroOcpp background tasks



  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }


  // LED shows charging state
  if (ocppPermitsCharge()) {
    digitalWrite(LED_PIN, HIGH); // EV plug energized
  } else {
    digitalWrite(LED_PIN, LOW);  // EV plug not energized
  }
  


    Serial.print("Card UID: ");
  for (byte i = 0; i < rfid.uid.size; i++) {
    Serial.print(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    Serial.print(rfid.uid.uidByte[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  // RFID card detection
 // if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    return;
  }


    if (isValidCard(rfid.uid.uidByte)) {
      Serial.println("Authorized card detected");

      if (!getTransaction()) {
        // No transaction active — Start new transaction
        Serial.printf("[main] Begin Transaction with idTag %s\n", idTag.c_str());
        auto ret = beginTransaction(idTag.c_str());

        if (ret) {
          Serial.println(F("[main] Transaction initiated. Waiting for connector plug + auth"));
            DHTRead();
        } else {
          Serial.println(F("[main] No transaction initiated"));
        }

      } else {
        // Transaction running — check if same card to stop it
        if (idTag.equals(getTransactionIdTag())) {
          Serial.println(F("[main] End transaction by RFID card"));
          endTransaction(idTag.c_str());
        } else {
          Serial.println(F("[main] Cannot end transaction by RFID card (different card?)"));
        }
      }

   } 
     else {
      Serial.println("Unauthorized card.");
    }
  

    delay(1500); // Debounce delay
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
//  }

  // Send DHT data only if charging is permitted
  if (ocppPermitsCharge() && millis() - lastDHTRead > dhtInterval) {
    lastDHTRead = millis();
    DHTRead();
  }

  // Read serial input and send to server if charging
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      if (ocppPermitsCharge()) {
        sendSerialDataToServer(inputData);
      }
      inputData = "";
    } else {
      inputData += c;
    }
  }
}

// Validate the scanned UID
bool isValidCard(byte *uid) {
  for (byte i = 0; i < 4; i++) {
    if (uid[i] != validUID[i]) {
      return false;
    }
  }
  return true;
}

// Read DHT and send to OCPP server
void DHTRead() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }

  Serial.print("Temperature: ");
  Serial.print(temperature);
  Serial.print(" °C, Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");

  sendDHTDataToServer(temperature, humidity);
}

// Send DHT data to OCPP server
void sendDHTDataToServer(float temperature, float humidity) {
  sendRequest("DataTransfer",
    [temperature, humidity]() {
      auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(256));
      JsonObject json = doc->to<JsonObject>();
      json["vendorId"] = "CustomClient";
      json["Temperature"] = temperature;
      json["Humidity"] = humidity;
      return doc;
    },
    [](JsonObject response) {
      Serial.println("Response from OCPP server:");
      serializeJson(response, Serial);
      Serial.println();
    }
  );
}

// Send custom serial data to OCPP server
void sendSerialDataToServer(String payload) {
  sendRequest("DataTransfer",
    [payload]() {
      auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(256));
      JsonObject json = doc->to<JsonObject>();
      json["vendorId"] = "CustomClient";
      json["data"] = payload;
      return doc;
    },
    [](JsonObject response) {
      Serial.println("Response from OCPP server:");
      serializeJson(response, Serial);
      Serial.println();
    }
  );
}
