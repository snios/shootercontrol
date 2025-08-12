#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>


// AP Settings
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

void setup() {
  Serial.begin(74880);

  // Initialize SPIFFS (Storage)
  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }
  
  // Set up the AP mode
  WiFi.softAP(ssid, password);
  Serial.println("Access Point started");

  // Define a route that handles POST requests
  server.on("/program", HTTP_POST, handlePostRequest);
  
  // Define the route to run the saved program
  server.on("/run", HTTP_GET, handleRunRequest);
  
  // Start the server
  server.begin();
  Serial.println("Server started");

  // This is done based on program beeing run now..
  //pinMode(5, OUTPUT);
  //pinMode(4, OUTPUT);
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

  // Parse the JSON
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error) {
    Serial.print("JSON Parsing failed: ");
    Serial.println(error.c_str());
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }
  // Get the program ID from the URL path
  //String programId = server.uri().substring(server.uri().lastIndexOf("/") + 1);
  String filename = "/" + String(programId) + ".json";

  // Save the JSON data to SPIFFS with the generated filename
  saveData(filename.c_str(), jsonString);

  // Send a response back
  server.send(200, "text/plain", "Commands executed and saved");
  return;
  } else {
    server.send(400, "text/plain", "Missing program parameter");
    return;
  }
 
}

void handleRunRequest() {
  // Get the pin parameter from the query string
  if (server.hasArg("program")) {
    int programId = server.arg("program").toInt();

    if (programId == 0) {
      int pins[] = {5, 4, 0, 2};  // GPIO numbers for D1, D2, D3, D4

      for (int i = 0; i < 4; i++) {
        pinMode(pins[i], OUTPUT); // Ensure the pin is set as output
        int pinState = digitalRead(pins[i]);  // Read the current state of the pin
        digitalWrite(pins[i], !pinState);     // Invert the pin state
      }
      server.send(200, "text/plain", "Program " + String(programId) + " executed");
      return;
    }

    // Run the saved program for the specified pin
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
    StaticJsonDocument<1024> doc;  // Increased buffer size to accommodate larger JSON data
    DeserializationError error = deserializeJson(doc, savedJson);

    if (!error) {
      JsonArray pinArray = doc.as<JsonArray>();

      // Create an array to hold the state of each pin
      struct PinState {
        int pin;                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              
        int currentCommandIndex;
        unsigned long lastCommandTime;
        unsigned long commandDuration;
        bool isFinished;
      };

      PinState pinStates[pinArray.size()];

      // Initialize pin states
      int index = 0;
      for (JsonObject pinObj : pinArray) {
        pinStates[index].pin = pinObj["pin"];
        digitalWrite(pinStates[index].pin, HIGH);  // Ensure the pin starts high (Relay board used is active when low.)
        pinMode(pinStates[index].pin, OUTPUT);  // Set the pin as output
        pinStates[index].currentCommandIndex = 0;
        pinStates[index].lastCommandTime = millis();
        pinStates[index].commandDuration = 0;  // Initialize to zero as no command is set yet
        pinStates[index].isFinished = false;
        index++;
      }

      // Main loop to process commands in parallel
      while (true) {
        bool allPinsFinished = true;  // Assume all pins are finished initially

        for (int i = 0; i < pinArray.size(); i++) {
          if (pinStates[i].isFinished) {
            continue;  // Skip this pin if it has finished its commands
          }

          JsonObject pinObj = pinArray[i];
          JsonArray commands = pinObj["commands"];

          if (pinStates[i].currentCommandIndex < commands.size()) {
            JsonObject command = commands[pinStates[i].currentCommandIndex];
            int duration = command["duration"];
            int cmd = command["command"];
            cmd = !cmd; //revert command
            unsigned long currentTime = millis();

            // Check if it's time to update the pin state
            if (currentTime - pinStates[i].lastCommandTime >= pinStates[i].commandDuration) {
              // Execute the command
              digitalWrite(pinStates[i].pin, cmd);
              Serial.print("Pin ");
              Serial.print(pinStates[i].pin);
              Serial.print(" set to ");
              Serial.println(cmd);

              // Update state for the new command
              pinStates[i].commandDuration = duration * 1000; // Set new command duration
              pinStates[i].lastCommandTime = currentTime; // Update last command time

              // Move to the next command
              pinStates[i].currentCommandIndex++;

              if (pinStates[i].currentCommandIndex >= commands.size()) {
                 // Wait for the last command duration to pass
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
      // Reset pins
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
