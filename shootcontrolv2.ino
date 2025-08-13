#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <Ticker.h>
 
// Access Point credentials
const char* ap_ssid = "D1Mini_AP";
const char* ap_password = "12345678"; // Password for AP
 
ESP8266WebServer server(80);
 
struct PinActions {
    uint8_t pin;
    int actionIndex;
    int numActions;
    Ticker ticker;
    const int* actions;
};
 
void executeAction(PinActions* pinActions) {
    while (pinActions->actionIndex < pinActions->numActions) {
        int currentAction = pinActions->actions[pinActions->actionIndex];
 
        if (currentAction == 0 || currentAction == 1) {
            digitalWrite(pinActions->pin, currentAction);
            Serial.print("Pin ");
            Serial.print(pinActions->pin);
            Serial.print(" set to ");
            Serial.println(currentAction == 1 ? "HIGH" : "LOW");
            pinActions->actionIndex++;
        } else if (currentAction > 1) {
            Serial.print("Pin ");
            Serial.print(pinActions->pin);
            Serial.print(" delay for ");
            Serial.print(currentAction);
            Serial.println(" ms");
            pinActions->ticker.once_ms(currentAction, executeAction, pinActions);
            pinActions->actionIndex++;
            return;
        }
    }
}
 
void handleRoot() {
    server.send(200, "text/plain", "Send a POST request with JSON to /save, or GET /load?id=<id>, /run?id=<id>");
}
 
void handleNotFound() {
    server.send(404, "text/plain", "404: Not Found");
}
 
// Function to find the next available ID
int findNextId() {
    int maxId = 0;
 
    // Open the directory
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
        String filename = dir.fileName();
        if (filename.startsWith("/config_") && filename.endsWith(".json")) {
            // Extract the ID from the filename
            int id = filename.substring(8, filename.length() - 5).toInt(); // Extract ID
            if (id > maxId) {
                maxId = id;
            }
        }
    }
 
    return maxId + 1; // Return the next available ID
}
 
void handleSave() {
    if (server.hasArg("plain") == false) {
        server.send(400, "text/plain", "400: Invalid Request");
        return;
    }
 
    String json = server.arg("plain");
    Serial.println("Received JSON:");
    Serial.println(json);
 
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, json);
 
    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        server.send(400, "text/plain", "400: Bad JSON Format");
        return;
    }
 
    // Retrieve id and name
    int id = doc["id"];
    const char* name = doc["name"];
    Serial.print("ID: ");
    Serial.println(id);
    Serial.print("Name: ");
    Serial.println(name);
 
    // Assign a new ID if the provided ID is 0
    if (id == 0) {
        id = findNextId();
        doc["id"] = id; // Update the document with the new ID
    }
    // Save the JSON file
    String filename = "/config_" + String(id) + ".json";
    File file = SPIFFS.open(filename, "w");
    if (!file) {
        server.send(500, "text/plain", "500: Failed to open file for writing");
        return;
    }
 
    serializeJson(doc, file);
    file.close();
 
    // Convert the saved JSON document back to a string
    String savedJson;
    serializeJson(doc, savedJson);
 
    server.send(200, "application/json", savedJson);
}
 
void handleList() {
    Serial.println("Listing all saved configurations...");
 
    // Open the root directory for reading
    Dir dir = SPIFFS.openDir("/");
 
    //This does not work since it misses one file..
    // if (!dir.next()) {
    //     Serial.println("Error: Failed to open root directory or no files found.");
    //     server.send(500, "text/plain", "Failed to open root directory or no files found");
    //     return;
    // }
 
    StaticJsonDocument<1024> doc;
    JsonArray array = doc.to<JsonArray>();
 
    while (dir.next()) {
        String filename = dir.fileName();
        Serial.print("Checking file: ");
        Serial.println(filename);
 
        if (filename.startsWith("/config_") && filename.endsWith(".json")) {
            Serial.println("Configuration file found, parsing...");
 
            File file = dir.openFile("r");
            StaticJsonDocument<2048> configDoc;
            DeserializationError error = deserializeJson(configDoc, file);
            file.close();
 
            if (error) {
                Serial.print("Error: Failed to parse JSON in file ");
                Serial.print(filename);
                Serial.print(": ");
                Serial.println(error.c_str());
                continue; // Skip to the next file
            }
 
            JsonObject obj = configDoc.as<JsonObject>();
            JsonObject entry = array.createNestedObject();
            entry["id"] = obj["id"];
            entry["name"] = obj["name"];
 
            Serial.print("Configuration ID: ");
            Serial.print(obj["id"].as<int>());
            Serial.print(", Name: ");
            Serial.println(obj["name"].as<const char*>());
        }
    }
 
    // If no configurations were found, notify the user
    if (array.size() == 0) {
        Serial.println("No valid configuration files found.");
        server.send(404, "application/json", "{\"status\": \"No configurations found.\"}");
        return;
    }
 
    // Serialize and send the JSON response
    String output;
    serializeJson(doc, output);
    Serial.println("Sending configuration list to client:");
    Serial.println(output);
    server.send(200, "application/json", output);
}
 
void handleLoad() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "400: Missing id parameter");
        return;
    }
 
    int id = server.arg("id").toInt();
    String filename = "/config_" + String(id) + ".json";
    if (!SPIFFS.exists(filename)) {
        server.send(404, "text/plain", "404: Configuration not found");
        return;
    }
 
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        server.send(500, "text/plain", "500: Failed to open file for reading");
        return;
    }
 
    String json = file.readString();
    file.close();
 
    server.send(200, "application/json", json);
}
 
void handleDelete() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "400: Missing id parameter");
        return;
    }
 
    int id = server.arg("id").toInt();
    String filename = "/config_" + String(id) + ".json";
    if (!SPIFFS.exists(filename)) {
        server.send(404, "text/plain", "404: Configuration not found");
        return;
    }
 
    SPIFFS.remove(filename);
    server.send(200, "application/json", "{\"status\": \"OK\", \"message\": \"Configuration deleted.\"}");
}
 
void handleRun() {
    if (!server.hasArg("id")) {
        server.send(400, "text/plain", "400: Missing id parameter");
        return;
    }
 
    int id = server.arg("id").toInt();
    String filename = "/config_" + String(id) + ".json";
    if (!SPIFFS.exists(filename)) {
        server.send(404, "text/plain", "404: Configuration not found");
        return;
    }
 
    File file = SPIFFS.open(filename, "r");
    if (!file) {
        server.send(500, "text/plain", "500: Failed to open file for reading");
        return;
    }
 
    String json = file.readString();
    file.close();
 
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, json);
 
    if (error) {
        Serial.print("Failed to parse JSON: ");
        Serial.println(error.c_str());
        server.send(500, "text/plain", "500: Failed to parse stored configuration");
        return;
    }
 
    JsonArray pins = doc["pinConfigurations"].as<JsonArray>();
 
    for (JsonObject pinConfig : pins) {
        int pin = pinConfig["pin"].as<int>();
        JsonArray actionsArray = pinConfig["actions"].as<JsonArray>();
 
        Serial.print("Running actions for pin ");
        Serial.println(pin);
 
        int numActions = actionsArray.size();
        int* actions = new int[numActions];
        int i = 0;
 
        for (JsonObject action : actionsArray) {
            if (action.containsKey("action")) {
                int actionValue = action["action"];
                actions[i] = (actionValue == 1) ? 0 : 1; // Invert the action value, for the current relay to work as expected
                Serial.print("Action ");
                Serial.print(i);
                Serial.print(": Set pin ");
                Serial.print(pin);
                Serial.print(" to ");
                Serial.println(actions[i] == 1 ? "HIGH" : "LOW");
            } else if (action.containsKey("delay")) {
                actions[i] = action["delay"];
                Serial.print("Action ");
                Serial.print(i);
                Serial.print(": Delay for ");
                Serial.print(actions[i]);
                Serial.println(" ms");
            }
            i++;
        }
 
        PinActions* pinActions = new PinActions {
            (uint8_t)pin,
            0,
            numActions,
            Ticker(),
            actions
        };
       
        digitalWrite(pinActions->pin, 1); //Initilize to high beacouse of how the relay board is built.
        pinMode(pinActions->pin, OUTPUT);
        executeAction(pinActions);
    }
 
    server.send(200, "application/json", "{\"status\": \"OK\", \"message\": \"Configuration executed.\"}");
}
 
void setup() {
    Serial.begin(74880);
 
    // Initialize SPIFFS
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }
 
    // Set up Access Point
    WiFi.softAP(ap_ssid, ap_password);
 
    IPAddress IP = WiFi.softAPIP();
    Serial.print("Access Point IP Address: ");
    Serial.println(IP);
 
    // Set up server routes
    server.on("/", HTTP_GET, handleRoot);
    server.on("/list", HTTP_GET, handleList);
    server.on("/save", HTTP_POST, handleSave);
    server.on("/load", HTTP_GET, handleLoad);
    server.on("/delete", HTTP_GET, handleDelete);
    server.on("/run", HTTP_GET, handleRun);
    server.onNotFound(handleNotFound);
 
    server.begin();
    Serial.println("HTTP server started");
}
 
void loop() {
    server.handleClient();
}
 
 