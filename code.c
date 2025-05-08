// Reflex Rush: A reaction-time game for ESP32
// Players press touch sensors when the Green Light appears.
// Features a menu, Bluetooth control, and SD card storage.

// Include necessary libraries
#include <Wire.h>            // For I2C communication with the OLED display
#include <Adafruit_SSD1306.h> // Library for SSD1306 OLED display
#include <BluetoothSerial.h>  // For Bluetooth communication (ESP32 core)
#include <SD.h>              // For SD card operations (SPI interface)
#include <SPI.h>             // For SPI communication with SD card

// Constants for hardware and game settings
const int SCREEN_WIDTH = 128;        // OLED display width in pixels
const int SCREEN_HEIGHT = 64;        // OLED display height in pixels
const int MAX_PLAYERS = 4;           // Maximum number of players supported
const int SD_CS_PIN = 5;             // SD card chip select pin (SPI)
const int JOYSTICK_Y = 35;           // Analog pin for joystick Y-axis (menu navigation)
const int MENU_BUTTON = 27;          // Digital pin for menu selection button (active-low)
const int TOUCH_PINS[MAX_PLAYERS] = {12, 13, 14, 15}; // GPIO pins for TTP223 touch sensors
const int JOYSTICK_THRESHOLD = 1000; // Threshold for joystick movement detection (analog range 0-4095)
const int DEBOUNCE_DELAY = 20;       // Debounce delay for button in milliseconds
const int MAX_HISTORY_SIZE = 10000;  // Max characters for game history (SD card storage)
const int RESULT_DISPLAY_TIME = 5000; // Time to display game results on OLED (ms)
const int BLUETOOTH_CHUNK_SIZE = 200; // Chunk size for sending history over Bluetooth

// OLED Display object (128x64, I2C interface)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Bluetooth object for ESP32
BluetoothSerial ESP_BT;

// Game variables
long redDuration, yellowDuration, greenDuration; // Durations for each traffic light phase (randomized)
volatile unsigned long reactionTimes[MAX_PLAYERS] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF}; // Reaction times for each player (volatile for ISR)
volatile bool touchDetected[MAX_PLAYERS] = {false, false, false, false}; // Flags to track if each player has touched (volatile for ISR)
String gameHistory = "";                  // String to store game history
int numberOfPlayers = 1;                 // Number of active players (default: 1)
String playerNames[MAX_PLAYERS] = {"Player 1", "Player 2", "Player 3", "Player 4"}; // Default player names
unsigned long greenStartTime = 0;        // Timestamp when Green Light starts

// Leaderboard structure to store player names and best reaction times
struct Player {
  String name;              // Player name
  unsigned long bestReactionTime; // Best reaction time in milliseconds
};
Player leaderboard[MAX_PLAYERS]; // Array to store leaderboard data

// Menu variables
int currentMenuOption = 0;       // Currently selected menu option (index)
const String MENU_OPTIONS[] = {"Start Game", "View History", "View Leaderboard", "Delete History"}; // Menu options
const int MENU_SIZE = 4;         // Number of menu options
bool menuDisplayed = false;      // Flag to track if menu is currently displayed

// Interrupt Service Routines (ISRs) for TTP223 touch sensors
// Each ISR handles a player's touch sensor, recording the reaction time
void IRAM_ATTR touchISR1() {
  static unsigned long lastInterrupt = 0; // Static to retain last interrupt time
  if (!touchDetected[0] && millis() - lastInterrupt > 50) { // Check if not already detected and debounce
    reactionTimes[0] = millis(); // Record the time of touch
    touchDetected[0] = true;     // Mark player as having touched
    Serial.print("Player 1 touched at: ");
    Serial.print(reactionTimes[0]);
    Serial.println(" ms");
    lastInterrupt = millis();    // Update last interrupt time
  }
}
void IRAM_ATTR touchISR2() {
  static unsigned long lastInterrupt = 0;
  if (!touchDetected[1] && millis() - lastInterrupt > 50) {
    reactionTimes[1] = millis();
    touchDetected[1] = true;
    Serial.print("Player 2 touched at: ");
    Serial.print(reactionTimes[1]);
    Serial.println(" ms");
    lastInterrupt = millis();
  }
}
void IRAM_ATTR touchISR3() {
  static unsigned long lastInterrupt = 0;
  if (!touchDetected[2] && millis() - lastInterrupt > 50) {
    reactionTimes[2] = millis();
    touchDetected[2] = true;
    Serial.print("Player 3 touched at: ");
    Serial.print(reactionTimes[2]);
    Serial.println(" ms");
    lastInterrupt = millis();
  }
}
void IRAM_ATTR touchISR4() {
  static unsigned long lastInterrupt = 0;
  if (!touchDetected[3] && millis() - lastInterrupt > 50) {
    reactionTimes[3] = millis();
    touchDetected[3] = true;
    Serial.print("Player 4 touched at: ");
    Serial.print(reactionTimes[3]);
    Serial.println(" ms");
    lastInterrupt = millis();
  }
}

// Setup function: Runs once on startup to initialize hardware and settings
void setup() {
  Serial.begin(115200); // Start serial communication at 115200 baud for debugging
  Serial.println("Setup started");

  // Initialize OLED display (I2C, address 0x3C)
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed")); // If OLED fails to initialize, halt
    while (1);
  }
  display.display(); // Show initial Adafruit splash screen
  delay(2000);       // Wait 2 seconds
  display.clearDisplay(); // Clear the display
  display.setTextColor(SSD1306_WHITE); // Set text color to white
  display.setTextSize(1); // Set text size
  display.setCursor(0, 0); // Set cursor to top-left
  display.println(F("Reflex Rush")); // Display game title
  display.display(); // Update the OLED
  Serial.println("OLED initialized");

  // Initialize TTP223 touch sensor pins and attach interrupts
  for (int i = 0; i < MAX_PLAYERS; i++) {
    pinMode(TOUCH_PINS[i], INPUT); // Set touch pins as input
    Serial.print("Touch sensor ");
    Serial.print(i + 1);
    Serial.print(" initialized on GPIO ");
    Serial.println(TOUCH_PINS[i]);
  }
  attachInterrupt(digitalPinToInterrupt(TOUCH_PINS[0]), touchISR1, RISING); // Attach ISRs for touch sensors
  attachInterrupt(digitalPinToInterrupt(TOUCH_PINS[1]), touchISR2, RISING);
  attachInterrupt(digitalPinToInterrupt(TOUCH_PINS[2]), touchISR3, RISING);
  attachInterrupt(digitalPinToInterrupt(TOUCH_PINS[3]), touchISR4, RISING);
  Serial.println("Interrupts attached for touch sensors");

  // Initialize menu selection button (active-low with internal pull-up)
  pinMode(MENU_BUTTON, INPUT_PULLUP);
  Serial.println("Menu button initialized on GPIO 27");
  // Debug: Check initial button state
  Serial.print("Initial button state (HIGH = not pressed, LOW = pressed): ");
  Serial.println(digitalRead(MENU_BUTTON));

  // Initialize leaderboard with empty entries
  for (int i = 0; i < MAX_PLAYERS; i++) {
    leaderboard[i].name = "";
    leaderboard[i].bestReactionTime = 0;
  }
  Serial.println("Leaderboard initialized");

  // Initialize Bluetooth with device name "ReflexRush"
  ESP_BT.begin("ReflexRush");
  Serial.println(F("Bluetooth started with name: ReflexRush"));
  // Optional: Clear Bluetooth buffer to prevent residual commands
  // while (ESP_BT.available()) {
  //   ESP_BT.read();
  // }
  // Serial.println("Bluetooth buffer cleared");

  // Initialize SD card for storage
  Serial.println("Attempting SD card initialization...");
  if (!SD.begin(SD_CS_PIN)) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("SD Card init failed!");
    display.display();
    Serial.println(F("SD Card initialization failed! Continuing without SD..."));
    // Continue even if SD fails (non-critical for basic game functionality)
  } else {
    Serial.println("SD Card initialized");
    gameHistory = loadHistoryFromSD(); // Load game history from SD
    loadLeaderboardFromSD();          // Load leaderboard from SD
  }
  Serial.println("Setup completed");
}

// Loop function: Runs continuously after setup
void loop() {
  Serial.println("Entering loop()"); // Debug: Confirm loop is reached
  // Debug: Check button state to detect if it's stuck
  Serial.print("Button state (HIGH = not pressed, LOW = pressed): ");
  Serial.println(digitalRead(MENU_BUTTON));

  // Display the main menu if not already displayed
  if (!menuDisplayed) {
    displayMainMenu();
    menuDisplayed = true;
    Serial.println("Main menu displayed");
  }

  // Read joystick Y-axis for menu navigation
  int yValue = analogRead(JOYSTICK_Y);
  static unsigned long lastJoystickMove = 0; // Static to retain last move time
  if (millis() - lastJoystickMove > 200) { // Debounce joystick (200ms delay between moves)
    if (yValue < JOYSTICK_THRESHOLD) { // Joystick moved up
      currentMenuOption = (currentMenuOption - 1 + MENU_SIZE) % MENU_SIZE; // Move cursor up
      displayMainMenu();
      lastJoystickMove = millis();
      Serial.print("Joystick moved UP, selected menu option: ");
      Serial.println(MENU_OPTIONS[currentMenuOption]);
    } else if (yValue > 4095 - JOYSTICK_THRESHOLD) { // Joystick moved down
      currentMenuOption = (currentMenuOption + 1) % MENU_SIZE; // Move cursor down
      displayMainMenu();
      lastJoystickMove = millis();
      Serial.print("Joystick moved DOWN, selected menu option: ");
      Serial.println(MENU_OPTIONS[currentMenuOption]);
    }
  }

  // Check for menu button press to select an option
  if (digitalRead(MENU_BUTTON) == LOW) {
    delay(DEBOUNCE_DELAY); // Debounce the button
    if (digitalRead(MENU_BUTTON) == LOW) { // Confirm button is still pressed
      Serial.print("Menu button pressed, executing option: ");
      Serial.println(MENU_OPTIONS[currentMenuOption]);
      executeMenuOption(); // Execute the selected menu option
      menuDisplayed = false; // Reset menu display flag
    }
  }

  // Check for Bluetooth commands
  if (ESP_BT.available()) {
    String command = ESP_BT.readStringUntil('\n'); // Read command until newline
    command.trim(); // Remove whitespace
    Serial.print("Bluetooth command received: ");
    Serial.println(command);
    menuDisplayed = false; // Reset menu display to refresh after command
    if (command.startsWith("SELECT_PLAYERS_")) { // Set number of players
      int num = command.substring(15).toInt();
      if (num >= 1 && num <= MAX_PLAYERS) {
        numberOfPlayers = num;
        displayMenuOption("Players: " + String(num));
        ESP_BT.println("OK: Players set to " + String(num));
        Serial.println("Players set to: " + String(num));
      } else {
        displayMenuOption("Invalid player count");
        ESP_BT.println("ERROR: Invalid player count");
        Serial.println("Invalid player count received");
      }
    } else if (command.startsWith("SET_PLAYER_")) { // Set player name
      int playerIndex = command.charAt(12) - '1';
      String playerName = command.substring(14);
      if (playerIndex >= 0 && playerIndex < numberOfPlayers && playerName.length() > 0) {
        playerNames[playerIndex] = playerName;
        displayMenuOption(playerNames[playerIndex] + " set!");
        ESP_BT.println("OK: Player " + String(playerIndex + 1) + " set to " + playerName);
        Serial.println("Player " + String(playerIndex + 1) + " set to: " + playerName);
      } else {
        displayMenuOption("Invalid player");
        ESP_BT.println("ERROR: Invalid player or name");
        Serial.println("Invalid player or name received");
      }
    } else if (command == "START") { // Start game command (disabled for debugging)
      // Note: Uncomment the following lines to re-enable Bluetooth game start
      Serial.println("START command received but disabled for debugging");
      // displayMenuOption("Starting game...");
      // ESP_BT.println("OK: Game started");
      // Serial.println("Game started via Bluetooth");
      // startGame();
    } else if (command == "VIEW_HISTORY") { // View game history
      displayMenuOption("Viewing history...");
      ESP_BT.println("OK: Game history");
      Serial.println("Viewing history via Bluetooth");
      sendHistoryInChunks();
    } else if (command == "VIEW_LEADERBOARD") { // View leaderboard
      displayMenuOption("Leaderboard:");
      ESP_BT.println("OK: Leaderboard");
      Serial.println("Viewing leaderboard via Bluetooth");
      showLeaderboard();
    } else if (command == "DELETE_HISTORY") { // Delete game history
      displayMenuOption("Deleting history...");
      Serial.println("Deleting history via Bluetooth");
      deleteHistory();
    } else { // Unknown command
      displayMenuOption("Invalid command");
      ESP_BT.println("ERROR: Unknown command");
      Serial.println("Unknown Bluetooth command received");
    }
  }
  delay(100); // Small delay to slow down loop for debugging
}

// Start the game: Runs the traffic light sequence and records player reactions
void startGame() {
  Serial.println("Starting game sequence");
  // Reset reaction times and touch detection flags
  for (int i = 0; i < MAX_PLAYERS; i++) {
    reactionTimes[i] = 0xFFFFFFFF; // Reset to max value (indicating no touch)
    touchDetected[i] = false;       // Reset touch detection
    Serial.print("Reset reaction time for Player ");
    Serial.print(i + 1);
    Serial.println(": 0xFFFFFFFF ms");
  }

  // Red Light phase: Players must wait
  redDuration = random(1000, 5000); // Random duration between 1-5 seconds
  displayTrafficLight("RED");
  Serial.print("Red Light displayed for ");
  Serial.print(redDuration);
  Serial.println(" ms");
  unsigned long startTime = millis();
  while (millis() - startTime < redDuration) {
    for (int i = 0; i < numberOfPlayers; i++) {
      if (touchDetected[i]) {
        reactionTimes[i] = 0; // Jumpstart (penalized as 0ms)
        Serial.print("Jumpstart detected for Player ");
        Serial.print(i + 1);
        Serial.println(" during Red Light");
      }
    }
  }

  // Yellow Light phase: Players prepare
  yellowDuration = random(500, 2000); // Random duration between 0.5-2 seconds
  displayTrafficLight("YELLOW");
  Serial.print("Yellow Light displayed for ");
  Serial.print(yellowDuration);
  Serial.println(" ms");
  startTime = millis();
  while (millis() - startTime < yellowDuration) {
    for (int i = 0; i < numberOfPlayers; i++) {
      if (touchDetected[i]) {
        reactionTimes[i] = 0; // Jumpstart
        Serial.print("Jumpstart detected for Player ");
        Serial.print(i + 1);
        Serial.println(" during Yellow Light");
      }
    }
  }

  // Green Light phase: Players must press their touch sensors
  greenDuration = random(1000, 3000); // Random duration between 1-3 seconds
  displayTrafficLight("GREEN");
  greenStartTime = millis(); // Record the start time of Green Light
  Serial.print("Green Light displayed for ");
  Serial.print(greenDuration);
  Serial.print(" ms, started at: ");
  Serial.print(greenStartTime);
  Serial.println(" ms");
  while (millis() - greenStartTime < greenDuration) {
    // Wait for the duration; reactions are recorded via ISRs
  }

  // Compile and display game results
  String gameResult = "Game result: \n";
  for (int i = 0; i < numberOfPlayers; i++) {
    if (reactionTimes[i] == 0) { // Jumpstart
      gameResult += playerNames[i] + ": JS (Jumpstart)\n";
    } else if (reactionTimes[i] > greenStartTime) { // Valid reaction
      unsigned long reactionTime = reactionTimes[i] - greenStartTime;
      gameResult += playerNames[i] + ": " + String(reactionTime) + " ms\n";
    } else { // No response
      gameResult += playerNames[i] + ": No response\n";
    }
  }
  ESP_BT.println(gameResult); // Send results via Bluetooth
  Serial.print("Game results: ");
  Serial.println(gameResult);
  displayGameResults(gameResult); // Show results on OLED
  // Save results to game history if SD card is available
  if (checkSDCardSpace()) {
    if (gameHistory.length() + gameResult.length() < MAX_HISTORY_SIZE) {
      gameHistory += gameResult; // Append to history
      Serial.println("Game result appended to history");
    } else {
      gameHistory = gameResult; // Truncate history if too large
      displayMenuOption("History truncated!");
      ESP_BT.println("WARNING: History truncated");
      Serial.println("History truncated due to size limit");
    }
    saveHistoryToSD(); // Save to SD card
  }
  updateLeaderboard(); // Update leaderboard with new results
  saveLeaderboardToSD(); // Save leaderboard to SD card
  menuDisplayed = false; // Reset menu display flag to show menu again
  Serial.println("Game ended, returning to menu");
  delay(5000); // Delay to prevent immediate restart (helps with debugging)
}

// Display traffic light phase on OLED
void displayTrafficLight(String color) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2); // Larger text for traffic lights
  display.println(color + " LIGHT");
  display.display();
  Serial.println("OLED updated with: " + color + " LIGHT");
}

// Display a menu option or message on OLED
void displayMenuOption(String option) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1); // Smaller text for messages
  display.println(option);
  display.display();
  Serial.println("OLED updated with menu option: " + option);
}

// Display game results on OLED with text wrapping
void displayGameResults(String result) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  int y = 0; // Y position for text
  for (int i = 0; i < result.length(); i += 21) { // Wrap text every 21 characters
    display.setCursor(0, y);
    display.println(result.substring(i, i + 21));
    y += 8; // Move down one line (8 pixels per line)
    if (y >= SCREEN_HEIGHT) break; // Stop if screen is full
  }
  display.display();
  Serial.println("Displaying game results on OLED");
  delay(RESULT_DISPLAY_TIME); // Show results for 5 seconds
}

// Display the main menu on OLED
void displayMainMenu() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  for (int i = 0; i < MENU_SIZE; i++) {
    if (i == currentMenuOption) {
      display.print("> "); // Highlight selected option
    } else {
      display.print("  ");
    }
    display.println(MENU_OPTIONS[i]); // Display each menu option
  }
  display.display();
  Serial.println("Main menu updated on OLED");
}

// Execute the selected menu option
void executeMenuOption() {
  Serial.print("Executing menu option: ");
  Serial.println(MENU_OPTIONS[currentMenuOption]);
  switch (currentMenuOption) {
    case 0: // Start Game
      displayMenuOption("Starting game...");
      ESP_BT.println("OK: Game started");
      Serial.println("Starting game from menu");
      startGame();
      break;
    case 1: // View History
      displayMenuOption("Viewing history...");
      ESP_BT.println("OK: Game history");
      Serial.println("Viewing history from menu");
      sendHistoryInChunks();
      break;
    case 2: // View Leaderboard
      displayMenuOption("Leaderboard:");
      ESP_BT.println("OK: Leaderboard");
      Serial.println("Viewing leaderboard from menu");
      showLeaderboard();
      break;
    case 3: // Delete History
      displayMenuOption("Deleting history...");
      Serial.println("Deleting history from menu");
      deleteHistory();
      break;
  }
}

// Send game history in chunks over Bluetooth to avoid buffer overflow
void sendHistoryInChunks() {
  Serial.println("Sending game history in chunks via Bluetooth");
  for (int i = 0; i < gameHistory.length(); i += BLUETOOTH_CHUNK_SIZE) {
    ESP_BT.println(gameHistory.substring(i, i + BLUETOOTH_CHUNK_SIZE));
    delay(50); // Small delay between chunks
    Serial.print("Sent history chunk: ");
    Serial.println(gameHistory.substring(i, i + BLUETOOTH_CHUNK_SIZE));
  }
}

// Load game history from SD card
String loadHistoryFromSD() {
  if (!SD.exists("/history.txt")) { // Check if history file exists
    Serial.println("No history file found on SD card");
    return "";
  }
  File file = SD.open("/history.txt", FILE_READ); // Open file for reading
  String history = "";
  if (file) {
    while (file.available()) {
      history += (char)file.read(); // Read file contents
    }
    file.close();
    Serial.println("Game history loaded from SD card");
  } else {
    Serial.println("Failed to open history file on SD card");
  }
  return history;
}

// Save game history to SD card
void saveHistoryToSD() {
  File file = SD.open("/history.txt", FILE_WRITE); // Open file for writing
  if (file) {
    file.print(gameHistory); // Write history to file
    file.close();
    Serial.println("Game history saved to SD card");
  } else {
    ESP_BT.println("ERROR: Failed to write history");
    Serial.println("ERROR: Failed to write history to SD card");
  }
}

// Delete game history from SD card
void deleteHistory() {
  if (SD.exists("/history.txt")) { // Check if history file exists
    SD.remove("/history.txt");    // Delete the file
    gameHistory = "";             // Clear in-memory history
    ESP_BT.println("OK: History deleted");
    Serial.println("Game history deleted from SD card");
  } else {
    ESP_BT.println("No history file found");
    Serial.println("No history file found to delete on SD card");
  }
}

// Load leaderboard from SD card
void loadLeaderboardFromSD() {
  if (!SD.exists("/leaderboard.txt")) { // Check if leaderboard file exists
    Serial.println("No leaderboard file found on SD card");
    return;
  }
  File file = SD.open("/leaderboard.txt", FILE_READ); // Open file for reading
  if (file) {
    int index = 0;
    while (file.available() && index < MAX_PLAYERS) {
      leaderboard[index].name = file.readStringUntil(','); // Read name until comma
      leaderboard[index].bestReactionTime = file.readStringUntil('\n').toInt(); // Read reaction time
      index++;
    }
    file.close();
    Serial.println("Leaderboard loaded from SD card");
  } else {
    Serial.println("Failed to open leaderboard file on SD card");
  }
}

// Save leaderboard to SD card
void saveLeaderboardToSD() {
  File file = SD.open("/leaderboard.txt", FILE_WRITE); // Open file for writing
  if (file) {
    for (int i = 0; i < MAX_PLAYERS; i++) {
      file.print(leaderboard[i].name); // Write name
      file.print(",");                // Separator
      file.println(leaderboard[i].bestReactionTime); // Write reaction time
    }
    file.close();
    Serial.println("Leaderboard saved to SD card");
  } else {
    ESP_BT.println("ERROR: Failed to write leaderboard");
    Serial.println("ERROR: Failed to write leaderboard to SD card");
  }
}

// Show leaderboard on OLED and send via Bluetooth
void showLeaderboard() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.println("Leaderboard:");
  for (int i = 0; i < MAX_PLAYERS; i++) {
    if (leaderboard[i].bestReactionTime > 0) { // Only show non-zero entries
      display.println(leaderboard[i].name + ": " + String(leaderboard[i].bestReactionTime) + " ms");
      ESP_BT.println(leaderboard[i].name + ": " + String(leaderboard[i].bestReactionTime) + " ms");
      Serial.print("Leaderboard entry: ");
      Serial.print(leaderboard[i].name);
      Serial.print(": ");
      Serial.print(leaderboard[i].bestReactionTime);
      Serial.println(" ms");
    }
  }
  display.display();
  Serial.println("Leaderboard displayed on OLED");
}

// Update leaderboard with current game results
void updateLeaderboard() {
  Serial.println("Updating leaderboard");
  for (int i = 0; i < numberOfPlayers; i++) {
    if (reactionTimes[i] > greenStartTime && reactionTimes[i] != 0xFFFFFFFF) { // Valid reaction
      unsigned long reactionTime = reactionTimes[i] - greenStartTime;
      if (leaderboard[i].bestReactionTime == 0 || reactionTime < leaderboard[i].bestReactionTime) {
        leaderboard[i].name = playerNames[i]; // Update name
        leaderboard[i].bestReactionTime = reactionTime; // Update best time
        Serial.print("Leaderboard updated for ");
        Serial.print(playerNames[i]);
        Serial.print(": ");
        Serial.print(reactionTime);
        Serial.println(" ms");
      }
    }
  }
}

// Check SD card space and delete history if full (simplified check)
bool checkSDCardSpace() {
  File root = SD.open("/"); // Open root directory
  unsigned long freeSpace = 0;
  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;
    freeSpace += entry.size(); // Sum up used space (approximation)
    entry.close();
  }
  root.close();
  // If used space exceeds 1MB, delete history (simplified check)
  if (freeSpace > 1024 * 1024) {
    Serial.println("SD card space low; deleting history...");
    deleteHistory();
    gameHistory = "";
    displayMenuOption("SD full, history cleared!");
    ESP_BT.println("WARNING: SD card full, history cleared");
    return true;
  }
  return true;
}
