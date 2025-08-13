#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>

// =========================
// Firmware info
// =========================
#define FW_VERSION "D1Mini-AP-commands v1.1 (2025-08-12)"

// =========================
// AP Settings
// =========================
const char* ssid = "D1MiniAP";
const char* password = "12345678";

ESP8266WebServer server(80);

// Define a struct to hold the command and its timing
struct Command {
  int pin;
  int command;
  unsigned long duration;
  unsigned long startTime;
  bool inProgress;
};

// =========================
// Pin mapping
// JSON pin numbers -> D-labels on Wemos D1 Mini -> GPIO
//
//  1 -> D1 -> GPIO5
//  2 -> D2 -> GPIO4
//  3 -> D3 -> GPIO0
//  4 -> D4 -> GPIO2
//  5 -> D5 -> GPIO14   (extra, ifall du vill använda fler)
//  6 -> D6 -> GPIO12
//  7 -> D7 -> GPIO13
//  8 -> D8 -> GPIO15
// Default: returnera originalvärdet (om du sätter GPIO direkt i JSON)
// =========================
int mapPinNumber(int jsonPin) {
  switch (jsonPin) {
    case 1: return 5;   // D1
    case 2: return 4;   // D2
    case 3: return 0;   // D3
    case 4: return 2;   // D4
    case 5: return 14;  // D5
    case 6: return 12;  // D6
    case 7: return 13;  // D7
    case 8: return 15;  // D8
    default: return jsonPin; // Om du redan anger GPIO direkt
  }
}

void handlePostRequest();
void handleRunRequest();
void saveData(const char* filename, const String& data);
void runProgram(int programId);
String loadData(const char* filename);

void setup() {
  Serial.begin(74880);
  delay(50);
  Serial.println();
  Serial.println(FW_VERSION);

  // Initialize SPIFFS (Storage)
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  // Set up the AP mode
  WiFi.softAP(ssid, password);
  Serial.println("Access Point started");

  // Routes
  server.on("/program", HTTP_POST, handlePostRequest);
  server.on("/run", HTTP_GET, handleRunRequest);

  // Firmware version endpoint
  server.on("/version", HTTP_GET, []() {
    server.send(200, "text/plain", FW_VERSION);
  });

  // Start the server
  server.begin();
  Serial.println("Server started");
}

void loop() {
  // Handle incoming clients
  server.handleClient();
}

void handlePostRequest() {
  if (server.hasArg("id")) {
    int programId = server.arg("id").toInt();

    String jsonString = server.arg("plain");
    Serial.println("Received JSON: " + jsonString);

    // Parse the JSON (validering)
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, jsonString);
    if (error) {
      Serial.print("JSON Parsing failed: ");
      Serial.println(error.c_str());
      server.send(400, "text/plain", "Invalid JSON");
      return;
    }

    // Filnamn: /<id>.json
    String filename = "/" + String(programId) + ".json";

    // Spara JSON i SPIFFS
    saveData(filename.c_str(), jsonString);

    // Svar
    server.send(200, "text/plain", "Commands executed and saved");
    return;
  } else {
    server.send(400, "text/plain", "Missing program parameter");
    return;
  }
}

void handleRunRequest() {
  if (server.hasArg("program")) {
    int programId = server.arg("program").toInt();

    if (programId == 0) {
      // Specialprogram: toggla D1..D4 via mappning (1..4)
      int pins[] = {
        mapPinNumber(1), // D1 -> GPIO5
        mapPinNumber(2), // D2 -> GPIO4
        mapPinNumber(3), // D3 -> GPIO0
        mapPinNumber(4)  // D4 -> GPIO2
      };

      for (int i = 0; i < 4; i++) {
        pinMode(pins[i], OUTPUT); // säkerställ output
        int pinState = digitalRead(pins[i]);  // läs current state
        digitalWrite(pins[i], !pinState);     // invertera state
      }
      server.send(200, "text/plain", "Program " + String(programId) + " executed");
      return;
    }

    // Kör sparat program
    runProgram(programId);
    server.send(200, "text/plain", "Program " + String(programId) + " executed");
  } else {
    server.send(400, "text/plain", "Missing program parameter");
  }
}

void saveData(const char* filename, const String& data) {
  File file = SPIFFS.open(filename, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  file.println(data);
  file.close();
  Serial.println("Data saved to " + String(filename));
}

void runProgram(int programId) {
  // Generate the filename based on the program ID
  String filename = "/" + String(programId) + ".json";

  // Load the saved JSON data
  String savedJson = loadData(filename.c_str());

  if (savedJson.length() > 0) {
    // Parse and run the saved JSON commands
    Serial.println("Data:");
    Serial.println(savedJson);
    StaticJsonDocument<1024> doc; // buffert
    DeserializationError error = deserializeJson(doc, savedJson);

    if (!error) {
      JsonArray pinArray = doc.as<JsonArray>();

      // Create an array to hold the state of each pin
      struct PinState {
        int pin; // <- detta blir GPIO efter mappning
        int currentCommandIndex;
        unsigned long lastCommandTime;
        unsigned long commandDuration;
        bool isFinished;
      };

      PinState pinStates[pinArray.size()];

      // Initiera pin-states
      int index = 0;
      for (JsonObject pinObj : pinArray) {
        int requestedPin = pinObj["pin"];           // JSON-värde (1..8 eller GPIO)
        int mappedPin    = mapPinNumber(requestedPin); // mappa till GPIO

        pinStates[index].pin = mappedPin;
        digitalWrite(pinStates[index].pin, HIGH);   // start HIGH (aktiv-låg reläkort)
        pinMode(pinStates[index].pin, OUTPUT);
        pinStates[index].currentCommandIndex = 0;
        pinStates[index].lastCommandTime = millis();
        pinStates[index].commandDuration = 0;
        pinStates[index].isFinished = false;
        index++;
      }

      // Kör kommandon parallellt
      while (true) {
        bool allPinsFinished = true;

        for (int i = 0; i < pinArray.size(); i++) {
          if (pinStates[i].isFinished) {
            continue;
          }

          JsonObject pinObj = pinArray[i];
          JsonArray commands = pinObj["commands"];

          if (pinStates[i].currentCommandIndex < commands.size()) {
            JsonObject command = commands[pinStates[i].currentCommandIndex];
            int duration = command["duration"];
            int cmd = command["command"];
            cmd = !cmd; // invertera (aktiv-låg)

            unsigned long currentTime = millis();

            // Check if it's time to update the pin state
            if (currentTime - pinStates[i].lastCommandTime >= pinStates[i].commandDuration) {
              digitalWrite(pinStates[i].pin, cmd);
              Serial.print("Pin ");
              Serial.print(pinStates[i].pin);
              Serial.print(" set to ");
              Serial.println(cmd);

              pinStates[i].commandDuration = duration * 1000UL;
              pinStates[i].lastCommandTime = currentTime;

              // Move to the next command
              pinStates[i].currentCommandIndex++;

              if (pinStates[i].currentCommandIndex >= commands.size()) {
                // Vänta ut sista duration innan vi markerar klar
                if (currentTime - pinStates[i].lastCommandTime >= pinStates[i].commandDuration) {
                  pinStates[i].isFinished = true;
                }
              }
            } else {
              allPinsFinished = false; // Wait for the current command to finish
            }
          }

          // Ensure the loop does not exit before the last command is fully processed
          if ((millis() - pinStates[i].lastCommandTime) < pinStates[i].commandDuration) {
            allPinsFinished = false;
          }
        }

        if (allPinsFinished) {
          break;  // Exit the loop if all pins have finished their commands
        }

        delay(10);  // Small delay to prevent the loop from running too fast
      }

      Serial.println("Program execution completed.");
      Serial.println("Waiting 3 seconds then resetting pins");
      delay(3000);

      // Reset pins till HIGH
      for (int i = 0; i < pinArray.size(); i++) {
        digitalWrite(pinStates[i].pin, HIGH);
        Serial.print("Pin ");
        Serial.print(pinStates[i].pin);
        Serial.println(" set to HIGH");
      }

      Serial.println("Program execution completed.");
    } else {
      Serial.println("Failed to parse saved JSON");
    }
  } else {
    Serial.println("No saved program found");
  }
}

String loadData(const char* filename) {
  File file = SPIFFS.open(filename, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return "";
  }

  String data = file.readString();  // Read the entire file content
  file.close();
  return data;
}
