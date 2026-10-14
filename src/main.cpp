#define ENABLE_FIRESTORE
#define ENABLE_USER_AUTH

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <DHT.h>
#include <FirebaseClient.h>

// ===============================
// WIFI
// ===============================
#define WIFI_SSID "Mila Nur Alwiyyah"
#define WIFI_PASSWORD "23112017"

// ===============================
// FIREBASE
// ===============================
#define API_KEY "AIzaSyCAVMntYMGFb0KUkUVq-qxv5Z4W0WdvLXg"

#define USER_EMAIL "esp32@fire-smart-alarm.com"
#define USER_PASSWORD "Firesmartalarm"

#define FIREBASE_PROJECT_ID "fire-smart-alarm"

// ===============================
// SENSOR
// ===============================
#define DHT_PIN 4
#define DHT_TYPE DHT22

// MQ-2 menggunakan AO
#define MQ2_AO 34

// ===============================
// LED & BUZZER
// ===============================
#define LED_RED 25
#define LED_YELLOW 26
#define LED_GREEN 27
#define BUZZER_PIN 13

DHT dht(DHT_PIN, DHT_TYPE);

// ===============================
// FIREBASE OBJECT
// ===============================
UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;

WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);

Firestore::Documents Docs;

// ===============================
// KONTROL ALARM
// ===============================
bool alarmEnabled = true;
unsigned long lastAlarmCheck = 0;
const unsigned long alarmCheckInterval = 3000;


// ===============================
// SETUP
// ===============================
void setup()
{
    Serial.begin(115200);
    delay(1000);

    dht.begin();

    pinMode(LED_RED, OUTPUT);
    pinMode(LED_YELLOW, OUTPUT);
    pinMode(LED_GREEN, OUTPUT);
    pinMode(BUZZER_PIN, OUTPUT);

    // Kondisi awal
    digitalWrite(LED_RED, LOW);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(BUZZER_PIN, LOW);

    Serial.println();
    Serial.println("=== FIRE SMART ALARM ===");
    Serial.println("Menghubungkan ke Wi-Fi...");

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Wi-Fi berhasil terhubung!");

    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    // Sinkronisasi waktu
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");

    Serial.println("Mengatur waktu...");

    struct tm timeinfo;

    while (!getLocalTime(&timeinfo))
    {
        Serial.print(".");
        delay(500);
    }

    Serial.println();
    Serial.println("Waktu berhasil disinkronkan.");

    // SSL
    ssl_client.setInsecure();

    Serial.println("Menginisialisasi Firebase...");

    initializeApp(
        aClient,
        app,
        getAuth(user_auth)
    );

    app.getApp<Firestore::Documents>(Docs);

    Serial.println("Menunggu Firebase siap...");
}



void checkAlarmControl()
{
    if (millis() - lastAlarmCheck < alarmCheckInterval)
    {
        return;
    }

    lastAlarmCheck = millis();

    String response = Docs.get(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        "alarm-control/esp32-lab",
        GetDocumentOptions()
    );

    Serial.println("Membaca kontrol alarm...");

    int fieldPos = response.indexOf("\"alarmEnabled\"");
    int valuePos = response.indexOf("\"booleanValue\"", fieldPos);

    if (fieldPos >= 0 && valuePos >= 0)
    {
        int truePos = response.indexOf("true", valuePos);
        int falsePos = response.indexOf("false", valuePos);

        if (truePos >= 0 && (falsePos < 0 || truePos < falsePos))
        {
            alarmEnabled = true;
        }
        else if (falsePos >= 0)
        {
            alarmEnabled = false;
        }

        Serial.print("Alarm enabled: ");
        Serial.println(alarmEnabled ? "true" : "false");
    }
    else
    {
        Serial.println("Nilai alarmEnabled belum ditemukan.");
        Serial.println(response);
    }
}


// ===============================
// LOOP
// ===============================
void loop()
{
    app.loop();

    Docs.loop();

    if (!app.ready())
    {
        return;
    }

    checkAlarmControl();

    // ===============================
    // BACA SENSOR
    // ===============================

    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();

    int smokeValue = analogRead(MQ2_AO);

    if (isnan(temperature) || isnan(humidity))
    {
        Serial.println("Gagal membaca DHT!");
        delay(2000);
        return;
    }

String status;


if (smokeValue < 430)
{
    status = "Normal";

    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_YELLOW, LOW);
    digitalWrite(LED_RED, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}
else if (smokeValue < 500)
{
    status = "Warning";

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, HIGH);
    digitalWrite(LED_RED, LOW);
    digitalWrite(BUZZER_PIN, LOW);
}
else
{
    status = "Danger";

    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_YELLOW, LOW);

    if (alarmEnabled)
    {
        // Alarm aktif: LED merah dan buzzer ON
        digitalWrite(LED_RED, HIGH);
        digitalWrite(BUZZER_PIN, HIGH);
    }
    else
    {
        // Alarm dibungkam: LED merah dan buzzer OFF
        digitalWrite(LED_RED, LOW);
        digitalWrite(BUZZER_PIN, LOW);
    }
}


    // ===============================
    // DOCUMENT ID
    // ===============================

    time_t now = time(nullptr);

    String documentId =
        "ESP32_001_" + String((unsigned long)now);

    String documentPath =
        "fire-activity/" + documentId;

    // ===============================
    // TIMESTAMP
    // ===============================

    struct tm timeinfo;

    gmtime_r(&now, &timeinfo);

    char timestamp[30];

    strftime(
        timestamp,
        sizeof(timestamp),
        "%Y-%m-%dT%H:%M:%SZ",
        &timeinfo
    );

    // ===============================
    // FIRESTORE VALUES
    // ===============================

    Values::StringValue deviceId("ESP32_001");

    // Nilai AO mentah dari MQ-2
    Values::DoubleValue smoke(smokeValue);

    Values::DoubleValue temp(temperature);

    Values::DoubleValue hum(humidity);

    Values::StringValue stat(status);

    Values::TimestampValue ts(timestamp);

    // ===============================
    // BUAT DOCUMENT
    // ===============================

    Document<Values::Value> doc(
        "deviceId",
        Values::Value(deviceId)
    );

    doc.add(
        "smoke",
        Values::Value(smoke)
    );

    doc.add(
        "temperature",
        Values::Value(temp)
    );

    doc.add(
        "humidity",
        Values::Value(hum)
    );

    doc.add(
        "status",
        Values::Value(stat)
    );

    doc.add(
        "timestamp",
        Values::Value(ts)
    );

    // ===============================
    // SERIAL MONITOR
    // ===============================

    Serial.println();
    Serial.println("==============================");
    Serial.println("DATA SENSOR");
    Serial.println("==============================");

    Serial.print("Suhu       : ");
    Serial.print(temperature);
    Serial.println(" °C");

    Serial.print("Kelembapan : ");
    Serial.print(humidity);
    Serial.println(" %");

    Serial.print("MQ-2 AO    : ");
    Serial.println(smokeValue);

    Serial.print("Status     : ");
    Serial.println(status);

    Serial.print("Document ID: ");
    Serial.println(documentId);

    Serial.println("Mengirim data ke Firestore...");

    // ===============================
    // KIRIM FIRESTORE
    // ===============================

    String payload = Docs.createDocument(
        aClient,
        Firestore::Parent(FIREBASE_PROJECT_ID),
        documentPath,
        DocumentMask(),
        doc
    );

    if (aClient.lastError().code() == 0)
    {
        Serial.println(
            "Data berhasil dikirim ke Firestore!"
        );
    }
    else
    {
        Serial.print(
            "Gagal mengirim data: "
        );

        Serial.println(
            aClient.lastError().message()
        );
    }

    delay(5000);
}