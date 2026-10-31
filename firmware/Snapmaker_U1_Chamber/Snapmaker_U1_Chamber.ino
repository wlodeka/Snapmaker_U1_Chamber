#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <string.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include "chamber_forward_types.h"

#define RELAY_PIN 5
#define FAN_PIN 6
#define NTC_PIN 7
#define BUZZER_PIN 8

Preferences preferences;
WebServer server(80);

// User-specific network and printer settings live only in NVS.  No Wi-Fi
// credentials or U1 address are embedded in the distributed firmware.
String wifiSsid;
String wifiPassword;
String printerIp;
String otaPassword;
// Supplied by U1's Moonraker purifier object; this is the authoritative
// TopCover detection signal.  An unknown/missing value is treated as absent.
bool u1TopCoverDetected = false;
bool u1TopCoverDetectionValid = false;
bool userNetworkConfigured = false;
bool configPortalActive = false;
bool stationConnectInProgress = false;
uint32_t stationConnectStartedMs = 0;
const uint32_t WIFI_CONNECT_TIMEOUT_MS = 20000UL;
const char *CONFIG_AP_SSID = "U1-Chamber-Setup";
const char *CONFIG_AP_PASSWORD = "chamberu1";
const uint8_t OTA_PASSWORD_MIN_LENGTH = 8;

float currentTemp = 0.0;
float setTemp = 30.0;
bool autoMode = true;
// PREHEAT is a local ESP32 workflow with no custom U1 macro/profile: the
// operator still explicitly starts the chamber.
bool preheatMode = false;
bool preheatReady = false;
bool autoStopLock = false;
bool masterOn = false;

// NTC 100 kOhm, B=3950: 3V3 -> 100 kOhm resistor -> NTC_PIN -> NTC -> GND.
const float NTC_NOMINAL_RESISTANCE = 100000.0;
const float NTC_NOMINAL_TEMP_C = 25.0;
const float NTC_BETA = 3950.0;
const float NTC_SERIES_RESISTOR = 100000.0;
const float ADC_SUPPLY_MV = 3300.0;
const float NTC_MIN_RESISTANCE = 1000.0;
// The chamber operates indoors; a much higher value is characteristic of an open NTC circuit.
const float NTC_MAX_RESISTANCE = 1200000.0;
const float NTC_MIN_VALID_TEMP_C = -15.0;
const float NTC_MAX_VALID_TEMP_C = 120.0;

// Target temperature is intentionally limited below the independent safety cut-off.
const float MAX_SET_TEMP = 60.0;
const float SAFETY_TEMP_LIMIT = 65.0;
const unsigned long BUZZER_ON_TIME = 250;
const unsigned long BUZZER_OFF_TIME = 750;

// parametry zaawansowane (konfigurowalne)
float hyst = 0.8;
unsigned long preFanTime = 20000;
unsigned long postFanTime = 30000;
float maxTempLimit = 65.0;

HeatingState chamberState = IDLE;

unsigned long stateStartTime = 0;
unsigned long lastTempRequest = 0;
const unsigned long tempInterval = 750;

unsigned long lastWiFiCheck = 0;
const unsigned long wifiCheckInterval = 30000;
String moonrakerCavityUrl;
String moonrakerGcodeUrl;
String moonrakerPrintStateUrl;

bool validPrinterIp(const String &address) {
  IPAddress parsed;
  return parsed.fromString(address);
}

void rebuildMoonrakerUrls() {
  moonrakerCavityUrl = "";
  moonrakerGcodeUrl = "";
  moonrakerPrintStateUrl = "";
  if (!validPrinterIp(printerIp)) return;

  const String base = "http://" + printerIp + ":7125";
  moonrakerCavityUrl = base + "/printer/objects/query?temperature_sensor%20cavity=temperature&print_stats=state&heater_bed=temperature,target,power&purifier=power_detected";
  moonrakerGcodeUrl = base + "/printer/gcode/script";
  moonrakerPrintStateUrl = base + "/printer/objects/query?print_stats=state";
}

void startConfigPortal() {
  if (configPortalActive) return;
  WiFi.mode(WIFI_AP_STA);
  if (WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD)) {
    configPortalActive = true;
    Serial.print("CONFIG AP: ");
    Serial.print(CONFIG_AP_SSID);
    Serial.print(" IP: http://");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("CONFIG AP FAILED");
  }
}

void stopConfigPortal() {
  if (!configPortalActive) return;

  // The setup AP is only a fallback for first configuration or a failed
  // station connection. Once the saved Wi-Fi connection succeeds, remove it.
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  configPortalActive = false;
  Serial.println("CONFIG AP stopped (station Wi-Fi connected)");
}

void startStationWiFi() {
  if (!userNetworkConfigured || wifiSsid.length() == 0) return;
  WiFi.mode(configPortalActive ? WIFI_AP_STA : WIFI_STA);
  WiFi.persistent(false);
  WiFi.begin(wifiSsid.c_str(), wifiPassword.c_str());
  stationConnectInProgress = true;
  stationConnectStartedMs = millis();
  Serial.print("WiFi connecting to: ");
  Serial.println(wifiSsid);
}
// Telemetry is intentionally polled more often than the 240 s BED ASSIST
// renew timer so the U1 status cards update promptly after user actions.
const unsigned long moonrakerPollInterval = 5000;
const unsigned long printerCavityStaleTime = 30000;
unsigned long lastMoonrakerPoll = 0;
unsigned long lastPrinterCavitySuccess = 0;
float printerCavityTemp = 0.0;
bool printerCavityValid = false;
unsigned long lastPrinterBedSuccess = 0;
float printerBedTemp = 0.0;
float printerBedTarget = 0.0;
float printerBedPower = 0.0;
bool printerBedValid = false;
bool printerBedTargetValid = false;
bool printerBedPowerValid = false;
bool printWasActive = false;
bool printEndPending = false;

enum MoonPrinterStateCode : uint8_t { MOON_PRINT_UNCHANGED, MOON_PRINT_STANDBY, MOON_PRINT_PRINTING, MOON_PRINT_COMPLETE, MOON_PRINT_CANCELLED, MOON_PRINT_OTHER };
struct MoonrakerSnapshot {
  bool httpOk;
  int httpCode;
  bool cavityValueValid;
  float cavityTemp;
  bool bedTemperatureValid;
  float bedTemperature;
  bool bedTargetValid;
  float bedTarget;
  bool bedPowerValid;
  float bedPower;
  bool topCoverDetectedValid;
  bool topCoverDetected;
  MoonPrinterStateCode printerStateCode;
  uint32_t getUs;
  uint32_t totalUs;
  uint32_t heap;
  uint32_t minHeap;
};

QueueHandle_t moonrakerQueue = nullptr;

enum MoonrakerCommandType : uint8_t {
  MOON_COMMAND_BED_ASSIST_START,
  MOON_COMMAND_BED_ASSIST_RENEW,
  MOON_COMMAND_BED_ASSIST_FAN_OFF,
  MOON_COMMAND_BED_ASSIST_MANUAL_OFF,
  MOON_COMMAND_MANUAL_FANS_ON,
  MOON_COMMAND_U1_FANS_OFF,
  MOON_COMMAND_SYNC_U1_DESIRED
};

struct MoonrakerCommand {
  MoonrakerCommandType type;
  uint32_t generation;
};

struct MoonrakerCommandResult {
  MoonrakerCommandType type;
  uint32_t generation;
  bool success;
  int primaryHttpCode;
  int secondaryHttpCode;
  int tertiaryHttpCode;
  uint8_t desiredOperation;
  bool stale;
  bool freshPrintingDetected;
  bool bedWriteCheckFailed;
};

QueueHandle_t moonrakerCommandQueue = nullptr;
QueueHandle_t moonrakerCommandResultQueue = nullptr;
bool moonrakerTaskReady = false;

// Test-only U1 simulator. It is RAM-only and starts OFF after every reboot.
// When enabled, Moonraker HTTP and all U1 G-code writes are bypassed, so a
// real U1 which is currently printing cannot receive a test command.
portMUX_TYPE virtualU1Mux = portMUX_INITIALIZER_UNLOCKED;
bool virtualU1Enabled = false;
MoonPrinterStateCode virtualU1PrinterState = MOON_PRINT_STANDBY;
bool virtualU1TopCoverDetected = true;

bool virtualU1IsEnabled() {
  portENTER_CRITICAL(&virtualU1Mux);
  bool enabled = virtualU1Enabled;
  portEXIT_CRITICAL(&virtualU1Mux);
  return enabled;
}

void getVirtualU1State(MoonPrinterStateCode &state, bool &topCover) {
  portENTER_CRITICAL(&virtualU1Mux);
  state = virtualU1PrinterState;
  topCover = virtualU1TopCoverDetected;
  portEXIT_CRITICAL(&virtualU1Mux);
}

void setVirtualU1State(bool enabled, MoonPrinterStateCode state, bool topCover) {
  portENTER_CRITICAL(&virtualU1Mux);
  virtualU1Enabled = enabled;
  virtualU1PrinterState = state;
  virtualU1TopCoverDetected = topCover;
  portEXIT_CRITICAL(&virtualU1Mux);
}

String printerState = "unknown";
bool moonrakerValid = false;
unsigned long lastMoonrakerSuccess = 0;
const unsigned long moonrakerWatchdogTimeout = 45000;

const uint32_t BED_ASSIST_RENEW_INTERVAL = 240000UL;
bool bedAssistActive = false;
bool bedAssistPending = false;
bool bedAssistRenewPending = false;
bool bedAssistCirculationRequested = false;
uint32_t bedAssistGeneration = 0;
uint32_t lastBedAssistRenew = 0;

// These flags express ESP32 ownership of the U1 fans, never their measured
// physical state.  They are changed only by loop()/WebServer handlers, not by
// the Heater Safety Task.  Releasing ownership schedules a single FAN=0 pass.
bool u1CirculationFanOwned = false;
bool u1CavityFanOwned = false;
bool u1FansReleasePending = false;
bool u1FansOffRequestedBySafety = false;

enum U1BedTargetAction : uint8_t { U1_BED_NO_TOUCH, U1_BED_TARGET_100, U1_BED_TARGET_0 };
enum U1DesiredOperation : uint8_t {
  U1_DESIRED_MANUAL_FANS_ON,
  U1_DESIRED_BED_ASSIST_START,
  U1_DESIRED_BED_ASSIST_RENEW,
  U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE,
  U1_DESIRED_BED_ASSIST_MANUAL_OFF,
  U1_DESIRED_RELEASE_FANS
};

// This is the sole cross-task desired-state hand-off for U1 commands.  The
// queue is only a wake-up signal; this persistent state prevents overwrite of
// the newest request from losing it.
struct U1DesiredState {
  uint32_t generation;
  bool syncPending;
  uint32_t nextAttemptMs;
  float circulationSpeed;
  float cavitySpeed;
  U1BedTargetAction bedAction;
  U1DesiredOperation operation;
};

SemaphoreHandle_t u1CommandMutex = nullptr;
U1DesiredState u1DesiredState = { 0, false, 0, 0.0f, 0.0f, U1_BED_NO_TOUCH, U1_DESIRED_RELEASE_FANS };

unsigned long buzzerLastChange = 0;
bool buzzerOn = false;
bool safetyAlarmActive = false;
bool thermistorFault = true;
bool overTemperatureFault = false;
bool hasValidNtcTemperature = false;

// Heater control is isolated from WebServer and Wi-Fi work.
const uint32_t HEATER_TASK_PERIOD_MS = 100;
const uint32_t HEATER_TASK_WDT_TIMEOUT_S = 5;
const unsigned long MIN_RELAY_OFF_TIME_MS = 10000UL;
// Conservative response check for a large, high-inertia chamber.  It tests
// only a sustained, actual relay-ON interval well below the target; it does
// not demand a fixed rise in every short thermostat window.
const unsigned long RUNAWAY_OBSERVATION_MS = 600000UL;
const float RUNAWAY_MIN_NET_RISE_C = 0.5f;
const float RUNAWAY_TARGET_MARGIN_C = 5.0f;

enum SafetyFaultReason : uint8_t { SAFETY_FAULT_NONE, SAFETY_FAULT_NTC, SAFETY_FAULT_OVERTEMP, SAFETY_FAULT_RUNAWAY, SAFETY_FAULT_SYSTEM };
const uint32_t FAULT_DETAILS_MAGIC = 0x46554C54UL; // "FULT"
const uint16_t PERSISTED_FAULT_VERSION = 2;
const uint32_t LAST_FAULT_NVS_RETRY_MS = 5000UL;

struct FaultDetails {
  uint32_t magic;
  SafetyFaultReason reason;
  bool valid;
  bool temperatureValid;
  float temperature;
  bool lastValidTemperatureAvailable;
  float lastValidTemperature;
  float targetTemperature;
  uint32_t uptimeMs;
  HeatingState chamberState;
  bool relayOn;
  bool masterOn;
  bool u1LinkOnline;
  bool preheatMode;
  bool runawayDataValid;
  float runawayStartTemperature;
  float runawayEndTemperature;
  float runawayRise;
  float runawayMinimumTemperature;
  float runawayMaximumTemperature;
  uint32_t runawayDurationMs;
  float runawayRequiredRise;
  uint32_t runawayRequiredWindowMs;
};

// Deliberately separate from the runtime record: fixed-width fields and an
// explicit packed wire format make this NVS payload stable across refactors.
struct __attribute__((packed)) PersistedFaultRecord {
  uint32_t magic;
  uint16_t version;
  uint16_t recordSize;
  uint32_t uptimeMs;
  uint32_t runawayDurationMs;
  uint32_t runawayRequiredWindowMs;
  float temperature;
  float lastValidTemperature;
  float targetTemperature;
  float runawayStartTemperature;
  float runawayEndTemperature;
  float runawayRise;
  float runawayMinimumTemperature;
  float runawayMaximumTemperature;
  float runawayRequiredRise;
  uint8_t reason;
  uint8_t valid;
  uint8_t temperatureValid;
  uint8_t lastValidTemperatureAvailable;
  uint8_t chamberState;
  uint8_t relayOn;
  uint8_t masterOn;
  uint8_t u1LinkOnline;
  uint8_t preheatMode;
  uint8_t runawayDataValid;
};

TaskHandle_t heaterSafetyTaskHandle = nullptr;
SemaphoreHandle_t heaterSafetyMutex = nullptr;
bool heaterSafetyTaskReady = false;
bool heaterSafetyU1LinkOnline = false;
bool heaterSafetySystemReady = false;
// Set by the Web OTA handler and consumed by the Heater Safety Task.  This
// keeps RELAY_PIN ownership exclusively in the Safety Task during an update.
bool otaUpdateRequested = false;
bool otaSafetyReady = false;
bool safetyFaultLatched = false;
SafetyFaultReason safetyFaultReason = SAFETY_FAULT_NONE;
// The Safety Task remains the owner of BUZZER_PIN.  loop() only requests the
// non-critical three-beep PREHEAT ready notification under its mutex.
bool preheatReadyBeepRequested = false;
bool preheatReadyBeepActive = false;
bool preheatReadyBeepOn = false;
uint8_t preheatReadyBeepsCompleted = 0;
unsigned long preheatReadyBeepLastChange = 0;
const unsigned long PREHEAT_READY_BEEP_ON_MS = 140;
const unsigned long PREHEAT_READY_BEEP_OFF_MS = 140;
bool faultResetRequested = false;
FaultDetails currentFaultDetails = {};
FaultDetails lastFaultDetails = {};
bool lastFaultSavePending = false;
uint32_t lastFaultGeneration = 0;
bool heaterRelayOn = false;
bool relayOffTimestampValid = false;
unsigned long lastRelayOffTime = 0;
bool runawayMonitoring = false;
unsigned long runawayWindowStartTime = 0;
float runawayWindowStartTemp = 0.0f;
float runawayWindowMinimumTemp = 0.0f;
float runawayWindowMaximumTemp = 0.0f;
bool minOffBlockedReported = false;

struct HeaterTelemetrySnapshot {
  float currentTemp;
  float setTemp;
  float hyst;
  float maxTempLimit;
  unsigned long preFanTime;
  unsigned long postFanTime;
  bool masterOn;
  HeatingState chamberState;
  bool thermistorFault;
  bool overTemperatureFault;
  bool safetyAlarmActive;
  bool faultLatched;
  SafetyFaultReason faultReason;
  FaultDetails currentFault;
  FaultDetails lastFault;
  bool runawayMonitoring;
  bool relayOn;
  bool fanOn;
};

// RAM-only chart history. The Moonraker task never reads or writes this buffer.
const uint16_t HISTORY_CAPACITY = 720;
const uint32_t HISTORY_INTERVAL_MS = 5000UL;
const size_t HISTORY_OUTPUT_BUFFER_SIZE = 2048;

struct HistorySample {
  uint32_t timestampMs;
  float chamberTemp;
  float targetTemp;
  float cavityTemp;
  float bedTemp;
  uint8_t flags;  // bit 0 = heater, bit 1 = fan, bit 2 = cavity valid, bit 3 = bed valid
  uint8_t reserved[3];
};

static_assert(sizeof(HistorySample) == 24, "Unexpected HistorySample size");

HistorySample historyBuffer[HISTORY_CAPACITY];
uint16_t historyWriteIndex = 0;
uint16_t historyCount = 0;
uint32_t historySequence = 0;
uint32_t lastHistorySampleMs = 0;

// Minimal, low-rate runtime diagnostics. Values are in microseconds unless noted.
uint32_t diagLoopLastUs = 0, diagLoopMaxUs = 0;
uint32_t diagServerLastUs = 0, diagServerMaxUs = 0;
uint32_t diagMoonGetLastUs = 0, diagMoonGetMaxUs = 0;
uint32_t diagMoonTotalLastUs = 0, diagMoonTotalMaxUs = 0;
uint32_t diagTempJsonLastUs = 0, diagTempJsonMaxUs = 0;
uint32_t diagRootLastUs = 0, diagRootMaxUs = 0;
uint32_t diagMoonHttpErrors = 0;
int diagLastMoonHttpCode = 0;
uint32_t diagHeapCurrent = 0, diagHeapMin = 0;
uint32_t diagHistoryLastMs = 0, diagHistoryMaxMs = 0;
uint16_t diagHistoryReturnedSamples = 0;
unsigned long diagLastReport = 0;

void diagnosticHeap() {
  diagHeapCurrent = ESP.getFreeHeap();
  diagHeapMin = ESP.getMinFreeHeap();
}

void diagnosticMax(uint32_t value, uint32_t &maximum) {
  if (value > maximum) maximum = value;
}

void diagnosticFinishLoop(uint32_t loopStartUs) {
  diagLoopLastUs = micros() - loopStartUs;
  diagnosticMax(diagLoopLastUs, diagLoopMaxUs);
  diagnosticHeap();

  if (diagLoopLastUs > 100000) {
    Serial.printf("SLOW t=%lu loop=%lums web=%lums lastMRget=%lums MRcode=%d heap=%lu min=%lu\n",
      millis(), diagLoopLastUs / 1000, diagServerLastUs / 1000,
      diagMoonGetLastUs / 1000, diagLastMoonHttpCode,
      diagHeapCurrent, diagHeapMin);
  }

  if (millis() - diagLastReport >= 10000) {
    diagLastReport = millis();
    Serial.printf("D t=%lu heap=%lu min=%lu loopMax=%lums webMax=%lums jsonMax=%lums rootMax=%lums historyLast=%lums historyMax=%lums historyN=%u mrGetMax=%lums mrTotalMax=%lums mrErr=%lu mrCode=%d\n",
      diagLastReport, diagHeapCurrent, diagHeapMin,
      diagLoopMaxUs / 1000, diagServerMaxUs / 1000,
      diagTempJsonMaxUs / 1000, diagRootMaxUs / 1000,
      diagHistoryLastMs, diagHistoryMaxMs, diagHistoryReturnedSamples,
      diagMoonGetMaxUs / 1000, diagMoonTotalMaxUs / 1000,
      diagMoonHttpErrors, diagLastMoonHttpCode);
  }
}

// ---------------- STATUS TEXT ----------------
String stateText(int s) {
  switch (s) {
    case 0: return "IDLE (heating OFF)";
    case 1: return "PRE-FAN";
    case 2: return "HEATING";
    case 3: return "COOLING";
    default: return "UNKNOWN";
  }
}

// ---------------- STATUS COLOR ----------------
String stateColor(int s) {
  switch (s) {
    case 0: return "#00eaff";   // idle – niebieski
    case 1: return "#ffaa00";   // pre-fan – pomarańczowy
    case 2: return "#ff6600";   // heating – czerwono-pomarańczowy
    case 3: return "#00ff9d";   // cooling – zielony
    default: return "#888888";
  }
}

// Returns false for a disconnected, shorted, or otherwise implausible NTC divider.
bool readNTCTemperature(float &temperatureC) {
  uint32_t millivolts = analogReadMilliVolts(NTC_PIN);
  if (millivolts <= 20 || millivolts >= 3280) return false;

  float resistance = NTC_SERIES_RESISTOR * (float)millivolts / (ADC_SUPPLY_MV - (float)millivolts);
  if (!isfinite(resistance) || resistance < NTC_MIN_RESISTANCE || resistance > NTC_MAX_RESISTANCE) return false;

  float temperatureK = 1.0 / ((1.0 / (NTC_NOMINAL_TEMP_C + 273.15)) + log(resistance / NTC_NOMINAL_RESISTANCE) / NTC_BETA);
  temperatureC = temperatureK - 273.15;
  return isfinite(temperatureC) && temperatureC >= NTC_MIN_VALID_TEMP_C && temperatureC <= NTC_MAX_VALID_TEMP_C;
}

void updateAlarmBuzzer(unsigned long now) {
  unsigned long interval = buzzerOn ? BUZZER_ON_TIME : BUZZER_OFF_TIME;
  if (now - buzzerLastChange >= interval) {
    buzzerOn = !buzzerOn;
    buzzerLastChange = now;
    digitalWrite(BUZZER_PIN, buzzerOn ? HIGH : LOW);
  }
}

// Called only by heaterSafetyTask while heaterSafetyMutex is held and only
// when no critical alarm is active.  Three complete ON pulses form READY.
void updatePreheatReadyBuzzer(unsigned long now) {
  if (preheatReadyBeepRequested && !preheatReadyBeepActive) {
    preheatReadyBeepRequested = false;
    preheatReadyBeepActive = true;
    preheatReadyBeepOn = true;
    preheatReadyBeepsCompleted = 0;
    preheatReadyBeepLastChange = now;
    digitalWrite(BUZZER_PIN, HIGH);
    return;
  }
  if (!preheatReadyBeepActive) return;

  const unsigned long interval = preheatReadyBeepOn ? PREHEAT_READY_BEEP_ON_MS : PREHEAT_READY_BEEP_OFF_MS;
  if ((uint32_t)(now - preheatReadyBeepLastChange) < interval) return;

  preheatReadyBeepLastChange = now;
  if (preheatReadyBeepOn) {
    preheatReadyBeepOn = false;
    digitalWrite(BUZZER_PIN, LOW);
    if (++preheatReadyBeepsCompleted >= 3) preheatReadyBeepActive = false;
  } else {
    preheatReadyBeepOn = true;
    digitalWrite(BUZZER_PIN, HIGH);
  }
}

void heaterSafetySetU1LinkOnline(bool online) {
  if (heaterSafetyMutex == nullptr) return;
  xSemaphoreTake(heaterSafetyMutex, portMAX_DELAY);
  heaterSafetyU1LinkOnline = online;
  xSemaphoreGive(heaterSafetyMutex);
}

// Values which decide relay/FSM behavior are changed under the same mutex as
// the Safety Task.  This prevents a partial configuration or START/STOP write
// from being observed while the task evaluates a control cycle.
bool heaterSafetyLock() {
  if (heaterSafetyMutex == nullptr) return false;
  return xSemaphoreTake(heaterSafetyMutex, portMAX_DELAY) == pdTRUE;
}

void heaterSafetyUnlock() {
  if (heaterSafetyMutex != nullptr) xSemaphoreGive(heaterSafetyMutex);
}

// Request a safe, persistent lockout before OTA flash writes begin.  The
// WebServer never writes RELAY_PIN; the Safety Task acknowledges the request
// only after it has forced the relay OFF itself.
bool requestOtaSafetyLockout() {
  if (!heaterSafetyLock()) return false;
  otaUpdateRequested = true;
  otaSafetyReady = false;
  masterOn = false;
  heaterSafetyUnlock();
  return true;
}

bool otaSafetyLockoutReady() {
  if (!heaterSafetyLock()) return false;
  bool ready = otaSafetyReady;
  heaterSafetyUnlock();
  return ready;
}

const char* safetyFaultReasonText(SafetyFaultReason reason) {
  switch (reason) {
    case SAFETY_FAULT_NTC: return "NTC";
    case SAFETY_FAULT_OVERTEMP: return "OVERTEMP";
    case SAFETY_FAULT_RUNAWAY: return "THERMAL_RUNAWAY";
    case SAFETY_FAULT_SYSTEM: return "SAFETY";
    default: return "NONE";
  }
}

// Called only by heaterSafetyTask while heaterSafetyMutex is held.
void captureFaultDetails(FaultDetails &details, SafetyFaultReason reason,
                         unsigned long now, bool u1LinkOnline,
                         bool temperatureValid) {
  details = {};
  details.magic = FAULT_DETAILS_MAGIC;
  details.reason = reason;
  details.valid = true;
  details.temperatureValid = temperatureValid;
  details.temperature = currentTemp;
  details.lastValidTemperatureAvailable = hasValidNtcTemperature;
  details.lastValidTemperature = currentTemp;
  details.targetTemperature = setTemp;
  details.uptimeMs = now;
  details.chamberState = chamberState;
  details.relayOn = heaterRelayOn;
  details.masterOn = masterOn;
  details.u1LinkOnline = u1LinkOnline;
  details.preheatMode = preheatMode;
  details.runawayDataValid = reason == SAFETY_FAULT_RUNAWAY;
  if (details.runawayDataValid) {
    details.runawayStartTemperature = runawayWindowStartTemp;
    details.runawayEndTemperature = currentTemp;
    details.runawayRise = currentTemp - runawayWindowStartTemp;
    details.runawayMinimumTemperature = runawayWindowMinimumTemp;
    details.runawayMaximumTemperature = runawayWindowMaximumTemp;
    details.runawayDurationMs = (uint32_t)(now - runawayWindowStartTime);
    details.runawayRequiredRise = RUNAWAY_MIN_NET_RISE_C;
    details.runawayRequiredWindowMs = RUNAWAY_OBSERVATION_MS;
  }
}

// Called only by heaterSafetyTask while heaterSafetyMutex is held.
void latchSafetyFault(SafetyFaultReason reason, unsigned long now,
                      bool u1LinkOnline, bool temperatureValid) {
  if (safetyFaultLatched) return;
  safetyFaultLatched = true;
  safetyFaultReason = reason;
  captureFaultDetails(currentFaultDetails, reason, now, u1LinkOnline, temperatureValid);
  lastFaultDetails = currentFaultDetails;
  lastFaultGeneration++;
  lastFaultSavePending = true;
  // The Safety Task never sends HTTP.  loop() consumes this request and makes
  // one best-effort asynchronous Moonraker fan-off request if it owns a U1 fan.
  u1FansOffRequestedBySafety = true;
}

PersistedFaultRecord makePersistedFaultRecord(const FaultDetails &details) {
  PersistedFaultRecord record = {};
  record.magic = FAULT_DETAILS_MAGIC;
  record.version = PERSISTED_FAULT_VERSION;
  record.recordSize = sizeof(PersistedFaultRecord);
  record.uptimeMs = details.uptimeMs;
  record.runawayDurationMs = details.runawayDurationMs;
  record.runawayRequiredWindowMs = details.runawayRequiredWindowMs;
  record.temperature = details.temperature;
  record.lastValidTemperature = details.lastValidTemperature;
  record.targetTemperature = details.targetTemperature;
  record.runawayStartTemperature = details.runawayStartTemperature;
  record.runawayEndTemperature = details.runawayEndTemperature;
  record.runawayRise = details.runawayRise;
  record.runawayMinimumTemperature = details.runawayMinimumTemperature;
  record.runawayMaximumTemperature = details.runawayMaximumTemperature;
  record.runawayRequiredRise = details.runawayRequiredRise;
  record.reason = (uint8_t)details.reason;
  record.valid = details.valid ? 1 : 0;
  record.temperatureValid = details.temperatureValid ? 1 : 0;
  record.lastValidTemperatureAvailable = details.lastValidTemperatureAvailable ? 1 : 0;
  record.chamberState = (uint8_t)details.chamberState;
  record.relayOn = details.relayOn ? 1 : 0;
  record.masterOn = details.masterOn ? 1 : 0;
  record.u1LinkOnline = details.u1LinkOnline ? 1 : 0;
  record.preheatMode = details.preheatMode ? 1 : 0;
  record.runawayDataValid = details.runawayDataValid ? 1 : 0;
  return record;
}

bool persistedFaultRecordIsValid(const PersistedFaultRecord &record) {
  return record.magic == FAULT_DETAILS_MAGIC &&
         record.version == PERSISTED_FAULT_VERSION &&
         record.recordSize == sizeof(PersistedFaultRecord) &&
         record.valid == 1 &&
         record.reason <= SAFETY_FAULT_SYSTEM &&
         record.chamberState <= POST_FAN_COOLING &&
         record.temperatureValid <= 1 && record.lastValidTemperatureAvailable <= 1 &&
         record.relayOn <= 1 && record.masterOn <= 1 && record.u1LinkOnline <= 1 &&
         record.preheatMode <= 1 && record.runawayDataValid <= 1;
}

FaultDetails makeRuntimeFaultDetails(const PersistedFaultRecord &record) {
  FaultDetails details = {};
  details.magic = FAULT_DETAILS_MAGIC;
  details.reason = (SafetyFaultReason)record.reason;
  details.valid = record.valid == 1;
  details.temperatureValid = record.temperatureValid == 1;
  details.temperature = record.temperature;
  details.lastValidTemperatureAvailable = record.lastValidTemperatureAvailable == 1;
  details.lastValidTemperature = record.lastValidTemperature;
  details.targetTemperature = record.targetTemperature;
  details.uptimeMs = record.uptimeMs;
  details.chamberState = (HeatingState)record.chamberState;
  details.relayOn = record.relayOn == 1;
  details.masterOn = record.masterOn == 1;
  details.u1LinkOnline = record.u1LinkOnline == 1;
  details.preheatMode = record.preheatMode == 1;
  details.runawayDataValid = record.runawayDataValid == 1;
  details.runawayStartTemperature = record.runawayStartTemperature;
  details.runawayEndTemperature = record.runawayEndTemperature;
  details.runawayRise = record.runawayRise;
  details.runawayMinimumTemperature = record.runawayMinimumTemperature;
  details.runawayMaximumTemperature = record.runawayMaximumTemperature;
  details.runawayDurationMs = record.runawayDurationMs;
  details.runawayRequiredRise = record.runawayRequiredRise;
  details.runawayRequiredWindowMs = record.runawayRequiredWindowMs;
  return details;
}

bool getLastFaultForNvs(FaultDetails &details, uint32_t &generation) {
  if (!heaterSafetyLock()) return false;
  if (!lastFaultSavePending) {
    heaterSafetyUnlock();
    return false;
  }
  details = lastFaultDetails;
  generation = lastFaultGeneration;
  heaterSafetyUnlock();
  return true;
}

// Called only by heaterSafetyTask while heaterSafetyMutex is held.
void heaterSafetyWriteRelay(bool on, unsigned long now) {
  if (on) {
    if (!heaterRelayOn) {
      digitalWrite(RELAY_PIN, HIGH);
      heaterRelayOn = true;
    }
  } else {
    if (heaterRelayOn) {
      digitalWrite(RELAY_PIN, LOW);
      heaterRelayOn = false;
      lastRelayOffTime = now;
      relayOffTimestampValid = true;
      runawayMonitoring = false;
      minOffBlockedReported = false;
    } else {
      digitalWrite(RELAY_PIN, LOW);
    }
  }
}

bool heaterSafetyRelayMayTurnOn(unsigned long now) {
  return !relayOffTimestampValid ||
         (unsigned long)(now - lastRelayOffTime) >= MIN_RELAY_OFF_TIME_MS;
}

bool getHeaterTelemetrySnapshot(HeaterTelemetrySnapshot &snapshot) {
  if (!heaterSafetyLock()) return false;
  snapshot.currentTemp = currentTemp;
  snapshot.setTemp = setTemp;
  snapshot.hyst = hyst;
  snapshot.maxTempLimit = maxTempLimit;
  snapshot.preFanTime = preFanTime;
  snapshot.postFanTime = postFanTime;
  snapshot.masterOn = masterOn;
  snapshot.chamberState = chamberState;
  snapshot.thermistorFault = thermistorFault;
  snapshot.overTemperatureFault = overTemperatureFault;
  snapshot.safetyAlarmActive = safetyAlarmActive;
  snapshot.faultLatched = safetyFaultLatched;
  snapshot.faultReason = safetyFaultReason;
  snapshot.currentFault = currentFaultDetails;
  snapshot.lastFault = lastFaultDetails;
  snapshot.runawayMonitoring = runawayMonitoring;
  snapshot.relayOn = heaterRelayOn;
  snapshot.fanOn = digitalRead(FAN_PIN) == HIGH;
  heaterSafetyUnlock();
  return true;
}

String faultDetailsJson(const char *prefix, const FaultDetails &details) {
  String json;
  String temp = details.temperatureValid ? String(details.temperature, 2) : "null";
  String lastValidTemp = details.lastValidTemperatureAvailable ? String(details.lastValidTemperature, 2) : "null";
  json += ", \"" + String(prefix) + "Valid\": " + String(details.valid ? 1 : 0);
  json += ", \"" + String(prefix) + "Reason\": " + String((int)details.reason);
  json += ", \"" + String(prefix) + "Temp\": " + temp;
  json += ", \"" + String(prefix) + "LastValidTemp\": " + lastValidTemp;
  json += ", \"" + String(prefix) + "Target\": " + String(details.targetTemperature, 1);
  json += ", \"" + String(prefix) + "AtMs\": " + String(details.uptimeMs);
  json += ", \"" + String(prefix) + "State\": " + String((int)details.chamberState);
  json += ", \"" + String(prefix) + "Relay\": " + String(details.relayOn ? 1 : 0);
  json += ", \"" + String(prefix) + "Master\": " + String(details.masterOn ? 1 : 0);
  json += ", \"" + String(prefix) + "U1Online\": " + String(details.u1LinkOnline ? 1 : 0);
  json += ", \"" + String(prefix) + "Preheat\": " + String(details.preheatMode ? 1 : 0);
  json += ", \"" + String(prefix) + "RunawayValid\": " + String(details.runawayDataValid ? 1 : 0);
  json += ", \"" + String(prefix) + "RunawayStart\": " + String(details.runawayStartTemperature, 2);
  json += ", \"" + String(prefix) + "RunawayEnd\": " + String(details.runawayEndTemperature, 2);
  json += ", \"" + String(prefix) + "RunawayRise\": " + String(details.runawayRise, 2);
  json += ", \"" + String(prefix) + "RunawayMin\": " + String(details.runawayMinimumTemperature, 2);
  json += ", \"" + String(prefix) + "RunawayMax\": " + String(details.runawayMaximumTemperature, 2);
  json += ", \"" + String(prefix) + "RunawayDurationMs\": " + String(details.runawayDurationMs);
  json += ", \"" + String(prefix) + "RunawayRequiredRise\": " + String(details.runawayRequiredRise, 1);
  json += ", \"" + String(prefix) + "RunawayWindowMs\": " + String(details.runawayRequiredWindowMs);
  return json;
}

String faultDetailsHtml(const FaultDetails &details) {
  if (!details.valid) return "No diagnostic data";
  if (details.reason == SAFETY_FAULT_RUNAWAY && details.runawayDataValid) {
    String riseText = details.runawayRise >= 0.0f ? "+" : "";
    riseText += String(details.runawayRise, 1);
    return "Start: " + String(details.runawayStartTemperature, 1) + "°C · End: " +
           String(details.runawayEndTemperature, 1) + "°C · Net rise: " + riseText + "°C / " + String(details.runawayDurationMs / 1000) +
           " s · Min/Max: " + String(details.runawayMinimumTemperature, 1) + "/" +
           String(details.runawayMaximumTemperature, 1) + "°C · Required net: +" +
           String(details.runawayRequiredRise, 1) + "°C · Target: " +
           String(details.targetTemperature, 1) + "°C";
  }
  if (details.reason == SAFETY_FAULT_OVERTEMP) {
    return "Temperature: " + String(details.temperature, 1) + "°C · Limit: " +
           String(SAFETY_TEMP_LIMIT, 1) + "°C · Target: " + String(details.targetTemperature, 1) + "°C";
  }
  if (details.reason == SAFETY_FAULT_NTC) {
    return details.lastValidTemperatureAvailable ?
           "Last valid temperature: " + String(details.lastValidTemperature, 1) + "°C · Target: " + String(details.targetTemperature, 1) + "°C" :
           "Last valid temperature: N/A · Target: " + String(details.targetTemperature, 1) + "°C";
  }
  return "Target: " + String(details.targetTemperature, 1) + "°C · Uptime: " + String(details.uptimeMs / 1000) + " s";
}

// This is the sole runtime owner of RELAY_PIN and FAN_PIN.
void heaterSafetyTask(void *parameter) {
  // Query first: Arduino-ESP32 often initializes the global TWDT before this
  // task starts.  Reinitializing it produces a harmless but misleading error.
  esp_err_t wdtStatus = esp_task_wdt_status(nullptr);
  esp_err_t wdtInit = ESP_OK;

  if (wdtStatus == ESP_ERR_INVALID_STATE) {
#if ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t wdtConfig = {};
    wdtConfig.timeout_ms = HEATER_TASK_WDT_TIMEOUT_S * 1000;
    wdtConfig.idle_core_mask = 0;
    wdtConfig.trigger_panic = true;
    wdtInit = esp_task_wdt_init(&wdtConfig);
#else
    wdtInit = esp_task_wdt_init(HEATER_TASK_WDT_TIMEOUT_S, true);
#endif
  }

  // ESP_OK means this task is already subscribed; ESP_ERR_NOT_FOUND means the
  // TWDT exists but this task must now be added to it.
  bool wdtRegistered = false;
  if (wdtStatus == ESP_OK) {
    wdtRegistered = true;
  } else if (wdtInit == ESP_OK) {
    wdtRegistered = esp_task_wdt_add(nullptr) == ESP_OK;
  }
  if (!wdtRegistered) {
    Serial.println("CRITICAL SAFETY: heater task WDT unavailable; heating locked OFF");
    if (heaterSafetyLock()) {
      heaterSafetySystemReady = false;
      masterOn = false;
      latchSafetyFault(SAFETY_FAULT_SYSTEM, millis(), heaterSafetyU1LinkOnline, false);
      safetyAlarmActive = true;
      buzzerOn = true;
      buzzerLastChange = millis();
      heaterSafetyUnlock();
    }
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(FAN_PIN, HIGH);
    digitalWrite(BUZZER_PIN, HIGH);
    for (;;) {
      updateAlarmBuzzer(millis());
      vTaskDelay(pdMS_TO_TICKS(HEATER_TASK_PERIOD_MS));
    }
  }

  if (heaterSafetyLock()) {
    heaterSafetySystemReady = true;
    heaterSafetyUnlock();
  }

  lastTempRequest = millis() - tempInterval;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    if (wdtRegistered) esp_task_wdt_reset();
    unsigned long currentMillis = millis();
    if (!heaterSafetyLock()) {
      // No shared state may be touched without the mutex. GPIO writes are safe
      // here and force the hardware into the conservative cooling state.
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(FAN_PIN, HIGH);
      vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(HEATER_TASK_PERIOD_MS));
      continue;
    }

    SafetyFaultReason faultLog = SAFETY_FAULT_NONE;
    bool resetLog = false;
    bool resetRejectedLog = false;
    bool minOffBlockedLog = false;
    bool runawayStartLog = false;
    bool runawayPassedLog = false;

    bool u1LinkOnline = heaterSafetyU1LinkOnline;
    if (otaUpdateRequested) {
      // OTA has priority over normal heating.  Keep local airflow on while
      // the upload is active; a reset or failed upload therefore cannot
      // resume heating without an explicit later START.
      masterOn = false;
      heaterSafetyWriteRelay(false, currentMillis);
      digitalWrite(FAN_PIN, HIGH);
      if (chamberState != POST_FAN_COOLING) {
        chamberState = POST_FAN_COOLING;
        stateStartTime = currentMillis;
      }
      runawayMonitoring = false;
      otaSafetyReady = true;
    } else if (!u1LinkOnline && chamberState != IDLE && chamberState != POST_FAN_COOLING) {
      masterOn = false;
      heaterSafetyWriteRelay(false, currentMillis);
      digitalWrite(FAN_PIN, HIGH);
      chamberState = POST_FAN_COOLING;
      stateStartTime = currentMillis;
    }

    if (currentMillis - lastTempRequest >= tempInterval) {
      lastTempRequest = currentMillis;
      float measuredTemp = 0.0;
      thermistorFault = !readNTCTemperature(measuredTemp);
      if (!thermistorFault) {
        currentTemp = measuredTemp;
        hasValidNtcTemperature = true;
        overTemperatureFault = currentTemp >= SAFETY_TEMP_LIMIT;
      } else {
        overTemperatureFault = false;
      }

      if (!thermistorFault && currentTemp >= maxTempLimit) {
        heaterSafetyWriteRelay(false, currentMillis);
        chamberState = POST_FAN_COOLING;
        stateStartTime = currentMillis;
      }
    }

    if (!safetyFaultLatched && thermistorFault) {
      latchSafetyFault(SAFETY_FAULT_NTC, currentMillis, u1LinkOnline, false);
      faultLog = SAFETY_FAULT_NTC;
    } else if (!safetyFaultLatched && overTemperatureFault) {
      latchSafetyFault(SAFETY_FAULT_OVERTEMP, currentMillis, u1LinkOnline, true);
      faultLog = SAFETY_FAULT_OVERTEMP;
    }

    if (faultResetRequested) {
      faultResetRequested = false;
      bool resetAllowed = safetyFaultLatched && !thermistorFault &&
                          currentTemp < maxTempLimit && u1LinkOnline && !heaterRelayOn;
      if (resetAllowed) {
        safetyFaultLatched = false;
        safetyFaultReason = SAFETY_FAULT_NONE;
        currentFaultDetails = {};
        runawayMonitoring = false;
        safetyAlarmActive = false;
        buzzerOn = false;
        digitalWrite(BUZZER_PIN, LOW);
        masterOn = false;
        chamberState = POST_FAN_COOLING;
        stateStartTime = currentMillis;
        resetLog = true;
      } else {
        resetRejectedLog = true;
      }
    }

    if (!safetyFaultLatched && !otaUpdateRequested) {
      if (!masterOn && chamberState != IDLE && chamberState != POST_FAN_COOLING) {
        chamberState = POST_FAN_COOLING;
        stateStartTime = currentMillis;
      }

      switch (chamberState) {
        case IDLE:
          heaterSafetyWriteRelay(false, currentMillis);
          if (masterOn && currentTemp <= (setTemp - (hyst / 2.0)) && currentTemp < maxTempLimit) {
            digitalWrite(FAN_PIN, HIGH);
            stateStartTime = currentMillis;
            chamberState = PRE_FAN_WAITING;
          } else {
            digitalWrite(FAN_PIN, LOW);
          }
          break;
        case PRE_FAN_WAITING:
          heaterSafetyWriteRelay(false, currentMillis);
          digitalWrite(FAN_PIN, HIGH);
          if (currentMillis - stateStartTime >= preFanTime) {
            if (heaterSafetyRelayMayTurnOn(currentMillis)) {
              heaterSafetyWriteRelay(true, currentMillis);
              chamberState = HEATING_ACTIVE;
            } else if (!minOffBlockedReported) {
              minOffBlockedReported = true;
              minOffBlockedLog = true;
            }
          }
          break;
        case HEATING_ACTIVE:
          digitalWrite(FAN_PIN, HIGH);
          if (currentTemp >= (setTemp + (hyst / 2.0))) {
            heaterSafetyWriteRelay(false, currentMillis);
            stateStartTime = currentMillis;
            chamberState = POST_FAN_COOLING;
          }
          break;
        case POST_FAN_COOLING:
          heaterSafetyWriteRelay(false, currentMillis);
          digitalWrite(FAN_PIN, HIGH);
          if (!safetyFaultLatched && currentMillis - stateStartTime >= postFanTime) {
            digitalWrite(FAN_PIN, LOW);
            chamberState = IDLE;
          }
          break;
      }

      // A large chamber naturally warms more slowly near its target.  Monitor
      // only sustained, actual relay-ON time while there is meaningful thermal
      // headroom.  Any relay OFF ends this continuous observation interval.
      bool monitorRunaway = masterOn && chamberState == HEATING_ACTIVE && heaterRelayOn &&
                            !thermistorFault && !overTemperatureFault &&
                            currentTemp <= (setTemp - RUNAWAY_TARGET_MARGIN_C);
      if (!monitorRunaway) {
        runawayMonitoring = false;
      } else if (!runawayMonitoring) {
        runawayMonitoring = true;
        runawayWindowStartTime = currentMillis;
        runawayWindowStartTemp = currentTemp;
        runawayWindowMinimumTemp = currentTemp;
        runawayWindowMaximumTemp = currentTemp;
        runawayStartLog = true;
      } else {
        if (currentTemp < runawayWindowMinimumTemp) runawayWindowMinimumTemp = currentTemp;
        if (currentTemp > runawayWindowMaximumTemp) runawayWindowMaximumTemp = currentTemp;
        if ((unsigned long)(currentMillis - runawayWindowStartTime) >= RUNAWAY_OBSERVATION_MS) {
          // No meaningful net gain (including a net fall) across ten minutes
          // of continuous real heating is evidence of missing thermal response.
          if ((currentTemp - runawayWindowStartTemp) < RUNAWAY_MIN_NET_RISE_C) {
            latchSafetyFault(SAFETY_FAULT_RUNAWAY, currentMillis, u1LinkOnline, true);
            faultLog = SAFETY_FAULT_RUNAWAY;
          } else {
            runawayWindowStartTime = currentMillis;
            runawayWindowStartTemp = currentTemp;
            runawayWindowMinimumTemp = currentTemp;
            runawayWindowMaximumTemp = currentTemp;
            runawayPassedLog = true;
          }
        }
      }
    }

    if (safetyFaultLatched) {
      preheatReadyBeepRequested = false;
      preheatReadyBeepActive = false;
      // This block has final priority after the FSM. A POST-FAN timeout must
      // never override the forced cooling state while FAULT is latched.
      if (!safetyAlarmActive) {
        safetyAlarmActive = true;
        stateStartTime = currentMillis;
        buzzerOn = true;
        buzzerLastChange = currentMillis;
        digitalWrite(BUZZER_PIN, HIGH);
      } else {
        updateAlarmBuzzer(currentMillis);
      }
      heaterSafetyWriteRelay(false, currentMillis);
      digitalWrite(FAN_PIN, HIGH);
      chamberState = POST_FAN_COOLING;
      masterOn = false;
    } else {
      updatePreheatReadyBuzzer(currentMillis);
    }

    heaterSafetyUnlock();

    if (faultLog != SAFETY_FAULT_NONE) {
      Serial.printf("SAFETY FAULT LATCHED: %s\n", safetyFaultReasonText(faultLog));
    }
    if (resetLog) Serial.println("FAULT RESET");
    if (resetRejectedLog) Serial.println("FAULT RESET REJECTED");
    if (minOffBlockedLog) Serial.println("RELAY ON BLOCKED: MIN OFF TIME");
    if (runawayStartLog) Serial.println("RUNAWAY OBSERVATION START");
    if (runawayPassedLog) Serial.println("RUNAWAY OBSERVATION PASSED");

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(HEATER_TASK_PERIOD_MS));
  }
}

MoonPrinterStateCode parsePrinterState(const String &value) {
  if (value == "standby") return MOON_PRINT_STANDBY;
  if (value == "printing") return MOON_PRINT_PRINTING;
  if (value == "complete") return MOON_PRINT_COMPLETE;
  if (value == "cancelled") return MOON_PRINT_CANCELLED;
  return MOON_PRINT_OTHER;
}

bool parseMoonrakerFloat(const String &response, int objectStart, const char *field, float minimum, float maximum, float &value) {
  if (objectStart < 0) return false;
  int objectEnd = response.indexOf("}", objectStart);
  int fieldStart = response.indexOf(field, objectStart);
  if (fieldStart < 0 || (objectEnd >= 0 && fieldStart > objectEnd)) return false;

  char *valueEnd;
  const char *valueStart = response.c_str() + fieldStart + strlen(field);
  float parsed = strtof(valueStart, &valueEnd);
  if (valueEnd == valueStart || !isfinite(parsed) || parsed < minimum || parsed > maximum) return false;
  value = parsed;
  return true;
}

bool parseMoonrakerBool(const String &response, int objectStart, const char *field, bool &value) {
  if (objectStart < 0) return false;
  int objectEnd = response.indexOf("}", objectStart);
  int fieldStart = response.indexOf(field, objectStart);
  if (fieldStart < 0 || (objectEnd >= 0 && fieldStart > objectEnd)) return false;

  int valueStart = fieldStart + strlen(field);
  while (valueStart < response.length() &&
         (response[valueStart] == ' ' || response[valueStart] == '\t' ||
          response[valueStart] == '\r' || response[valueStart] == '\n')) valueStart++;
  if (response.startsWith("true", valueStart)) {
    value = true;
    return true;
  }
  if (response.startsWith("false", valueStart)) {
    value = false;
    return true;
  }
  return false;
}

int sendMoonrakerGcode(const char *script) {
  if (virtualU1IsEnabled()) {
    Serial.printf("VIRTUAL U1 GCODE: %s\n", script);
    return HTTP_CODE_OK;
  }
  if (WiFi.status() != WL_CONNECTED || moonrakerGcodeUrl.length() == 0) return -1;

  HTTPClient http;
  http.setConnectTimeout(500);
  http.setTimeout(600);
  if (!http.begin(moonrakerGcodeUrl)) return -1;
  http.addHeader("Content-Type", "application/json");
  String body = String("{\"script\":\"") + script + "\"}";
  int httpCode = http.POST(body);
  http.end();
  return httpCode;
}

int sendMoonrakerFanSpeed(const char *fanName, float speed) {
  // U1 creates circulation_fan only while its own purifier logic confirms a
  // TopCover.  Never send that G-code for an absent or unknown cover state;
  // treat the requested "off" state as already satisfied in this case.
  if (strcmp(fanName, "circulation_fan") == 0 &&
      (!u1TopCoverDetectionValid || !u1TopCoverDetected)) {
    Serial.println("U1 FANS: circulation command skipped (TopCover absent/unknown)");
    return HTTP_CODE_OK;
  }
  char script[96];
  snprintf(script, sizeof(script), "SET_FAN_SPEED FAN=%s SPEED=%.2f", fanName, speed);
  return sendMoonrakerGcode(script);
}

// Keep the parser types and its helper functions in a C++ namespace.  The
// Arduino .ino preprocessor only auto-prototypes global sketch functions;
// this prevents it from emitting prototypes before these local types exist.
namespace FreshJson {

enum FreshPrintState : uint8_t { FRESH_PRINT_NOT_RUNNING, FRESH_PRINT_RUNNING, FRESH_PRINT_UNKNOWN };
enum FreshJsonContext : uint8_t { FRESH_JSON_ROOT, FRESH_JSON_RESULT, FRESH_JSON_STATUS, FRESH_JSON_PRINT_STATS, FRESH_JSON_OTHER };

// ArduinoJson is not otherwise a dependency of this firmware.  This compact
// local parser validates the complete JSON grammar and follows only the exact
// result.status.print_stats.state path needed by this fail-safe authorization.
struct FreshJsonParser {
  const char *data;
  size_t length;
  size_t position;
  uint8_t depth;
  bool resultSeen;
  bool statusSeen;
  bool printStatsSeen;
  bool stateSeen;
  String state;
};

void freshJsonSkipWhitespace(FreshJsonParser &parser);
bool freshJsonParseString(FreshJsonParser &parser, String *output);
bool freshJsonParseValue(FreshJsonParser &parser, FreshJsonContext context);
bool freshJsonParseArray(FreshJsonParser &parser);
bool freshJsonParseObject(FreshJsonParser &parser, FreshJsonContext context);
bool freshJsonParseLiteral(FreshJsonParser &parser, const char *literal);
bool freshJsonParseNumber(FreshJsonParser &parser);
FreshPrintState parseVerifiedFreshPrintState(const String &response);
FreshPrintState queryFreshMoonrakerPrintState();

void freshJsonSkipWhitespace(FreshJsonParser &parser) {
  while (parser.position < parser.length) {
    char character = parser.data[parser.position];
    if (character != ' ' && character != '\t' && character != '\r' && character != '\n') break;
    parser.position++;
  }
}

bool freshJsonParseString(FreshJsonParser &parser, String *output) {
  if (parser.position >= parser.length || parser.data[parser.position++] != '\"') return false;
  if (output != nullptr) *output = "";
  while (parser.position < parser.length) {
    char character = parser.data[parser.position++];
    if (character == '\"') return true;
    if ((uint8_t)character < 0x20) return false;
    // This endpoint is expected to be ASCII-only.  Reject raw non-ASCII
    // bytes rather than accepting an invalid/ambiguous UTF-8 payload.
    if ((uint8_t)character >= 0x80) return false;
    if (character == '\\') {
      if (parser.position >= parser.length) return false;
      char escaped = parser.data[parser.position++];
      if (escaped == 'u') {
        for (uint8_t index = 0; index < 4; index++) {
          if (parser.position >= parser.length) return false;
          char hex = parser.data[parser.position++];
          if (!((hex >= '0' && hex <= '9') || (hex >= 'a' && hex <= 'f') || (hex >= 'A' && hex <= 'F'))) return false;
        }
      } else if (!(escaped == '\"' || escaped == '\\' || escaped == '/' || escaped == 'b' || escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't')) {
        return false;
      }
      // Escaped keys/states never match the plain ASCII names accepted below.
      if (output != nullptr) *output += '\x01';
    } else if (output != nullptr) {
      *output += character;
    }
  }
  return false;
}

bool freshJsonParseValue(FreshJsonParser &parser, FreshJsonContext context);

bool freshJsonParseArray(FreshJsonParser &parser) {
  if (parser.position >= parser.length || parser.data[parser.position++] != '[' || ++parser.depth > 24) return false;
  freshJsonSkipWhitespace(parser);
  if (parser.position < parser.length && parser.data[parser.position] == ']') {
    parser.position++;
    parser.depth--;
    return true;
  }
  for (;;) {
    if (!freshJsonParseValue(parser, FRESH_JSON_OTHER)) return false;
    freshJsonSkipWhitespace(parser);
    if (parser.position >= parser.length) return false;
    char separator = parser.data[parser.position++];
    if (separator == ']') {
      parser.depth--;
      return true;
    }
    if (separator != ',') return false;
    freshJsonSkipWhitespace(parser);
  }
}

bool freshJsonParseObject(FreshJsonParser &parser, FreshJsonContext context) {
  if (parser.position >= parser.length || parser.data[parser.position++] != '{' || ++parser.depth > 24) return false;
  freshJsonSkipWhitespace(parser);
  if (parser.position < parser.length && parser.data[parser.position] == '}') {
    parser.position++;
    parser.depth--;
    return true;
  }
  for (;;) {
    String key;
    if (!freshJsonParseString(parser, &key)) return false;
    freshJsonSkipWhitespace(parser);
    if (parser.position >= parser.length || parser.data[parser.position++] != ':') return false;
    freshJsonSkipWhitespace(parser);

    if (context == FRESH_JSON_ROOT && key == "result") {
      if (parser.resultSeen || !freshJsonParseObject(parser, FRESH_JSON_RESULT)) return false;
      parser.resultSeen = true;
    } else if (context == FRESH_JSON_RESULT && key == "status") {
      if (parser.statusSeen || !freshJsonParseObject(parser, FRESH_JSON_STATUS)) return false;
      parser.statusSeen = true;
    } else if (context == FRESH_JSON_STATUS && key == "print_stats") {
      if (parser.printStatsSeen || !freshJsonParseObject(parser, FRESH_JSON_PRINT_STATS)) return false;
      parser.printStatsSeen = true;
    } else if (context == FRESH_JSON_PRINT_STATS && key == "state") {
      if (parser.stateSeen || !freshJsonParseString(parser, &parser.state)) return false;
      parser.stateSeen = true;
    } else if (!freshJsonParseValue(parser, FRESH_JSON_OTHER)) {
      return false;
    }

    freshJsonSkipWhitespace(parser);
    if (parser.position >= parser.length) return false;
    char separator = parser.data[parser.position++];
    if (separator == '}') {
      parser.depth--;
      return true;
    }
    if (separator != ',') return false;
    freshJsonSkipWhitespace(parser);
  }
}

bool freshJsonParseLiteral(FreshJsonParser &parser, const char *literal) {
  for (size_t index = 0; literal[index] != '\0'; index++) {
    if (parser.position >= parser.length || parser.data[parser.position++] != literal[index]) return false;
  }
  return true;
}

bool freshJsonParseNumber(FreshJsonParser &parser) {
  if (parser.position < parser.length && parser.data[parser.position] == '-') parser.position++;
  if (parser.position >= parser.length) return false;
  if (parser.data[parser.position] == '0') {
    parser.position++;
  } else if (parser.data[parser.position] >= '1' && parser.data[parser.position] <= '9') {
    do { parser.position++; } while (parser.position < parser.length && parser.data[parser.position] >= '0' && parser.data[parser.position] <= '9');
  } else {
    return false;
  }
  if (parser.position < parser.length && parser.data[parser.position] == '.') {
    parser.position++;
    size_t fractionStart = parser.position;
    while (parser.position < parser.length && parser.data[parser.position] >= '0' && parser.data[parser.position] <= '9') parser.position++;
    if (parser.position == fractionStart) return false;
  }
  if (parser.position < parser.length && (parser.data[parser.position] == 'e' || parser.data[parser.position] == 'E')) {
    parser.position++;
    if (parser.position < parser.length && (parser.data[parser.position] == '+' || parser.data[parser.position] == '-')) parser.position++;
    size_t exponentStart = parser.position;
    while (parser.position < parser.length && parser.data[parser.position] >= '0' && parser.data[parser.position] <= '9') parser.position++;
    if (parser.position == exponentStart) return false;
  }
  return true;
}

bool freshJsonParseValue(FreshJsonParser &parser, FreshJsonContext context) {
  freshJsonSkipWhitespace(parser);
  if (parser.position >= parser.length) return false;
  char character = parser.data[parser.position];
  if (character == '{') return freshJsonParseObject(parser, context);
  if (character == '[') return freshJsonParseArray(parser);
  if (character == '\"') return freshJsonParseString(parser, nullptr);
  if (character == 't') return freshJsonParseLiteral(parser, "true");
  if (character == 'f') return freshJsonParseLiteral(parser, "false");
  if (character == 'n') return freshJsonParseLiteral(parser, "null");
  return freshJsonParseNumber(parser);
}

FreshPrintState parseVerifiedFreshPrintState(const String &response) {
  FreshJsonParser parser = {};
  parser.data = response.c_str();
  parser.length = response.length();
  freshJsonSkipWhitespace(parser);
  if (parser.position >= parser.length || parser.data[parser.position] != '{' || !freshJsonParseObject(parser, FRESH_JSON_ROOT)) return FRESH_PRINT_UNKNOWN;
  freshJsonSkipWhitespace(parser);
  if (parser.position != parser.length || !parser.resultSeen || !parser.statusSeen || !parser.printStatsSeen || !parser.stateSeen || parser.state.length() == 0) return FRESH_PRINT_UNKNOWN;
  if (parser.state == "printing") return FRESH_PRINT_RUNNING;
  if (parser.state == "standby" || parser.state == "complete" || parser.state == "cancelled") return FRESH_PRINT_NOT_RUNNING;
  return FRESH_PRINT_UNKNOWN;
}

// Called only from moonrakerTask's command path.  Unknown is deliberately
// fail-safe for bed=100: parsing must validate result.status.print_stats.state.
FreshPrintState queryFreshMoonrakerPrintState() {
  if (virtualU1IsEnabled()) {
    MoonPrinterStateCode state;
    bool topCover;
    getVirtualU1State(state, topCover);
    (void)topCover;
    return state == MOON_PRINT_PRINTING ? FRESH_PRINT_RUNNING :
           state == MOON_PRINT_STANDBY || state == MOON_PRINT_COMPLETE || state == MOON_PRINT_CANCELLED ?
             FRESH_PRINT_NOT_RUNNING : FRESH_PRINT_UNKNOWN;
  }
  if (WiFi.status() != WL_CONNECTED || moonrakerPrintStateUrl.length() == 0) return FRESH_PRINT_UNKNOWN;

  HTTPClient http;
  http.setConnectTimeout(500);
  http.setTimeout(600);
  if (!http.begin(moonrakerPrintStateUrl)) return FRESH_PRINT_UNKNOWN;

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return FRESH_PRINT_UNKNOWN;
  }

  String response = http.getString();
  http.end();
  if (response.length() == 0) return FRESH_PRINT_UNKNOWN;
  return parseVerifiedFreshPrintState(response);
}

} // namespace FreshJson

bool enqueueMoonrakerCommand(MoonrakerCommandType type, uint32_t generation);

bool u1CommandLock() {
  return u1CommandMutex != nullptr && xSemaphoreTake(u1CommandMutex, portMAX_DELAY) == pdTRUE;
}

void u1CommandUnlock() {
  if (u1CommandMutex != nullptr) xSemaphoreGive(u1CommandMutex);
}

bool getPendingU1DesiredState(U1DesiredState &state, uint32_t nowMs) {
  if (!u1CommandLock()) return false;
  bool due = u1DesiredState.syncPending && (int32_t)(nowMs - u1DesiredState.nextAttemptMs) >= 0;
  if (due) state = u1DesiredState;
  u1CommandUnlock();
  return due;
}

bool u1CommandGenerationCurrent(uint32_t generation) {
  if (!u1CommandLock()) return false;
  bool current = u1DesiredState.generation == generation;
  u1CommandUnlock();
  return current;
}

void finishU1DesiredSync(uint32_t generation, bool success) {
  if (!u1CommandLock()) return;
  if (u1DesiredState.generation == generation) {
    if (success) {
      u1DesiredState.syncPending = false;
    } else {
      // A failed HTTP request does not discard the latest desired state.  A
      // new request wakes the task immediately; otherwise retry follows the
      // existing Moonraker cadence without a busy loop.
      u1DesiredState.nextAttemptMs = millis() + moonrakerPollInterval;
    }
  }
  u1CommandUnlock();
}

bool requestU1DesiredState(float circulationSpeed, float cavitySpeed,
                           U1BedTargetAction bedAction,
                           U1DesiredOperation operation,
                           uint32_t &generation) {
  if (!u1CommandLock()) return false;
  u1DesiredState.generation++;
  if (u1DesiredState.generation == 0) u1DesiredState.generation++;
  u1DesiredState.syncPending = true;
  u1DesiredState.nextAttemptMs = 0;
  u1DesiredState.circulationSpeed = circulationSpeed;
  u1DesiredState.cavitySpeed = cavitySpeed;
  u1DesiredState.bedAction = bedAction;
  u1DesiredState.operation = operation;
  generation = u1DesiredState.generation;
  u1CommandUnlock();

  // The task also examines syncPending after polling, so xQueueOverwrite is
  // only an immediate wake-up.  Losing this wake-up cannot lose desired state.
  if (!moonrakerTaskReady) return false;
  enqueueMoonrakerCommand(MOON_COMMAND_SYNC_U1_DESIRED, generation);
  return true;
}

void processMoonrakerCommand(const MoonrakerCommand &command) {
  if (command.type != MOON_COMMAND_SYNC_U1_DESIRED) return;

  U1DesiredState desired = {};
  if (!getPendingU1DesiredState(desired, millis())) return;
  if (command.generation != desired.generation || !u1CommandGenerationCurrent(desired.generation)) return;

  MoonrakerCommandResult result = { MOON_COMMAND_SYNC_U1_DESIRED, desired.generation,
    false, -1, -1, -1, (uint8_t)desired.operation, false, false, false };
  bool allOk = true;
  bool cancelCurrentDesiredForPrinting = false;

  if (desired.operation == U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE) {
    result.primaryHttpCode = sendMoonrakerFanSpeed("circulation_fan", desired.circulationSpeed);
    allOk = result.primaryHttpCode == HTTP_CODE_OK;
  } else if (desired.operation == U1_DESIRED_BED_ASSIST_RENEW) {
    if (!u1CommandGenerationCurrent(desired.generation)) {
      result.stale = true;
    } else {
      FreshJson::FreshPrintState freshState = FreshJson::queryFreshMoonrakerPrintState();
      if (!u1CommandGenerationCurrent(desired.generation)) {
        result.stale = true;
      } else if (freshState == FreshJson::FRESH_PRINT_RUNNING) {
        result.freshPrintingDetected = true;
        cancelCurrentDesiredForPrinting = true;
        allOk = false;
      } else if (freshState != FreshJson::FRESH_PRINT_NOT_RUNNING) {
        result.bedWriteCheckFailed = true;
        allOk = false;
      } else {
        result.primaryHttpCode = sendMoonrakerGcode("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=100");
        allOk = result.primaryHttpCode == HTTP_CODE_OK;
      }
    }
  } else {
    result.primaryHttpCode = sendMoonrakerFanSpeed("circulation_fan", desired.circulationSpeed);
    allOk = result.primaryHttpCode == HTTP_CODE_OK;

    // A newer STOP/FAULT/PRINTING request invalidates the old command before
    // it can produce its next external effect.
    if (!u1CommandGenerationCurrent(desired.generation)) {
      result.stale = true;
    } else if (desired.operation == U1_DESIRED_BED_ASSIST_START && !allOk) {
      // Do not arm the bed unless the first fan command has succeeded.
    } else {
      result.secondaryHttpCode = desired.cavitySpeed > 0.0f ?
        sendMoonrakerGcode("SET_FAN_SPEED FAN=cavity_fan SPEED=0.3") :
        sendMoonrakerGcode("SET_FAN_SPEED FAN=cavity_fan SPEED=0");
      allOk = allOk && result.secondaryHttpCode == HTTP_CODE_OK;

      if (!u1CommandGenerationCurrent(desired.generation)) {
        result.stale = true;
      } else if (desired.operation == U1_DESIRED_BED_ASSIST_START && !allOk) {
        // Likewise, a cavity command failure blocks bed=100 for this epoch.
      } else if (desired.bedAction == U1_BED_TARGET_100) {
        // This is deliberately a fresh Moonraker read, not the periodic
        // snapshot.  Check epoch both before and after its blocking HTTP call.
        if (!u1CommandGenerationCurrent(desired.generation)) {
          result.stale = true;
        } else {
          FreshJson::FreshPrintState freshState = FreshJson::queryFreshMoonrakerPrintState();
          if (!u1CommandGenerationCurrent(desired.generation)) {
            result.stale = true;
          } else if (freshState == FreshJson::FRESH_PRINT_RUNNING) {
            result.freshPrintingDetected = true;
            cancelCurrentDesiredForPrinting = true;
            allOk = false;
          } else if (freshState != FreshJson::FRESH_PRINT_NOT_RUNNING) {
            result.bedWriteCheckFailed = true;
            allOk = false;
          } else {
            result.tertiaryHttpCode = sendMoonrakerGcode("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=100");
            allOk = allOk && result.tertiaryHttpCode == HTTP_CODE_OK;
          }
        }
      } else if (desired.bedAction == U1_BED_TARGET_0) {
        result.tertiaryHttpCode = sendMoonrakerGcode("SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0");
        allOk = allOk && result.tertiaryHttpCode == HTTP_CODE_OK;
      }
    }
  }

  if (!result.stale && !u1CommandGenerationCurrent(desired.generation)) result.stale = true;
  if (!result.stale) {
    result.success = allOk;
    finishU1DesiredSync(desired.generation, cancelCurrentDesiredForPrinting ? true : result.success);
  }
  if (moonrakerCommandResultQueue != nullptr) xQueueOverwrite(moonrakerCommandResultQueue, &result);
}

// This task owns the blocking HTTP request. It never writes controller state or GPIO.
void moonrakerTask(void *parameter) {
  uint32_t lastPollMs = millis();

  for (;;) {
    // The persistent desired state is authoritative.  This makes a lost or
    // overwritten wake-up harmless and gives the newest epoch priority.
    U1DesiredState pendingDesired = {};
    if (getPendingU1DesiredState(pendingDesired, millis())) {
      MoonrakerCommand syncCommand = { MOON_COMMAND_SYNC_U1_DESIRED, pendingDesired.generation };
      processMoonrakerCommand(syncCommand);
      continue;
    }

    uint32_t elapsed = millis() - lastPollMs;
    if (elapsed < moonrakerPollInterval) {
      MoonrakerCommand command;
      if (xQueueReceive(moonrakerCommandQueue, &command, pdMS_TO_TICKS(moonrakerPollInterval - elapsed)) == pdPASS) {
        processMoonrakerCommand(command);
        continue;
      }
    }

    lastPollMs = millis();

    MoonrakerSnapshot snapshot = {};
    snapshot.httpCode = -1;
    snapshot.printerStateCode = MOON_PRINT_UNCHANGED;
    uint32_t totalStartUs = micros();

    if (virtualU1IsEnabled()) {
      bool topCover = false;
      getVirtualU1State(snapshot.printerStateCode, topCover);
      snapshot.httpOk = true;
      snapshot.httpCode = HTTP_CODE_OK;
      snapshot.topCoverDetectedValid = true;
      snapshot.topCoverDetected = topCover;
    } else if (WiFi.status() == WL_CONNECTED && moonrakerCavityUrl.length() > 0) {
      HTTPClient http;
      http.setConnectTimeout(500);
      http.setTimeout(600);
      if (!http.begin(moonrakerCavityUrl)) {
        snapshot.httpOk = false;
      } else {

        uint32_t getStartUs = micros();
        snapshot.httpCode = http.GET();
        snapshot.getUs = micros() - getStartUs;
        snapshot.httpOk = snapshot.httpCode == HTTP_CODE_OK;

        if (snapshot.httpOk) {
        String response = http.getString();

        int printStateStart = response.indexOf("\"print_stats\"");
        if (printStateStart >= 0) {
          int stateStart = response.indexOf("\"state\": \"", printStateStart);
          if (stateStart >= 0) {
            stateStart += 10;
            int stateEnd = response.indexOf("\"", stateStart);
            if (stateEnd > stateStart) {
              snapshot.printerStateCode = parsePrinterState(response.substring(stateStart, stateEnd));
            }
          }
        }

        int valueStart = response.indexOf("\"temperature\":");
        if (valueStart >= 0) {
          char *valueEnd;
          float value = strtof(response.c_str() + valueStart + 14, &valueEnd);
          if (valueEnd != response.c_str() + valueStart + 14 && isfinite(value) && value >= -20.0 && value <= 120.0) {
            snapshot.cavityTemp = value;
            snapshot.cavityValueValid = true;
          }
        }

        int bedStart = response.indexOf("\"heater_bed\"");
        snapshot.bedTemperatureValid = parseMoonrakerFloat(response, bedStart, "\"temperature\":", -20.0, 150.0, snapshot.bedTemperature);
        snapshot.bedTargetValid = parseMoonrakerFloat(response, bedStart, "\"target\":", 0.0, 150.0, snapshot.bedTarget);
        snapshot.bedPowerValid = parseMoonrakerFloat(response, bedStart, "\"power\":", 0.0, 1.0, snapshot.bedPower);
        int purifierStart = response.indexOf("\"purifier\"");
        snapshot.topCoverDetectedValid = parseMoonrakerBool(response, purifierStart, "\"power_detected\":", snapshot.topCoverDetected);
        }
        http.end();
      }
    }

    snapshot.totalUs = micros() - totalStartUs;
    snapshot.heap = ESP.getFreeHeap();
    snapshot.minHeap = ESP.getMinFreeHeap();
    xQueueOverwrite(moonrakerQueue, &snapshot);
  }
}

bool enqueueMoonrakerCommand(MoonrakerCommandType type, uint32_t generation) {
  if (!moonrakerTaskReady || moonrakerCommandQueue == nullptr) return false;
  MoonrakerCommand command = { type, generation };
  return xQueueOverwrite(moonrakerCommandQueue, &command) == pdPASS;
}

void releaseU1FansIfOwned(const char *reason) {
  if ((!u1CirculationFanOwned && !u1CavityFanOwned) || u1FansReleasePending) return;

  u1FansReleasePending = true;
  uint32_t generation = 0;
  bool queued = requestU1DesiredState(0.0f, 0.0f, U1_BED_NO_TOUCH,
                                      U1_DESIRED_RELEASE_FANS, generation);
  Serial.printf("U1 FANS: RELEASE (%s) epoch=%lu %s\n", reason,
    (unsigned long)generation, queued ? "QUEUED" : "PENDING");
}

void updateBedAssistCirculationFan() {
  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);
  // Only BED ASSIST in MANUAL owns this speed.  HEATING_ACTIVE is used rather
  // than a raw relay edge so normal thermostat hysteresis cannot spam U1.
  const bool shouldRun = preheatMode && bedAssistActive &&
                         u1TopCoverDetectionValid && u1TopCoverDetected &&
                         thermal.masterOn && thermal.chamberState == HEATING_ACTIVE &&
                         printerState != "printing";
  if (shouldRun == bedAssistCirculationRequested) return;

  uint32_t generation = 0;
  if (requestU1DesiredState(shouldRun ? 0.60f : 0.0f, 0.0f, U1_BED_NO_TOUCH,
                            U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE, generation)) {
    bedAssistCirculationRequested = shouldRun;
    Serial.printf("BED ASSIST: CIRCULATION %s epoch=%lu\n", shouldRun ? "60%" : "OFF",
      (unsigned long)generation);
  }
}

bool takeU1FansOffRequestedBySafety() {
  if (!heaterSafetyLock()) return false;
  bool requested = u1FansOffRequestedBySafety;
  u1FansOffRequestedBySafety = false;
  heaterSafetyUnlock();
  return requested;
}

void stopBedAssist() {
  bool fanMayBeRunning = bedAssistActive || bedAssistPending || u1CirculationFanOwned || u1CavityFanOwned;
  bedAssistGeneration++;
  bedAssistActive = false;
  bedAssistPending = false;
  bedAssistRenewPending = false;
  bedAssistCirculationRequested = false;
  if (fanMayBeRunning) releaseU1FansIfOwned("BED ASSIST AUTO OFF");
}

void stopBedAssistManual() {
  bool bedAssistMayBeRunning = bedAssistActive || bedAssistPending;
  bedAssistGeneration++;
  bedAssistActive = false;
  bedAssistPending = false;
  bedAssistRenewPending = false;
  bedAssistCirculationRequested = false;
  if (bedAssistMayBeRunning) {
    u1FansReleasePending = true;
    uint32_t generation = 0;
    bool queued = requestU1DesiredState(0.0f, 0.0f, U1_BED_TARGET_0,
                                        U1_DESIRED_BED_ASSIST_MANUAL_OFF, generation);
    Serial.printf("BED ASSIST: MANUAL OFF epoch=%lu %s\n", (unsigned long)generation,
      queued ? "QUEUED" : "PENDING");
  }
}

void applyMoonrakerCommandResult(const MoonrakerCommandResult &result) {
  if (result.type != MOON_COMMAND_SYNC_U1_DESIRED) return;
  if (result.stale || !u1CommandGenerationCurrent(result.generation)) {
    Serial.printf("U1 SYNC: STALE epoch=%lu\n", (unsigned long)result.generation);
    return;
  }

  U1DesiredOperation operation = (U1DesiredOperation)result.desiredOperation;
  if (result.freshPrintingDetected) {
    // The fresh pre-bed authorization is more recent than the normal polling
    // snapshot.  Hand off immediately: fans 0/0, no bed=0, no later renew.
    Serial.println("BED ASSIST: FRESH PRINTING; RELEASE U1 FANS");
    stopBedAssist();
    return;
  }
  if (result.bedWriteCheckFailed) {
    // UNKNOWN deliberately leaves the current desired state pending.  The
    // task retries on its normal cadence; it never guesses that printing is idle.
    Serial.println("BED ASSIST: FRESH PRINT CHECK FAILED; BED=100 BLOCKED");
    return;
  }

  if (operation == U1_DESIRED_BED_ASSIST_START) {
    if (result.success && preheatMode && printerState != "printing") {
      bedAssistPending = false;
      bedAssistActive = true;
      lastBedAssistRenew = millis();
      Serial.println("BED ASSIST: ON");
    } else {
      bedAssistPending = false;
      bedAssistActive = false;
      bedAssistCirculationRequested = false;
      Serial.printf("BED ASSIST: START NOT ACTIVE circulation=%d cavity=%d heater=%d\n",
        result.primaryHttpCode, result.secondaryHttpCode, result.tertiaryHttpCode);
      releaseU1FansIfOwned("BED ASSIST START FAILED");
    }
  } else if (operation == U1_DESIRED_BED_ASSIST_RENEW) {
    bedAssistRenewPending = false;
    if (result.success) {
      lastBedAssistRenew = millis();
    } else {
      Serial.printf("BED ASSIST: RENEW FAILED heater=%d\n", result.primaryHttpCode);
      stopBedAssist();
    }
  } else if (operation == U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE) {
    Serial.printf("BED ASSIST: CIRCULATION UPDATE speed=%s http=%d\n",
      bedAssistCirculationRequested ? "0.60" : "0.00", result.primaryHttpCode);
  } else if (operation == U1_DESIRED_BED_ASSIST_MANUAL_OFF) {
    if (result.success) {
      u1CirculationFanOwned = false;
      u1CavityFanOwned = false;
      u1FansReleasePending = false;
    }
    Serial.printf("BED ASSIST: MANUAL OFF circulation=%d cavity=%d heater=%d\n",
      result.primaryHttpCode, result.secondaryHttpCode, result.tertiaryHttpCode);
  } else if (operation == U1_DESIRED_RELEASE_FANS) {
    if (result.success) {
      u1CirculationFanOwned = false;
      u1CavityFanOwned = false;
      u1FansReleasePending = false;
    }
    Serial.printf("U1 FANS: OFF circulation=%d cavity=%d\n", result.primaryHttpCode, result.secondaryHttpCode);
  } else if (operation == U1_DESIRED_MANUAL_FANS_ON) {
    Serial.printf("U1 FANS: MANUAL ON circulation=%d cavity=%d\n", result.primaryHttpCode, result.secondaryHttpCode);
  }
}

// Only loop() applies the task result to controller-visible Moonraker state.
void applyMoonrakerSnapshot(const MoonrakerSnapshot &snapshot) {
  diagLastMoonHttpCode = snapshot.httpCode;
  diagMoonGetLastUs = snapshot.getUs;
  diagMoonTotalLastUs = snapshot.totalUs;
  diagnosticMax(diagMoonGetLastUs, diagMoonGetMaxUs);
  diagnosticMax(diagMoonTotalLastUs, diagMoonTotalMaxUs);
  diagHeapCurrent = snapshot.heap;
  diagHeapMin = snapshot.minHeap;
  if (!snapshot.httpOk) diagMoonHttpErrors++;

  if (snapshot.httpOk) {
    moonrakerValid = true;
    lastMoonrakerSuccess = millis();

    if (snapshot.printerStateCode != MOON_PRINT_UNCHANGED) {
      if (snapshot.printerStateCode == MOON_PRINT_STANDBY) printerState = "standby";
      else if (snapshot.printerStateCode == MOON_PRINT_PRINTING) printerState = "printing";
      else if (snapshot.printerStateCode == MOON_PRINT_COMPLETE) printerState = "complete";
      else if (snapshot.printerStateCode == MOON_PRINT_CANCELLED) printerState = "cancelled";
      else printerState = "unknown";

      if (snapshot.printerStateCode == MOON_PRINT_PRINTING) {
        printWasActive = true;
      } else if (printWasActive &&
                 (snapshot.printerStateCode == MOON_PRINT_COMPLETE ||
                  snapshot.printerStateCode == MOON_PRINT_CANCELLED)) {
        printEndPending = true;
      }
    }

    if (printerState == "printing") {
      if (bedAssistActive || bedAssistPending) {
        // Automatic BED ASSIST hand-off: fans only; the slicer owns bed target.
        stopBedAssist();
      } else {
        // A manual preheat must release U1 fan ownership exactly once at PRINTING.
        releaseU1FansIfOwned("PRINTING");
      }
    }

    if (snapshot.cavityValueValid) {
      printerCavityTemp = snapshot.cavityTemp;
      printerCavityValid = true;
      lastPrinterCavitySuccess = millis();
    }

    if (snapshot.bedTemperatureValid) {
      printerBedTemp = snapshot.bedTemperature;
      printerBedValid = true;
      lastPrinterBedSuccess = millis();
    }
    if (snapshot.bedTargetValid) {
      printerBedTarget = snapshot.bedTarget;
      printerBedTargetValid = true;
    } else {
      printerBedTargetValid = false;
    }
    if (snapshot.bedPowerValid) {
      printerBedPower = snapshot.bedPower;
      printerBedPowerValid = true;
    } else {
      printerBedPowerValid = false;
    }

    // The U1 purifier object is the only source used for TopCover detection.
    // A missing/invalid field fails closed: circulation_fan remains disabled.
    u1TopCoverDetectionValid = snapshot.topCoverDetectedValid;
    u1TopCoverDetected = snapshot.topCoverDetectedValid && snapshot.topCoverDetected;
  } else {
    moonrakerValid = false;
    u1TopCoverDetectionValid = false;
    u1TopCoverDetected = false;
  }

  if (millis() - lastPrinterCavitySuccess > printerCavityStaleTime) printerCavityValid = false;
  if (millis() - lastPrinterBedSuccess > printerCavityStaleTime) {
    printerBedValid = false;
    printerBedTargetValid = false;
    printerBedPowerValid = false;
  }

  Serial.printf("MR t=%lu code=%d get=%lums total=%lums heap=%lu min=%lu\n",
    millis(), diagLastMoonHttpCode, diagMoonGetLastUs / 1000,
    diagMoonTotalLastUs / 1000, diagHeapCurrent, diagHeapMin);
}

// ---------------- JSON ENDPOINT (temp + heater + fan) ----------------
void handleTempJSON() {
  uint32_t diagStartUs = micros();
  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);
  String json = "{\"temp\": " + String(thermal.currentTemp, 2) +
              ", \"heater\": " + String(thermal.relayOn ? 1 : 0) +
              ", \"fan\": " + String(thermal.fanOn ? 1 : 0) +
              ", \"target\": " + String(thermal.setTemp,1) +
              ", \"master\": " + String(thermal.masterOn ? 1 : 0) +
              ", \"printerCavity\": " + String(printerCavityTemp, 2) +
              ", \"printerCavityValid\": " + String(printerCavityValid ? 1 : 0) +
              ", \"printerBed\": " + String(printerBedTemp, 2) +
              ", \"printerBedValid\": " + String(printerBedValid ? 1 : 0) +
              ", \"printerBedTarget\": " + String(printerBedTarget, 1) +
              ", \"printerBedTargetValid\": " + String(printerBedTargetValid ? 1 : 0) +
              ", \"printerBedPower\": " + String(printerBedPower, 3) +
              ", \"printerBedPowerValid\": " + String(printerBedPowerValid ? 1 : 0) +
              ", \"printerBedHeating\": " + String((printerBedValid && (printerBedPowerValid ? printerBedPower > 0.0 : (printerBedTargetValid && printerBedTarget > printerBedTemp + 0.5))) ? 1 : 0) +
              ", \"preheatMode\": " + String(preheatMode ? 1 : 0) +
              ", \"preheatReady\": " + String(preheatReady ? 1 : 0) +
              ", \"virtualU1\": " + String(virtualU1IsEnabled() ? 1 : 0) +
              ", \"u1Online\": " + String((moonrakerValid && (uint32_t)(millis() - lastMoonrakerSuccess) <= moonrakerWatchdogTimeout) ? 1 : 0) +
              ", \"ntcOk\": " + String(thermal.thermistorFault ? 0 : 1) +
              ", \"topCoverValid\": " + String(u1TopCoverDetectionValid ? 1 : 0) +
              ", \"topCoverDetected\": " + String(u1TopCoverDetected ? 1 : 0) +
              ", \"state\": " + String((int)thermal.chamberState) +
              ", \"fault\": " + String(thermal.faultLatched ? 1 : 0) +
              ", \"faultReason\": " + String((int)thermal.faultReason) +
              faultDetailsJson("faultDetail", thermal.currentFault) +
              faultDetailsJson("lastFault", thermal.lastFault) +
              "}";

  server.send(200, "application/json", json);
  diagTempJsonLastUs = micros() - diagStartUs;
  diagnosticMax(diagTempJsonLastUs, diagTempJsonMaxUs);
  diagnosticHeap();
}

void recordHistorySample(uint32_t nowMs) {
  if ((uint32_t)(nowMs - lastHistorySampleMs) < HISTORY_INTERVAL_MS) return;

  HeaterTelemetrySnapshot thermal = {};
  if (!getHeaterTelemetrySnapshot(thermal)) return;
  lastHistorySampleMs = nowMs;

  HistorySample &sample = historyBuffer[historyWriteIndex];
  sample.timestampMs = nowMs;
  sample.chamberTemp = thermal.currentTemp;
  sample.targetTemp = thermal.setTemp;
  sample.cavityTemp = printerCavityTemp;
  sample.bedTemp = printerBedTemp;
  sample.flags = (thermal.relayOn ? 0x01 : 0x00) |
                 (thermal.fanOn ? 0x02 : 0x00) |
                 (printerCavityValid ? 0x04 : 0x00) |
                 (printerBedValid ? 0x08 : 0x00);
  sample.reserved[0] = 0;
  sample.reserved[1] = 0;
  sample.reserved[2] = 0;

  historyWriteIndex = (historyWriteIndex + 1) % HISTORY_CAPACITY;
  if (historyCount < HISTORY_CAPACITY) historyCount++;
  historySequence++;
}

// Valid for the retained window because it is always much smaller than 2^31.
bool historySequenceInRange(uint32_t sequence, uint32_t first, uint32_t last) {
  return (uint32_t)(sequence - first) < 0x80000000UL &&
         (uint32_t)(last - sequence) < 0x80000000UL;
}

bool parseHistorySequence(const String &text, uint32_t &value) {
  if (text.length() == 0 || text.length() > 10) return false;

  uint32_t parsed = 0;
  for (size_t i = 0; i < text.length(); i++) {
    char character = text[i];
    if (character < '0' || character > '9') return false;
    uint32_t digit = character - '0';
    if (parsed > (UINT32_MAX - digit) / 10) return false;
    parsed = parsed * 10 + digit;
  }

  value = parsed;
  return true;
}

void historyFlush(char *buffer, size_t &used) {
  if (used == 0) return;
  server.sendContent(buffer, used);
  used = 0;
}

void historyAppend(char *buffer, size_t &used, const char *content, size_t length) {
  if (length > HISTORY_OUTPUT_BUFFER_SIZE) {
    historyFlush(buffer, used);
    server.sendContent(content, length);
    return;
  }

  if (used + length > HISTORY_OUTPUT_BUFFER_SIZE) historyFlush(buffer, used);
  memcpy(buffer + used, content, length);
  used += length;
}

void handleHistoryJSON() {
  uint32_t startMs = millis();
  bool afterRequested = server.hasArg("after");
  bool hasAfter = false;
  uint32_t afterSequence = 0;

  if (afterRequested) {
    hasAfter = parseHistorySequence(server.arg("after"), afterSequence);
  }

  uint16_t startOffset = 0;
  uint16_t returnedCount = historyCount;
  bool resetRequired = false;
  uint32_t firstSequence = 0;

  if (historyCount > 0) {
    firstSequence = historySequence - historyCount + 1;

    if (hasAfter) {
      if (historySequenceInRange(afterSequence, firstSequence, historySequence)) {
        startOffset = (uint16_t)(afterSequence - firstSequence + 1);
        returnedCount = historyCount - startOffset;
      } else {
        // The requested sequence is no longer retained or comes from an ESP restart.
        resetRequired = true;
      }
    }
  } else if (afterRequested) {
    resetRequired = true;
  }

  if (afterRequested && !hasAfter) resetRequired = true;

  char outputBuffer[HISTORY_OUTPUT_BUFFER_SIZE];
  char recordBuffer[128];
  size_t used = 0;
  int headerLength = snprintf(recordBuffer, sizeof(recordBuffer),
    "{\"now\":%lu,\"firstSeq\":%lu,\"lastSeq\":%lu,\"reset\":%u,\"count\":%u,\"samples\":[",
    (unsigned long)millis(), (unsigned long)firstSequence,
    (unsigned long)historySequence, resetRequired ? 1 : 0, returnedCount);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  if (headerLength > 0 && headerLength < (int)sizeof(recordBuffer)) {
    historyAppend(outputBuffer, used, recordBuffer, (size_t)headerLength);
  }

  uint16_t oldestIndex = (historyWriteIndex + HISTORY_CAPACITY - historyCount) % HISTORY_CAPACITY;
  for (uint16_t offset = 0; offset < returnedCount; offset++) {
    const HistorySample &sample = historyBuffer[(oldestIndex + startOffset + offset) % HISTORY_CAPACITY];
    char cavityValue[16];
    char bedValue[16];
    bool cavityValid = (sample.flags & 0x04) != 0;
    bool bedValid = (sample.flags & 0x08) != 0;
    if (cavityValid) snprintf(cavityValue, sizeof(cavityValue), "%.2f", sample.cavityTemp);
    else snprintf(cavityValue, sizeof(cavityValue), "null");
    if (bedValid) snprintf(bedValue, sizeof(bedValue), "%.2f", sample.bedTemp);
    else snprintf(bedValue, sizeof(bedValue), "null");
    int recordLength = snprintf(recordBuffer, sizeof(recordBuffer),
      "%s[%lu,%.2f,%.2f,%s,%s,%u]", offset == 0 ? "" : ",",
      (unsigned long)sample.timestampMs, sample.chamberTemp, sample.targetTemp,
      cavityValue, bedValue, sample.flags);

    if (recordLength > 0 && recordLength < (int)sizeof(recordBuffer)) {
      historyAppend(outputBuffer, used, recordBuffer, (size_t)recordLength);
    }
  }

  historyAppend(outputBuffer, used, "]}", 2);
  historyFlush(outputBuffer, used);
  server.sendContent("");

  diagHistoryLastMs = millis() - startMs;
  diagnosticMax(diagHistoryLastMs, diagHistoryMaxMs);
  diagHistoryReturnedSamples = returnedCount;
  diagnosticHeap();
}

// ---------------- USTAWIENIA ZAawansowane (GET /cfg) ----------------
void handleConfig() {
  const bool hasHyst = server.hasArg("hyst");
  const float requestedHyst = hasHyst ? server.arg("hyst").toFloat() : 0.0;
  const bool hasPre = server.hasArg("pre");
  const long requestedPre = hasPre ? server.arg("pre").toInt() : 0;
  const bool hasPost = server.hasArg("post");
  const long requestedPost = hasPost ? server.arg("post").toInt() : 0;
  const bool hasMax = server.hasArg("max");
  const float requestedMax = hasMax ? server.arg("max").toFloat() : 0.0;

  bool saveHyst = false, savePre = false, savePost = false, saveMax = false, saveSetTemp = false;
  float savedHyst = 0.0, savedMax = 0.0, savedSetTemp = 0.0;
  unsigned long savedPre = 0, savedPost = 0;

  if (heaterSafetyLock()) {
    if (hasHyst && requestedHyst >= 0.1 && requestedHyst <= 10.0) {
      hyst = requestedHyst;
      savedHyst = hyst;
      saveHyst = true;
    }
    if (hasPre && requestedPre >= 0 && requestedPre <= 600) {
      preFanTime = (unsigned long)requestedPre * 1000UL;
      savedPre = preFanTime;
      savePre = true;
    }
    if (hasPost && requestedPost >= 0 && requestedPost <= 600) {
      postFanTime = (unsigned long)requestedPost * 1000UL;
      savedPost = postFanTime;
      savePost = true;
    }
    if (hasMax && requestedMax >= 1.0 && requestedMax <= SAFETY_TEMP_LIMIT) {
      maxTempLimit = requestedMax;
      savedMax = maxTempLimit;
      saveMax = true;
      float allowedSetTemp = min(maxTempLimit, MAX_SET_TEMP);
      if (setTemp > allowedSetTemp) {
        setTemp = allowedSetTemp;
        savedSetTemp = setTemp;
        saveSetTemp = true;
      }
    }
    heaterSafetyUnlock();
  }

  if (saveHyst) preferences.putFloat("hyst", savedHyst);
  if (savePre) preferences.putUInt("preFan", savedPre);
  if (savePost) preferences.putUInt("postFan", savedPost);
  if (saveMax) preferences.putFloat("maxTemp", savedMax);
  if (saveSetTemp) preferences.putFloat("setTemp", savedSetTemp);

  server.sendHeader("Location", "/");
  server.send(303);
}

String htmlEscape(const String &value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t index = 0; index < value.length(); index++) {
    const char character = value[index];
    if (character == '&') escaped += "&amp;";
    else if (character == '<') escaped += "&lt;";
    else if (character == '>') escaped += "&gt;";
    else if (character == '\"') escaped += "&quot;";
    else escaped += character;
  }
  return escaped;
}

String deviceConfigMessage;

void handleDeviceConfig() {
  const String requestedSsid = server.arg("wifiSsid");
  const String requestedPassword = server.arg("wifiPass");
  const String requestedPrinterIp = server.arg("printerIp");
  const bool clearWifiPassword = server.hasArg("clearWifiPass") && server.arg("clearWifiPass") == "1";

  if (requestedSsid.length() == 0 || !validPrinterIp(requestedPrinterIp)) {
    deviceConfigMessage = requestedSsid.length() == 0 ?
      "Enter a Wi-Fi network name." : "Enter a valid U1 IPv4 address.";
    server.sendHeader("Location", "/");
    server.send(303);
    return;
  }

  // A blank password keeps the previous secret unless the user explicitly
  // selects open Wi-Fi.  The saved secret is never sent back to the browser.
  String passwordToStore = requestedPassword;
  if (clearWifiPassword) passwordToStore = "";
  else if (passwordToStore.length() == 0 && userNetworkConfigured) passwordToStore = wifiPassword;

  preferences.putString("wifiSsid", requestedSsid);
  preferences.putString("wifiPass", passwordToStore);
  preferences.putString("printerIp", requestedPrinterIp);
  preferences.putBool("cfgReady", true);

  server.send(200, "text/html", "<html><body>Settings saved. Restarting ESP32...</body></html>");
  delay(250);
  ESP.restart();
}

void handleFactoryReset() {
  preferences.clear();
  server.send(200, "text/html", "<html><body>Factory Reset complete. Restarting in configuration mode...</body></html>");
  delay(250);
  WiFi.disconnect(true, true);
  ESP.restart();
}

void handleControllerRestart() {
  // Reuse the existing OTA safety handshake: the Safety Task, which owns the
  // local relay and fan, must acknowledge RELAY_PIN=LOW before a web-triggered
  // restart is permitted.
  if (!requestOtaSafetyLockout()) {
    server.send(503, "text/html", "<html><body>Safety lock unavailable. Restart rejected.</body></html>");
    return;
  }

  const uint32_t deadline = millis() + 500UL;
  while (!otaSafetyLockoutReady() && (int32_t)(millis() - deadline) < 0) delay(5);
  if (!otaSafetyLockoutReady()) {
    server.send(503, "text/html", "<html><body>Safety lock acknowledgement timed out. Restart rejected.</body></html>");
    return;
  }

  server.send(200, "text/html", "<html><body>Controller restarting...</body></html>");
  delay(300);
  ESP.restart();
}

bool otaRequestAuthorized() {
  return otaPassword.length() >= OTA_PASSWORD_MIN_LENGTH &&
         server.authenticate("chamber", otaPassword.c_str());
}

void handleOtaPassword() {
  const String requestedPassword = server.arg("otaPassword");
  if (requestedPassword.length() < OTA_PASSWORD_MIN_LENGTH) {
    deviceConfigMessage = "Firmware update password must have at least 8 characters.";
  } else {
    otaPassword = requestedPassword;
    preferences.putString("otaPass", otaPassword);
    deviceConfigMessage = "Firmware update password saved.";
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleOtaPage() {
  if (otaPassword.length() < OTA_PASSWORD_MIN_LENGTH) {
    server.send(403, "text/html", "<html><body>Set a firmware update password first.</body></html>");
    return;
  }
  if (!otaRequestAuthorized()) {
    server.requestAuthentication();
    return;
  }
  const char *page = R"rawliteral(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Chamber firmware update</title></head><body style="font-family:sans-serif;max-width:520px;margin:32px auto">
<h2>Chamber firmware update</h2>
<p>The heater will be locked OFF before the firmware is written. Select the regular Arduino <code>.ino.bin</code> file.</p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin,application/octet-stream" required><br><br>
<button type="submit">UPLOAD FIRMWARE</button>
</form><p><a href="/">Back to controller</a></p></body></html>)rawliteral";
  server.send(200, "text/html", page);
}

bool otaUploadAccepted = false;
bool otaUploadFailed = false;
String otaUploadMessage;

void handleOtaUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    otaUploadAccepted = false;
    otaUploadFailed = false;
    otaUploadMessage = "";

    if (!otaRequestAuthorized()) {
      otaUploadFailed = true;
      otaUploadMessage = "Unauthorized firmware upload.";
      return;
    }
    if (!requestOtaSafetyLockout()) {
      otaUploadFailed = true;
      otaUploadMessage = "Safety lock unavailable; upload rejected.";
      return;
    }

    const uint32_t deadline = millis() + 500UL;
    while (!otaSafetyLockoutReady() && (int32_t)(millis() - deadline) < 0) delay(5);
    if (!otaSafetyLockoutReady()) {
      otaUploadFailed = true;
      otaUploadMessage = "Safety lock acknowledgement timed out; restart Chamber before retrying.";
      return;
    }
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
      otaUploadFailed = true;
      otaUploadMessage = "Firmware update cannot start (no OTA partition or insufficient space).";
      return;
    }
    otaUploadAccepted = true;
    Serial.printf("OTA: upload started: %s\n", upload.filename.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (otaUploadAccepted && !otaUploadFailed &&
        Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaUploadFailed = true;
      otaUploadMessage = "Firmware write failed.";
      Update.abort();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (otaUploadAccepted && !otaUploadFailed) {
      if (Update.end(true) && Update.isFinished()) {
        otaUploadMessage = "Firmware uploaded successfully. Restarting Chamber...";
        Serial.printf("OTA: upload complete: %u bytes\n", upload.totalSize);
      } else {
        otaUploadFailed = true;
        otaUploadMessage = "Firmware verification failed.";
      }
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    otaUploadFailed = true;
    otaUploadMessage = "Firmware upload aborted.";
    Update.abort();
  }
}

void handleOtaUploadComplete() {
  if (!otaRequestAuthorized()) {
    server.requestAuthentication();
    return;
  }
  const bool success = otaUploadAccepted && !otaUploadFailed && Update.isFinished();
  server.send(success ? 200 : 500, "text/html", "<html><body>" + otaUploadMessage + "</body></html>");
  if (success) {
    delay(500);
    ESP.restart();
  }
}

// ---------------- GUI ----------------
void handleRoot() {
  uint32_t diagStartUs = micros();
  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);
  const float currentTemp = thermal.currentTemp;
  const float setTemp = thermal.setTemp;
  const float hyst = thermal.hyst;
  const float maxTempLimit = thermal.maxTempLimit;
  const unsigned long preFanTime = thermal.preFanTime;
  const unsigned long postFanTime = thermal.postFanTime;
  const bool masterOn = thermal.masterOn;
  const HeatingState chamberState = thermal.chamberState;
  const FaultDetails currentFault = thermal.currentFault;
  const FaultDetails lastFault = thermal.lastFault;
  const bool u1Online = moonrakerValid && (uint32_t)(millis() - lastMoonrakerSuccess) <= moonrakerWatchdogTimeout;
  const bool ntcOk = !thermal.thermistorFault;
  const char *topCoverText = !u1TopCoverDetectionValid ? "UNKNOWN" :
                             u1TopCoverDetected ? "CONNECTED" : "NOT DETECTED";
  const char *topCoverClass = !u1TopCoverDetectionValid ? "warn" :
                              u1TopCoverDetected ? "ok" : "muted";
  String html="";
  html += "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Snapmaker U1 Chamber Control</title>";
  html += "<style>";
  html += "*{box-sizing:border-box}body{font-family:'Segoe UI',Arial,sans-serif;background:#050a0f;color:#eafcff;margin:0;padding:12px;overflow-x:hidden}body:before{content:'';position:fixed;inset:0;background:radial-gradient(circle at 20% 0%,rgba(0,234,255,.10),transparent 32%),radial-gradient(circle at 90% 20%,rgba(0,255,157,.06),transparent 28%);pointer-events:none;z-index:-1}";
  html += ".dash{max-width:1880px;margin:auto;display:grid;grid-template-columns:350px 330px 285px minmax(0,650px);justify-content:center;gap:11px}.card{background:linear-gradient(145deg,#0b1821,#050d13);border:1px solid rgba(0,234,255,.30);border-radius:15px;padding:13px;box-shadow:inset 0 1px rgba(255,255,255,.04),0 0 24px rgba(0,234,255,.08)}";
  html += ".header{grid-column:1/-1;min-height:72px;display:flex;align-items:center;justify-content:space-between;gap:20px;padding:11px 18px;overflow:hidden}.brand{display:flex;align-items:center;gap:13px}.logo{width:47px;height:47px;color:#00eaff;filter:drop-shadow(0 0 8px rgba(0,234,255,.5))}.brand1{font-size:25px;font-weight:800;letter-spacing:2px}.brand2{font-size:13px;font-weight:800;letter-spacing:5px;color:#00eaff}.tag{font-size:11px;color:#75ffca;margin-top:4px}.headinfo{display:flex;align-items:center;gap:25px;font-size:11px}.headitem{display:flex;gap:8px;align-items:center}.hi{font-size:21px;color:#00eaff}.hl{opacity:.55;font-size:9px;text-transform:uppercase;letter-spacing:1px}.hv{font-weight:700;margin-top:2px}.hv.ok{color:#00ff9d}.hv.bad{color:#ff667a}.hv.warn{color:#ffd35a}.hv.muted{color:#9fb4ba}.maker{font-style:italic;color:#bdeaf2}.maker b{display:block;font-style:normal;font-size:16px;color:white}";
  html += ".title{font-size:21px;font-weight:800;color:#00eaff;text-shadow:0 0 8px rgba(0,234,255,.6);margin-bottom:4px}.sub{font-size:11px;color:#7faeb8;margin-bottom:11px}";
  html += ".faultNotice{display:none;margin:6px 0;padding:6px 8px;border:1px solid #ff1744;border-radius:8px;background:rgba(255,23,68,.12);color:#ff718a;font-size:11px;font-weight:800}.faultNotice.show{display:block}.faultHead{display:flex;align-items:center;justify-content:space-between;gap:8px}.faultInfo{font-size:10px;font-weight:500;line-height:1.35;margin-top:4px;color:#ffd5dc}.lastFault{border-color:rgba(255,184,66,.65);background:rgba(255,184,66,.08);color:#ffd37c}.lastFault .faultInfo{color:#ffe5af}.faultReset{color:#fff;background:#8f1730;border:1px solid #ff718a;border-radius:6px;padding:3px 7px;text-decoration:none;font-size:10px}";
  html += ".gauge{width:272px;height:272px;margin:1px auto 5px;border-radius:50%;position:relative;background:conic-gradient(from 220deg,#00eaff 0deg,var(--deg),rgba(0,234,255,.06) var(--deg),rgba(0,234,255,.06) 280deg,transparent 280deg)}.gauge:before{content:'';position:absolute;inset:14px;border-radius:50%;background:radial-gradient(circle,#0a1c25,#061017 70%);border:1px solid rgba(0,234,255,.25);box-shadow:inset 0 0 25px rgba(0,234,255,.10)}.gcontent{position:absolute;inset:0;z-index:2;display:flex;flex-direction:column;align-items:center;justify-content:center;text-align:center;padding-top:12px}.thermo{width:38px;height:38px;color:#ffd166;filter:drop-shadow(0 0 8px #ffd166)}.bigtemp{font-size:43px;font-weight:800;line-height:1;margin-top:4px}.clabel{font-size:12px;margin-top:6px}.line{width:135px;height:1px;background:rgba(0,234,255,.35);margin:6px}.tlabel{font-size:10px;text-transform:uppercase;letter-spacing:1px;color:#88aeb7}.target{font-size:25px;font-weight:800;color:#ffd700}.scale{position:absolute;z-index:3;left:18px;right:18px;bottom:14px;display:flex;justify-content:space-between;font-size:10px;color:#9bb5bd}";
  html += ".progress{margin:0 2px 8px}.phead{display:flex;justify-content:space-between;font-size:10px;color:#82aab4;margin-bottom:3px}.phead b{font-size:15px;color:white}.ptrack{height:8px;border-radius:7px;border:1px solid rgba(0,234,255,.35);background:#061017;overflow:hidden}.pfill{height:100%;width:0;background:linear-gradient(90deg,#ffd700 0%,#ffb000 45%,#ff8000 60%,#db4e4e 100%);box-shadow:0 0 10px #ffb000;transition:.35s}";
  html += ".iorow{display:grid;grid-template-columns:1fr 1fr;gap:7px}.io{border:1px solid rgba(0,234,255,.35);border-radius:10px;padding:5px;text-align:center;background:#07131a}.io.heater-card{border-color:rgba(0,255,157,.55)}.io.heater-card.on{border-color:#ff1744;box-shadow:0 0 15px rgba(255,23,68,.15)}.iolabel{font-size:11px}.iostate{font-size:18px;font-weight:800}.heater-icon{height:43px;display:flex;align-items:flex-end;justify-content:center;gap:3px;position:relative}.bar{width:5px;border-radius:4px 4px 1px 1px;background:#00ff66;box-shadow:0 0 7px #00ff66;opacity:.8;animation:hi 1.6s ease-in-out infinite}.bar:nth-child(1){height:16px;animation-delay:0s}.bar:nth-child(2){height:24px;animation-delay:.08s}.bar:nth-child(3){height:32px;animation-delay:.16s}.bar:nth-child(4){height:40px;animation-delay:.24s}.bar:nth-child(5){height:32px;animation-delay:.32s}.bar:nth-child(6){height:24px;animation-delay:.40s}.bar:nth-child(7){height:16px;animation-delay:.48s}.heater-icon.on .bar{background:#ff1744;box-shadow:0 0 10px #ff1744;animation:heat .48s ease-in-out infinite alternate}.heater-icon.on .bar:nth-child(1){animation-delay:0s}.heater-icon.on .bar:nth-child(2){animation-delay:.06s}.heater-icon.on .bar:nth-child(3){animation-delay:.12s}.heater-icon.on .bar:nth-child(4){animation-delay:.18s}.heater-icon.on .bar:nth-child(5){animation-delay:.24s}.heater-icon.on .bar:nth-child(6){animation-delay:.30s}.heater-icon.on .bar:nth-child(7){animation-delay:.36s}@keyframes hi{0%,100%{transform:scaleY(.82);opacity:.58}50%{transform:scaleY(1);opacity:1}}@keyframes heat{from{transform:scaleY(.62) translateY(3px);opacity:.48;filter:brightness(.9)}to{transform:scaleY(1.10) translateY(-1px);opacity:1;filter:brightness(1.3)}}";
  html += ".fan-icon{height:43px;position:relative;width:55px;margin:auto;overflow:hidden}.rotor{position:absolute;width:43px;height:43px;left:50%;top:50%;transform:translate(-50%,-50%);transform-origin:50% 50%}.blade{position:absolute;left:18.5px;top:0;width:6px;height:20px;border-radius:8px;background:#00eaff;box-shadow:0 0 8px #00eaff;transform-origin:3px 21.5px;opacity:.82}.blade:nth-child(1){transform:rotate(0)}.blade:nth-child(2){transform:rotate(60deg)}.blade:nth-child(3){transform:rotate(120deg)}.blade:nth-child(4){transform:rotate(180deg)}.blade:nth-child(5){transform:rotate(240deg)}.blade:nth-child(6){transform:rotate(300deg)}.fc{position:absolute;left:14.5px;top:14.5px;width:14px;height:14px;border-radius:50%;background:#00eaff;box-shadow:0 0 10px #00eaff;z-index:3}.fan-icon.on .rotor{animation:spin .78s linear infinite}.fan-icon.on .blade{opacity:1;box-shadow:0 0 12px #00eaff}.fan-icon.on .fc{box-shadow:0 0 15px #00eaff}@keyframes spin{to{transform:translate(-50%,-50%) rotate(360deg)}}";
  html += ".status{margin:6px 0;border-radius:9px;padding:6px 9px;border:1px solid rgba(0,234,255,.4);display:flex;align-items:center;gap:8px;background:rgba(0,234,255,.06)}.dot{width:10px;height:10px;border-radius:50%;background:#00eaff;box-shadow:0 0 9px currentColor}.smain{font-size:13px;font-weight:800}.ssub{font-size:9px;opacity:.65}.controls{display:grid;grid-template-columns:1fr;gap:7px}.btn{min-height:36px;border-radius:9px;display:flex;align-items:center;justify-content:center;text-decoration:none;font-weight:800;font-size:14px}.start{background:#00e99a;color:#003318;box-shadow:0 0 13px rgba(0,255,157,.4)}.stop{background:#50121f;color:#ff819e;border:1px solid #ff416b}";
  html += ".modeRow{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:10px 0 40px}";
  html += ".modeBtn{display:flex;align-items:center;justify-content:center;min-height:45px;border-radius:10px;border:1px solid #53616a;background:linear-gradient(180deg,#53616a,#3d4850);color:#fff;text-decoration:none;font-size:16px;font-weight:800;box-shadow:0 0 8px rgba(83,97,106,.20)}";
  html += ".klipperBtn{border-color:#53616a;background:linear-gradient(180deg,#53616a,#3d4850);box-shadow:0 0 8px rgba(83,97,106,.20)}";
  html += ".modeBtn.active{border-color:#00ffff;background:linear-gradient(180deg,#00d9d9,#00bfc8);box-shadow:0 0 14px rgba(0,255,255,.60),inset 0 0 8px rgba(0,255,255,.20)}";
  html += ".klipperBtn.active{border-color:#00ffff;background:linear-gradient(180deg,#00d9d9,#00bfc8);box-shadow:0 0 14px rgba(0,255,255,.60),inset 0 0 8px rgba(0,255,255,.20)}";
  html += ".bedassist{position:relative;display:flex;align-items:center;gap:8px;height:28px;margin:-31px 0 10px;font-size:11px;font-weight:800;color:#b9d7df}.bedBtn{position:absolute;right:calc(50% + 4px);min-width:76px;min-height:28px;border-radius:8px;border:1px solid #53616a;background:#26323a;color:#dceff4;display:flex;align-items:center;justify-content:center;text-decoration:none;font-size:12px}.bedBtn.on{border-color:#ff5a36;background:#7b1e25;color:#fff;box-shadow:0 0 10px rgba(255,90,54,.45)}";
  html += ".preheatState{height:28px;margin:-31px 0 10px;font-size:11px;font-weight:800;color:#b9d7df}.preheatState.ready{color:#00ff9d;text-shadow:0 0 8px rgba(0,255,157,.45)}.virtualU1{margin-top:10px;padding:8px;border:1px solid rgba(255,176,0,.6);border-radius:8px;background:rgba(255,176,0,.08);font-size:10px;color:#ffd37c}.virtualU1 b{display:block;margin-bottom:5px}.virtualU1 a{display:inline-block;margin:3px 4px 0 0;padding:5px 6px;border:1px solid #ffb000;border-radius:5px;color:#fff1c2;text-decoration:none}.virtualU1 .off{border-color:#ff667a;color:#ffe1e7}";
  html += ".quick{display:grid;grid-template-columns:1fr 1.3fr 1fr;gap:7px;margin:14px 0 9px}.qbtn{min-height:45px;border-radius:9px;background:linear-gradient(#0aabef,#087dcc);color:#fff;border:1px solid rgba(0,234,255,.5);display:flex;align-items:center;justify-content:center;text-decoration:none;font-size:20px;font-weight:800}.qval{border:1px solid rgba(0,234,255,.45);border-radius:9px;display:flex;align-items:center;justify-content:center;font-size:24px;font-weight:800;white-space:nowrap;background:#07151d}.presets{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.preset{min-height:62px!important;flex-direction:column;font-size:17px;color:#000}.preset small{font-size:9px;opacity:.8;margin-top:6px}.preset:nth-child(1){background:linear-gradient(#fff275,#ffd700);border-color:#ffd700}.preset:nth-child(2){background:linear-gradient(#ffb075,#ff8000);border-color:#ff8000}.preset:nth-child(3){background:linear-gradient(#f55656,#db4e4e);border-color:#db4e4e;color:#000}";
  html += ".settings{display:grid;grid-template-columns:1fr 1fr;gap:8px}.setting label{display:flex;justify-content:space-between;font-size:11px;margin-bottom:4px}.unit{opacity:.5;font-size:9px}.setting input{width:100%;height:33px;background:#07131a;color:white;border:1px solid #24414a;border-radius:8px;padding:6px 7px;font-size:14px}.save{grid-column:1/-1;height:36px;border:0;border-radius:9px;background:#00e99a;color:#003318;font-weight:900;font-size:13px}.saved{display:none;text-align:center;color:#00ff9d;font-size:10px;font-weight:700}";
  html += ".devicecfg{margin-top:13px;padding-top:11px;border-top:1px solid rgba(0,234,255,.2)}.connectionToggle{width:100%;display:flex;align-items:center;justify-content:space-between;margin:0;padding:0;border:0;background:transparent;color:#00eaff;font:inherit;font-size:13px;font-weight:700;text-align:left;cursor:pointer}.connectionArrow{font-size:16px;line-height:1;transition:transform .15s ease}.connectionToggle[aria-expanded='true'] .connectionArrow{transform:rotate(90deg)}.connectionPanel{padding-top:8px}.devicecfg .hint{margin:0 0 8px;font-size:10px;color:#8fb7c0;line-height:1.35}.devicecfg .notice{color:#ffd35a}.devicecfg .check{grid-column:1/-1;display:flex;align-items:center;gap:7px;font-size:11px;color:#d9f2f7}.devicecfg .check input{width:auto;height:auto}.ota{display:block;margin-top:9px;padding:8px;text-align:center;border:1px solid #00d9ff;border-radius:8px;background:linear-gradient(135deg,#005a8c,#00a9e8);color:#e8fbff;font-size:11px;font-weight:800;text-decoration:none;box-shadow:0 0 10px rgba(0,217,255,.30)}.reboot{display:block;margin-top:9px;padding:8px;text-align:center;border:1px solid #ff1744;border-radius:8px;background:linear-gradient(135deg,#7a0018,#d50032);color:#fff0f3;font-size:11px;font-weight:800;text-decoration:none;box-shadow:0 0 10px rgba(255,23,68,.28)}.factory{display:block;margin-top:9px;text-align:center;color:#ff9d9d;font-size:11px;text-decoration:none}";
  html += ".chartcard{min-width:0}.charttop{display:flex;justify-content:space-between;gap:12px;margin-bottom:5px}.metrics{display:grid;grid-template-columns:repeat(3,1fr);gap:7px;min-width:360px}.metric{border:1px solid rgba(0,234,255,.3);border-radius:7px;padding:5px;text-align:center}.metric:nth-child(2){border-color:rgba(255,170,0,.35)}.ml{font-size:9px;opacity:.65}.mv{font-size:17px;font-weight:800;color:#00eaff}.metric:nth-child(2) .mv{color:#ffaa00}.chart{height:185px;position:relative}.chart.iochart{height:88px}.chartlabel{font-size:14px;color:#00eaff;font-weight:700;margin:5px 0}.footer{grid-column:1/-1;display:grid;grid-template-columns:1fr 1fr 1fr auto;gap:15px;align-items:center;padding:6px 17px;font-size:10px}.footer b{display:block;color:#eefcff;font-size:11px}.footer span{color:#55b9ca}.flogo{font-size:15px;font-weight:800;text-align:right;letter-spacing:1px}.flogo small{display:block;font-size:8px;color:#6d9da7}";
  html += R"rawliteral(
/* ===== V6.4 ADAPTIVE: 100% browser / short viewport ===== */
@media (min-width:1501px) and (max-height:1000px){
  body{padding:7px}
  .dash{gap:8px}
  .header{height:64px;min-height:64px;padding:7px 15px}
  .logo{width:42px;height:42px}
  .brand{gap:10px}
  .brand1{font-size:22px}
  .brand2{font-size:11px;letter-spacing:4px}
  .tag{font-size:9px;margin-top:2px}
  .headinfo{gap:17px;font-size:10px}
  .hi{font-size:18px}
  .maker b{font-size:14px}
  .card{padding:10px}
  .title{font-size:19px;margin-bottom:3px}
  .sub{font-size:10px;margin-bottom:7px}
  .gauge{width:238px;height:238px;margin:0 auto 4px}
  .gcontent{padding-top:9px}
  .thermo{width:32px;height:32px}
  .bigtemp{font-size:38px}
  .clabel{font-size:11px;margin-top:4px}
  .line{width:120px;margin:4px}
  .tlabel{font-size:9px}
  .target{font-size:22px}
  .scale{left:15px;right:15px;bottom:11px;font-size:9px}
  .progress{margin:0 1px 6px}
  .phead{margin-bottom:2px}
  .phead b{font-size:14px}
  .ptrack{height:7px}
  .iorow{gap:6px}
  .io{padding:4px}
  .heater-icon,.fan-icon{height:35px;overflow:hidden}
  .heater-icon{gap:3px}
  .heater-icon .bar{width:4px}
  .heater-icon .bar:nth-child(1){height:13px}
  .heater-icon .bar:nth-child(2){height:20px}
  .heater-icon .bar:nth-child(3){height:27px}
  .heater-icon .bar:nth-child(4){height:34px}
  .heater-icon .bar:nth-child(5){height:27px}
  .heater-icon .bar:nth-child(6){height:20px}
  .heater-icon .bar:nth-child(7){height:13px}
  .fan-icon{width:44px;height:35px;overflow:hidden}
  .fan-icon .rotor{width:35px;height:35px}
  .fan-icon .blade{left:14.5px;width:5px;height:16px;transform-origin:2.5px 17.5px}
  .fan-icon .fc{left:11.5px;top:11.5px;width:12px;height:12px}
  .iolabel{font-size:10px}
  .iostate{font-size:16px}
  .status{margin:5px 0;padding:5px 8px}
  .smain{font-size:12px}
  .ssub{font-size:8px}
  .controls{gap:6px}
  .btn{min-height:33px;font-size:13px}
  .quick{margin:11px 0 7px;gap:6px}
  .qbtn{min-height:41px;font-size:19px}
  .qval{font-size:22px}
  .presets{gap:6px}
  .preset{min-height:56px!important;font-size:16px}
  .preset small{font-size:8px;margin-top:4px}
  .settings{gap:7px}
  .setting label{font-size:10px;margin-bottom:3px}
  .unit{font-size:8px}
  .setting input{height:31px;font-size:13px}
  .save{height:33px;font-size:12px}
  .charttop{margin-bottom:3px}
  .metrics{gap:5px;min-width:330px}
  .metric{padding:4px}
  .ml{font-size:8px}
  .mv{font-size:15px}
  .chart{height:165px}
  .chart.iochart{height:75px}
  .chartlabel{font-size:13px;margin:4px 0}
  .footer{padding:4px 14px;font-size:9px}
  .footer b{font-size:10px}
  .flogo{font-size:13px}
  .flogo small{font-size:7px}
}

/* Very short desktop viewport */
@media (min-width:1501px) and (max-height:850px){
  body{padding:5px}
  .header{height:58px;min-height:58px;padding:5px 12px}
  .logo{width:37px;height:37px}
  .brand1{font-size:20px}
  .brand2{font-size:10px}
  .tag{display:none}
  .card{padding:8px}
  .title{font-size:18px}
  .sub{font-size:9px;margin-bottom:5px}
  .gauge{width:215px;height:215px}
  .bigtemp{font-size:35px}
  .thermo{width:29px;height:29px}
  .target{font-size:20px}
  .scale{font-size:8px;bottom:9px}
  .heater-icon,.fan-icon{height:35px}
  .status{margin:4px 0;padding:4px 7px}
  .btn{min-height:30px}
  .quick{margin:8px 0 5px}
  .qbtn{min-height:37px}
  .preset{min-height:51px!important}
  .setting input{height:29px}
  .save{height:30px}
  .chart{height:150px}
  .chart.iochart{height:65px}
  .footer{padding:3px 12px}
}
)rawliteral";
  // V6.7 GUI: compact desktop tiles / one-column mobile layout. Control logic is unchanged.
  html += R"rawliteral(
/* ===== V6.7 COMPACT HORIZONTAL DASHBOARD ===== */
.dash{
  max-width:1200px;
  grid-template-columns:330px 330px minmax(520px,1fr);
  grid-template-rows:auto auto;
  align-items:stretch;
  gap:14px;
  margin:0 auto;
}

.header,
.footer{
  grid-column:1/-1;
}

.statuscard{
  grid-column:1;
  grid-row:2 / span 2;
  align-self:stretch;
}

.quickcard{
  grid-column:2;
  grid-row:2;
  align-self:start;
}

.configcard{
  grid-column:2;
  grid-row:3;
  align-self:start;
}

.chartcard{
  grid-column:3;
  grid-row:2 / span 2;
  align-self:stretch;
}
.quickcard .title,.configcard .title{font-size:19px}
.quickcard .sub,.configcard .sub{margin-bottom:8px}
.quickcard .quick{margin:8px 0}
.quickcard .qbtn{min-height:40px}
.quickcard .preset{min-height:52px!important}
.configcard .settings{gap:6px}
.configcard .setting input{height:31px}
.configcard .save{height:34px}
.chartcard .chart{height:220px}
.chartcard .chart.iochart{height:105px}
.chartcard .charttop{margin-bottom:14px}
.chartcard .chartlabel{margin:12px 0 10px}
/* Quieter three-blade turbine icon; rotation is still driven only by the reported FAN state. */
.fan-icon{width:48px;height:48px;border:2px solid rgba(0,234,255,.38);border-radius:50%;background:radial-gradient(circle,#10313d 0 16%,#07131a 18% 100%)}
.fan-icon .rotor{width:40px;height:40px}
.fan-icon .blade{left:13px;top:1px;width:14px;height:19px;border-radius:90% 20% 90% 20%;transform-origin:7px 19px;background:linear-gradient(90deg,#00a8d1,#5df5ff)}
.fan-icon .blade:nth-child(1){transform:rotate(0deg)}
.fan-icon .blade:nth-child(2){transform:rotate(120deg)}
.fan-icon .blade:nth-child(3){transform:rotate(240deg)}
.fan-icon .fc{left:15px;top:15px;width:14px;height:14px;background:#d9fbff;border:2px solid #00b9db;box-shadow:0 0 8px #00eaff}
/* Thermal core: green idle energy, red expanding heat waves while the heater is ON. */
.gauge{background:conic-gradient(from 220deg,var(--gaugeColor,#00eaff) 0deg,var(--gaugeColor,#00eaff) var(--deg),rgba(0,234,255,.06) var(--deg),rgba(0,234,255,.06) 280deg,transparent 280deg)}
.heater-icon{height:48px;display:flex;align-items:center;justify-content:center;position:relative}
.heater-orbit{width:42px;height:42px;position:relative;border:2px solid rgba(0,255,102,.48);border-radius:50%;background:radial-gradient(circle,#10351f 0 18%,#071a13 19% 100%);box-shadow:inset 0 0 10px rgba(0,255,102,.18),0 0 7px rgba(0,255,102,.16)}
.heat-ring{position:absolute;border:2px solid #00ff66;border-radius:50%;opacity:.58;animation:heatIdle 2.2s ease-in-out infinite}
.heat-ring.r1{inset:4px}.heat-ring.r2{inset:10px;animation-delay:.22s}.heat-ring.r3{inset:16px;animation-delay:.44s}
.heat-core{position:absolute;left:15px;top:15px;width:10px;height:10px;border-radius:50%;background:#d8ffe8;box-shadow:0 0 9px #00ff66}
.heater-icon.on .heater-orbit{border-color:rgba(255,23,68,.7);background:radial-gradient(circle,#41111d 0 18%,#1d0810 19% 100%);box-shadow:inset 0 0 12px rgba(255,23,68,.24),0 0 11px rgba(255,23,68,.35)}
.heater-icon.on .heat-ring{border-color:#ff1744;animation:heatWave .85s ease-out infinite}
.heater-icon.on .heat-ring.r2{animation-delay:.18s}.heater-icon.on .heat-ring.r3{animation-delay:.36s}
.heater-icon.on .heat-core{background:#fff0f2;box-shadow:0 0 13px #ff1744;animation:heatCore .62s ease-in-out infinite alternate}
@keyframes heatIdle{0%,100%{opacity:.35;transform:scale(.92)}50%{opacity:.78;transform:scale(1)}}
@keyframes heatWave{0%{opacity:.92;transform:scale(.78)}100%{opacity:.12;transform:scale(1.16)}}
@keyframes heatCore{from{transform:scale(.82)}to{transform:scale(1.28)}}
.metrics{grid-template-columns:repeat(5,1fr);min-width:0}
.metric.cavity{border-color:rgba(183,131,255,.55)}.metric.cavity .mv{color:#b783ff}.metric.bed{border-color:rgba(255,90,54,.55)}.metric.bed .mv{color:#ff775d}.bedheat{display:block;font-size:8px;font-weight:800;color:#ff775d}
@media(max-width:1320px){.dash{max-width:980px;grid-template-columns:330px minmax(300px,1fr);justify-content:stretch}.header,.footer{grid-column:1/-1}.statuscard{grid-column:1}.quickcard{grid-column:2}.configcard{grid-column:2}.chartcard{grid-column:1/-1}.chartcard .chart{height:185px}}
@media(max-width:760px){body{padding:8px}.dash{max-width:560px;display:flex;flex-direction:column;gap:9px}.card,.statuscard,.quickcard,.configcard,.chartcard,.header,.footer{width:100%;grid-column:auto}.header{min-height:0}.headinfo,.maker{display:none}.brand1{font-size:20px}.brand2{font-size:10px;letter-spacing:3px}.gauge{width:246px;height:246px}.quickcard .quick{margin:8px 0}.metrics{grid-template-columns:repeat(2,1fr);min-width:0;gap:5px}.metric{padding:4px}.mv{font-size:14px}.charttop{flex-direction:column}.chartcard .chart{height:180px}.chartcard .chart.iochart{height:72px}.footer{display:none}.settings{grid-template-columns:1fr 1fr}.fan-icon{width:46px;height:46px}}
)rawliteral";
  html += "</style></head><body><div class='dash'>";

  // HEADER
  html += "<div class='card header'><div class='brand'><svg class='logo' viewBox='0 0 64 64' fill='none' stroke='currentColor' stroke-width='3'><path d='M32 5 55 18v27L32 58 9 45V18L32 5Z'/><path d='m9 18 23 13 23-13M32 31v27'/></svg><div><div class='brand1'>SNAPMAKER U1</div><div class='brand2'>CHAMBER CONTROL</div><div class='tag'>Higher temperatures. Greater possibilities.</div></div></div><div class='headinfo'><div class='headitem'><span class='hi'>⌁</span><div><div class='hl'>Controller</div><div class='hv'>ESP32-S3</div></div></div><div class='headitem'><span class='hi'>◉</span><div><div class='hl'>Network</div><div class='hv'>WiFi</div></div></div><div class='headitem'><span class='hi'>⇄</span><div><div class='hl'>U1 link</div><div class='hv "+String(u1Online ? "ok" : "bad")+"' id='u1LinkState'>"+String(u1Online ? "ONLINE" : "OFFLINE")+"</div></div></div><div class='headitem'><span class='hi'>⌂</span><div><div class='hl'>TopCover</div><div class='hv "+String(topCoverClass)+"' id='topCoverState'>"+String(topCoverText)+"</div></div></div><div class='headitem'><span class='hi'>♨</span><div><div class='hl'>NTC100</div><div class='hv "+String(ntcOk ? "ok" : "bad")+"' id='ntcState'>"+String(ntcOk ? "OK" : "FAULT")+"</div></div></div><div class='headitem'><span class='hi'>◷</span><div><div class='hl'>Local time</div><div class='hv' id='clock'>--:--:--</div></div></div><div class='maker'><b>Make More</b>Create Better</div></div></div>";

  // MAIN
  html += "<div class='card statuscard'><div class='title'>Chamber Status</div><div class='sub'>Snapmaker U1 Enclosure</div><div class='gauge' id='gauge' style='--deg:0deg'><div class='gcontent'><svg class='thermo' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2'><path d='M14 14.76V5a2 2 0 1 0-4 0v9.76a4 4 0 1 0 4 0Z'/><path d='M12 12v6'/></svg><div class='bigtemp' id='currentTemp'>"+String(currentTemp,1)+"°C</div><div class='clabel'>Current temperature</div><div class='line'></div><div class='tlabel'>Target temperature</div><div class='target' id='targetTemp'>"+String(setTemp,1)+"°C</div></div><div class='scale'><span>0°C</span><span>75°C</span></div></div>";
  html += "<div class='progress'><div class='phead'><span>Chamber progress</span><b id='progressPct'>0%</b></div><div class='ptrack'><div class='pfill' id='progressFill'></div></div></div>";
  html += "<div class='iorow'><div class='io heater-card' id='heaterCard'><div class='heater-icon' id='heaterIcon'><div class='heater-orbit'><span class='heat-ring r1'></span><span class='heat-ring r2'></span><span class='heat-ring r3'></span><span class='heat-core'></span></div></div><div class='iolabel'>Heater</div><div class='iostate' id='heaterState' style='color:#00ff9d'>OFF</div></div><div class='io' id='fanCard'><div class='fan-icon' id='fanIcon'><div class='rotor'>";for(int i=0;i<3;i++)html+="<span class='blade'></span>";html+="</div><div class='fc'></div></div><div class='iolabel'>Fan</div><div class='iostate' id='fanState' style='color:#00eaff'>OFF</div></div></div>";
  html += "<div class='status' id='statusBar'><span class='dot' id='statusDot'></span><div><div class='smain' id='statusMain'>"+stateText(chamberState)+"</div><div class='ssub' id='statusSub'>Chamber heating is off</div></div></div><div id='faultNotice' class='faultNotice"+String(thermal.faultLatched ? " show" : "")+"'><div class='faultHead'><span>FAULT: <span id='faultReason'>"+String(safetyFaultReasonText(thermal.faultReason))+"</span></span><a class='faultReset' id='faultResetBtn' href='/faultreset'>RESET FAULT</a></div><div class='faultInfo' id='faultInfo'>"+faultDetailsHtml(currentFault)+"</div></div><div id='lastFaultNotice' class='faultNotice lastFault"+String(lastFault.valid ? " show" : "")+"'><div>LAST FAULT: <span id='lastFaultReason'>"+String(safetyFaultReasonText(lastFault.reason))+"</span></div><div class='faultInfo' id='lastFaultInfo'>"+faultDetailsHtml(lastFault)+"</div></div><div class='controls'>";
  if(masterOn) html+="<a class='btn stop' href='/toggle'>■ STOP</a>"; else html+="<a class='btn start' href='/toggle'>▶ START</a>";
  html += "<div></div></div></div>";

  // QUICK
  html += "\<div class='card quickcard'>\<div class='title'>Quick target temperature\</div>\<div class='sub'>Set target temperature for chamber\</div>\<div class='modeRow'>\<a class='modeBtn"+String(!preheatMode ? " active" : "")+"' href='/mode?m=manual' onclick=\"return selectMode(this)\">MANUAL\</a>\<a class='modeBtn klipperBtn"+String(preheatMode ? " active" : "")+"' href='/mode?m=preheat' onclick=\"return selectMode(this)\">PREHEAT\</a>\</div>";
  if (preheatMode) html += "\<div class='preheatState"+String(preheatReady ? " ready" : "")+"' id='preheatState'>PREHEAT: "+String(preheatReady ? "READY" : "WARMING")+"\</div>";
  html += "\<div class='quick'>\<a class='qbtn' href='/adjust?d=-1'>−\</a>\<div class='qval' id='quickValue'>"+String(setTemp,1)+"°C\</div>\<a class='qbtn' href='/adjust?d=1'>+\</a>\</div>\<div class='presets'>\<a class='qbtn preset' href='/set?st=50'>50°C\<small>PETG\</small>\</a>\<a class='qbtn preset' href='/set?st=55'>55°C\<small>ABS / ASA\</small>\</a>\<a class='qbtn preset' href='/set?st=60'>60°C\<small>PC / Nylon\</small>\</a>\</div>\</div>";

  // SETTINGS
  html += "<div class='card configcard'><div class='title'>Advanced settings</div><div class='sub'>Configure thermal management</div><form action='/cfg' method='GET' class='settings'><div class='setting'><label>Hysteresis <span class='unit'>°C</span></label><input name='hyst' type='number' min='0.1' max='10' step='0.1' value='"+String(hyst,1)+"'></div><div class='setting'><label>Pre-fan <span class='unit'>s</span></label><input name='pre' type='number' min='0' max='600' value='"+String(preFanTime/1000)+"'></div><div class='setting'><label>Post-fan <span class='unit'>s</span></label><input name='post' type='number' min='0' max='600' value='"+String(postFanTime/1000)+"'></div><div class='setting'><label>Temperature limit <span class='unit'>°C</span></label><input name='max' type='number' min='1' max='75' step='0.5' value='"+String(maxTempLimit,1)+"'></div><button class='save' type='submit'>▣ &nbsp; SAVE SETTINGS</button></form><div class='saved' id='savedMsg'>✓ SETTINGS SAVED</div>";
  html += "<div class='devicecfg'><button type='button' class='connectionToggle' id='connectionToggle' aria-expanded='false' aria-controls='connectionPanel'><span>Controller connection</span><span class='connectionArrow' aria-hidden='true'>▸</span></button><div class='connectionPanel' id='connectionPanel' hidden><p class='hint'>";
  if (configPortalActive) html += "<span class='notice'>Configuration mode: connect to Wi-Fi U1-Chamber-Setup, then open 192.168.4.1.</span>";
  else html += "Wi-Fi and U1 Moonraker address. TopCover is detected automatically by U1.";
  html += "</p><form action='/devicecfg' method='POST' class='settings'><div class='setting'><label>Wi-Fi network</label><input name='wifiSsid' maxlength='32' value='" + htmlEscape(wifiSsid) + "' required></div><div class='setting'><label>Wi-Fi password</label><input name='wifiPass' type='password' maxlength='64' placeholder='Leave blank to keep saved'></div><div class='setting'><label>U1 IP address</label><input name='printerIp' inputmode='decimal' maxlength='15' value='" + htmlEscape(printerIp) + "' placeholder='192.168.x.x' required></div><label class='check'><input name='clearWifiPass' value='1' type='checkbox'> Use open Wi-Fi network (clear saved password)</label><button class='save' type='submit'>SAVE CONNECTION SETTINGS</button></form><form action='/otapassword' method='POST' class='settings'><div class='setting'><label>Firmware update password</label><input name='otaPassword' type='password' minlength='8' maxlength='64' placeholder='At least 8 characters' required></div><div class='setting'><label>OTA status</label><input value='" + String(otaPassword.length() >= OTA_PASSWORD_MIN_LENGTH ? "Password configured" : "Password required") + "' disabled></div><button class='save' type='submit'>SAVE UPDATE PASSWORD</button></form><a class='ota' href='/update'>FIRMWARE UPDATE (Wi-Fi)</a><a class='reboot' href='/restart' onclick=\"return confirm('Restart Chamber controller? Heating will be safely stopped.')\">RESTART CONTROLLER</a>";
  const bool virtualU1 = virtualU1IsEnabled();
  html += "<div class='virtualU1'><b>TEST ONLY — Virtual U1: " + String(virtualU1 ? "ON" : "OFF") + "</b>";
  html += "When ON, Chamber sends no HTTP or G-code to the real U1. ";
  if (virtualU1) {
    html += "<a href='/virtualu1?mode=on&state=standby&topcover=1'>STANDBY + COVER</a><a href='/virtualu1?mode=on&state=standby&topcover=0'>STANDBY NO COVER</a><a href='/virtualu1?mode=on&state=printing&topcover=1'>SIMULATE PRINTING</a><a href='/virtualu1?mode=on&state=complete&topcover=1'>SIMULATE COMPLETE</a><a class='off' href='/virtualu1?mode=off'>EXIT VIRTUAL U1</a>";
  } else {
    html += "<a href='/virtualu1?mode=on&state=standby&topcover=1' onclick=\"return confirm('Enable Virtual U1? Real U1 HTTP and G-code commands will be ignored until you exit test mode.')\">ENABLE VIRTUAL U1</a>";
  }
  html += "</div>";
  if (deviceConfigMessage.length() > 0) html += "<p class='hint notice'>" + htmlEscape(deviceConfigMessage) + "</p>";
  html += "<a class='factory' href='/factoryreset' onclick=\"return confirm('Delete Wi-Fi, U1 IP and all saved controller settings?')\">FACTORY RESET</a></div></div></div>";

  // CHARTS
  html += "<div class='card chartcard'><div class='charttop'><div><div class='title'>Temperature history</div><div class='sub'>Real-time chamber temperature</div></div><div class='metrics'><div class='metric'><div class='ml'>Chamber</div><div class='mv' id='metricCurrent'>"+String(currentTemp,1)+"°C</div></div><div class='metric'><div class='ml'>Target</div><div class='mv' id='metricTarget'>"+String(setTemp,1)+"°C</div></div><div class='metric'><div class='ml'>Δ To target</div><div class='mv' id='metricDelta'>--</div></div></div></div><div class='chart'><canvas id='chartTemp'></canvas></div><div class='chartlabel'>Heater / Fan (ON/OFF)</div><div class='chart iochart'><canvas id='chartIO'></canvas></div></div>";

  // FOOTER
  html += "<div class='card footer'><div>♨ <b>Stable Temperature</b><span>Better Prints</span></div><div>▱ <b>Wide Material Compatibility</b><span>PLA · PETG · ABS · ASA · PC · Nylon</span></div><div>◇ <b>Custom Solution</b><span>By Users. For Makers.</span></div><div class='flogo'>SNAPMAKER<small>MAKE SOMETHING WONDERFUL</small></div></div></div>";

  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script><script>";
  html += "function toggleControllerConnection(){let b=document.getElementById('connectionToggle'),p=document.getElementById('connectionPanel'),open=b.getAttribute('aria-expanded')==='true';b.setAttribute('aria-expanded',open?'false':'true');p.hidden=open}document.getElementById('connectionToggle').addEventListener('click',toggleControllerConnection);let labels=[],tempData=[],heaterData=[],fanData=[],targetData=[],cavityData=[],bedData=[],historyLastSeq=null,historyBusy=false;let ctxT=document.getElementById('chartTemp').getContext('2d'),ctxI=document.getElementById('chartIO').getContext('2d');";
  html += "let chartTemp=new Chart(ctxT,{type:'line',data:{labels:labels,datasets:[{label:'Chamber temperature',data:tempData,borderColor:'#00eaff',backgroundColor:'rgba(0,234,255,.08)',borderWidth:3,tension:.25,pointRadius:0,fill:true},{label:'Target temperature',data:targetData,borderColor:'#ffaa00',borderDash:[7,5],borderWidth:2,pointRadius:0}]},options:{responsive:true,maintainAspectRatio:false,animation:false,scales:{x:{ticks:{color:'#9ebbc3',maxTicksLimit:8},grid:{color:'rgba(90,160,175,.1)'}},y:{ticks:{color:'#9ebbc3'},suggestedMin:0,suggestedMax:80,grid:{color:'rgba(90,160,175,.1)'}}},plugins:{legend:{labels:{color:'#e8f8fb',usePointStyle:true}}}}});";
  html += "let chartIO=new Chart(ctxI,{type:'line',data:{labels:labels,datasets:[{label:'Heater',data:heaterData,borderColor:'#ff9d16',borderWidth:2,stepped:true,pointRadius:0},{label:'Fan',data:fanData,borderColor:'#00ff9d',borderWidth:2,stepped:true,pointRadius:0}]},options:{responsive:true,maintainAspectRatio:false,animation:false,scales:{x:{ticks:{color:'#9ebbc3',maxTicksLimit:8}},y:{min:-.1,max:1.1,ticks:{color:'#cfe8ed',callback:v=>v===1?'ON':v===0?'OFF':''}}},plugins:{legend:{labels:{color:'#e8f8fb',usePointStyle:true}}}}});";
  html += "function si(s){return[{m:'IDLE',x:'Chamber heating is off',c:'#00eaff'},{m:'PRE-FAN',x:'Preparing airflow before heating',c:'#ffaa00'},{m:'HEATING ACTIVE',x:'Heater is actively warming the chamber',c:'#ff5a36'},{m:'COOLING',x:'Post-heating cooling cycle',c:'#00ff9d'}][s]||{m:'UNKNOWN',x:'Controller state',c:'#888'};}";
  html += "function hlabel(t,n){return new Date(Date.now()-(n-t)).toLocaleTimeString()}function htrim(){while(labels.length>720){labels.shift();tempData.shift();targetData.shift();cavityData.shift();bedData.shift();heaterData.shift();fanData.shift()}}function happly(d,replace){if(replace||d.reset){labels.length=tempData.length=targetData.length=cavityData.length=bedData.length=heaterData.length=fanData.length=0}let s=d.samples||[];for(let i=0;i<s.length;i++){let p=s[i],f=p[5]||0;labels.push(hlabel(p[0],d.now));tempData.push(p[1]);targetData.push(p[2]);cavityData.push(p[3]===null?null:p[3]);bedData.push(p[4]===null?null:p[4]);heaterData.push((f&1)?1:0);fanData.push((f&2)?1:0)}htrim();if(typeof d.lastSeq==='number')historyLastSeq=d.lastSeq>>>0;chartTemp.update();chartIO.update()}function hload(full){if(historyBusy)return;if(!full&&historyLastSeq===null)full=true;historyBusy=true;let url=full?'/history.json':'/history.json?after='+encodeURIComponent(historyLastSeq);fetch(url).then(r=>r.json()).then(d=>happly(d,full)).catch(()=>{}).finally(()=>{historyBusy=false})}";
  html += "function ftext(d,p){let r=d[p+'Reason'],t=d[p+'Target'];if(r===3&&d[p+'RunawayValid']===1)return 'Start: '+d[p+'RunawayStart'].toFixed(1)+'°C · End: '+d[p+'RunawayEnd'].toFixed(1)+'°C · Net rise: '+(d[p+'RunawayRise']>=0?'+':'')+d[p+'RunawayRise'].toFixed(1)+'°C / '+Math.round(d[p+'RunawayDurationMs']/1000)+' s · Min/Max: '+d[p+'RunawayMin'].toFixed(1)+'/'+d[p+'RunawayMax'].toFixed(1)+'°C · Required net: +'+d[p+'RunawayRequiredRise'].toFixed(1)+'°C · Target: '+t.toFixed(1)+'°C';if(r===2)return 'Temperature: '+(d[p+'Temp']===null?'N/A':d[p+'Temp'].toFixed(1)+'°C')+' · Limit: 65.0°C · Target: '+t.toFixed(1)+'°C';if(r===1)return 'Last valid temperature: '+(d[p+'LastValidTemp']===null?'N/A':d[p+'LastValidTemp'].toFixed(1)+'°C')+' · Target: '+t.toFixed(1)+'°C';return 'Target: '+t.toFixed(1)+'°C · Uptime: '+Math.round(d[p+'AtMs']/1000)+' s'}";
  html += "let updBusy=false;function upd(){if(updBusy)return;updBusy=true;fetch('/temp.json').then(r=>r.json()).then(d=>{let mm=document.querySelector('.modeBtn'),kk=document.querySelector('.klipperBtn');if(mm&&kk){mm.classList.toggle('active',!d.klipperMode);kk.classList.toggle('active',d.klipperMode);}let ba=document.getElementById('bedAssistBtn');if(ba){ba.classList.toggle('on',d.bedAssist===1);ba.textContent=d.bedAssist===1?'ON':'OFF';ba.href='/bedassist?state='+(d.bedAssist===1?'off':'on')}let bt=document.querySelector('a.btn');if(bt){bt.classList.toggle('start',!d.master);bt.classList.toggle('stop',d.master);bt.textContent=d.master?' STOP':' START';}document.getElementById('currentTemp').textContent=d.temp.toFixed(1)+'°C';let tc=d.target;let tcColor=tc<45?'#ffd700':tc<55?'#ff8000':'#db4e4e';let tt=document.getElementById('targetTemp');tt.textContent=tc.toFixed(1)+'°C';tt.style.color=tcColor;document.getElementById('quickValue').textContent=d.target.toFixed(1)+'°C';document.getElementById('metricCurrent').textContent=d.temp.toFixed(1)+'°C';document.getElementById('metricTarget').textContent=d.target.toFixed(1)+'°C';let delta=d.target-d.temp;document.getElementById('metricDelta').textContent=(delta>=0?delta.toFixed(1):'−'+Math.abs(delta).toFixed(1))+'°C';let pct=d.target>0?Math.max(0,Math.min(100,d.temp/d.target*100)):0;document.getElementById('progressFill').style.width=pct+'%';document.getElementById('progressPct').textContent=Math.round(pct)+'%';let heat=Math.max(0,Math.min(1,d.temp/65)),gauge=document.getElementById('gauge');gauge.style.setProperty('--deg',(heat*280)+'deg');let hue=d.temp<=40?52-17*(d.temp/40):d.temp<=50?35-15*((d.temp-40)/10):20-20*(Math.min(d.temp,60)-50)/10;gauge.style.setProperty('--gaugeColor','hsl('+hue+',100%,50%)');let hi=document.getElementById('heaterIcon'),hc=document.getElementById('heaterCard'),hs=document.getElementById('heaterState');if(d.heater){hi.classList.add('on');hc.classList.add('on');hs.textContent='ON';hs.style.color='#ff1744'}else{hi.classList.remove('on');hc.classList.remove('on');hs.textContent='OFF';hs.style.color='#00ff9d'}let fi=document.getElementById('fanIcon'),fs=document.getElementById('fanState');if(d.fan){fi.classList.add('on');fs.textContent='ON'}else{fi.classList.remove('on');fs.textContent='OFF'}let q=si(d.state),sb=document.getElementById('statusBar');document.getElementById('statusMain').textContent=q.m;document.getElementById('statusSub').textContent=q.x;document.getElementById('statusDot').style.background=q.c;sb.style.borderColor=q.c+'88';sb.style.background=q.c+'12';let cavityOk=d.printerCavityValid===1;let cavityValue=cavityOk?d.printerCavity:null;document.getElementById('metricCavity').textContent=cavityOk?cavityValue.toFixed(1)+'°C':'--';let bedOk=d.printerBedValid===1;document.getElementById('metricBed').textContent=bedOk?d.printerBed.toFixed(1)+'°C':'--';document.getElementById('bedHeat').textContent=bedOk?(d.printerBedHeating===1?'HEATING':'IDLE'):'--';let fn=document.getElementById('faultNotice');if(fn){let fr=['NONE','NTC','OVERTEMP','RUNAWAY','SAFETY'];fn.classList.toggle('show',d.fault===1);document.getElementById('faultReason').textContent=fr[d.faultReason]||'SAFETY';}hload(false)}).catch(()=>{}).finally(()=>{updBusy=false})}function clk(){document.getElementById('clock').textContent=new Date().toLocaleTimeString()}clk();setInterval(clk,1000);hload(true);upd();setInterval(upd,5000);";
  html += "function fshow(d){let fr=['NONE','NTC','OVERTEMP','RUNAWAY','SAFETY'],fn=document.getElementById('faultNotice'),ln=document.getElementById('lastFaultNotice');if(fn){fn.classList.toggle('show',d.fault===1);document.getElementById('faultReason').textContent=fr[d.faultReason]||'SAFETY';document.getElementById('faultInfo').textContent=ftext(d,'faultDetail')}if(ln){ln.classList.toggle('show',d.lastFaultValid===1);document.getElementById('lastFaultReason').textContent=fr[d.lastFaultReason]||'NONE';document.getElementById('lastFaultInfo').textContent=ftext(d,'lastFault')}}let nativeFetch=window.fetch;window.fetch=function(){let r=nativeFetch.apply(this,arguments),u=arguments[0];if(u==='/temp.json')r.then(x=>x.clone().json()).then(fshow).catch(()=>{});return r};";
  html += "let preheatFetch=window.fetch;window.fetch=function(){let r=preheatFetch.apply(this,arguments),u=arguments[0];if(u==='/temp.json')r.then(x=>x.clone().json()).then(d=>setTimeout(()=>{let mm=document.querySelector('.modeBtn'),pp=document.querySelector('.klipperBtn');if(mm&&pp){mm.classList.toggle('active',!d.preheatMode);pp.classList.toggle('active',d.preheatMode)}let ps=document.getElementById('preheatState');if(ps){ps.classList.toggle('ready',d.preheatReady===1);ps.textContent='PREHEAT: '+(d.preheatReady===1?'READY':'WARMING')}let ul=document.getElementById('u1LinkState'),nt=document.getElementById('ntcState'),tc=document.getElementById('topCoverState');if(ul){ul.textContent=d.u1Online===1?'ONLINE':'OFFLINE';ul.className='hv '+(d.u1Online===1?'ok':'bad')}if(nt){nt.textContent=d.ntcOk===1?'OK':'FAULT';nt.className='hv '+(d.ntcOk===1?'ok':'bad')}if(tc){let known=d.topCoverValid===1,connected=d.topCoverDetected===1;tc.textContent=!known?'UNKNOWN':connected?'CONNECTED':'NOT DETECTED';tc.className='hv '+(!known?'warn':connected?'ok':'muted')}},0)).catch(()=>{});return r};";
  html += "function selectMode(a){let pre=a.href.indexOf('m=preheat')>=0,mm=document.querySelector('.modeBtn'),pp=document.querySelector('.klipperBtn');if(mm&&pp){mm.classList.toggle('active',!pre);pp.classList.toggle('active',pre)}fetch(a.href+(a.href.indexOf('?')>=0?'&':'?')+'ajax=1').catch(()=>{});return false}";
  html += "chartTemp.data.datasets.push({label:'U1 Cavity',data:cavityData,borderColor:'#b783ff',backgroundColor:'rgba(183,131,255,.05)',borderWidth:2,tension:.25,pointRadius:0,fill:false},{label:'Bed',data:bedData,borderColor:'#ff775d',backgroundColor:'rgba(255,119,93,.04)',borderWidth:2,tension:.25,pointRadius:0,fill:false});document.querySelector('.metrics').insertAdjacentHTML('beforeend',\"<div class='metric cavity'><div class='ml'>U1 Cavity</div><div class='mv' id='metricCavity'>--</div></div><div class='metric bed'><div class='ml'>Bed</div><div class='mv' id='metricBed'>--</div><span class='bedheat' id='bedHeat'>--</span></div>\");";
  html += "document.querySelector('.scale span:last-child').textContent='65°C';document.querySelector(\"input[name=max]\").max='65';";
  html += "</script></body></html>";

  server.send(200,"text/html",html);
  diagRootLastUs = micros() - diagStartUs;
  diagnosticMax(diagRootLastUs, diagRootMaxUs);
  diagnosticHeap();
}
// ---------------- HANDLERY ----------------
void handleMode() {
  if (server.hasArg("m")) {
    String mode = server.arg("m");

    if (mode == "manual") {
      if (preheatMode) stopBedAssistManual();
      preheatMode = false;
      preheatReady = false;
    }

    if (mode == "preheat" && printerState != "printing") {
      preheatMode = true;
      preheatReady = false;
      if (!bedAssistActive && !bedAssistPending) {
        bedAssistGeneration++;
        HeaterTelemetrySnapshot thermal = {};
        getHeaterTelemetrySnapshot(thermal);
        const bool circulationNeeded = u1TopCoverDetectionValid && u1TopCoverDetected && thermal.masterOn &&
                                       thermal.chamberState == HEATING_ACTIVE;
        uint32_t generation = 0;
        bedAssistPending = requestU1DesiredState(circulationNeeded ? 0.60f : 0.0f, 0.3f, U1_BED_TARGET_100,
                                                 U1_DESIRED_BED_ASSIST_START, generation);
        bedAssistRenewPending = false;
        if (bedAssistPending) {
          u1CirculationFanOwned = true;
          u1CavityFanOwned = true;
          u1FansReleasePending = false;
          bedAssistCirculationRequested = circulationNeeded;
          Serial.printf("PREHEAT: BED ASSIST START epoch=%lu\n", (unsigned long)generation);
        }
      }
    }
  }

  if (server.hasArg("ajax")) {
    server.send(204);
  } else {
    server.sendHeader("Location", "/");
    server.send(303);
  }
}

void publishVirtualU1Snapshot() {
  if (moonrakerQueue == nullptr || !virtualU1IsEnabled()) return;
  MoonrakerSnapshot snapshot = {};
  bool topCover = false;
  getVirtualU1State(snapshot.printerStateCode, topCover);
  snapshot.httpOk = true;
  snapshot.httpCode = HTTP_CODE_OK;
  snapshot.topCoverDetectedValid = true;
  snapshot.topCoverDetected = topCover;
  snapshot.heap = ESP.getFreeHeap();
  snapshot.minHeap = ESP.getMinFreeHeap();
  xQueueOverwrite(moonrakerQueue, &snapshot);
}

void handleVirtualU1() {
  String mode = server.arg("mode");
  if (mode == "off") {
    setVirtualU1State(false, MOON_PRINT_STANDBY, true);
    Serial.println("VIRTUAL U1: DISABLED; REAL U1 POLLING RESUMES");
  } else if (mode == "on") {
    MoonPrinterStateCode state = MOON_PRINT_STANDBY;
    String requestedState = server.arg("state");
    if (requestedState == "printing") state = MOON_PRINT_PRINTING;
    else if (requestedState == "complete") state = MOON_PRINT_COMPLETE;
    else if (requestedState == "cancelled") state = MOON_PRINT_CANCELLED;
    bool topCover = server.arg("topcover") != "0";
    setVirtualU1State(true, state, topCover);
    publishVirtualU1Snapshot();
    Serial.printf("VIRTUAL U1: %s, TopCover=%s\n", requestedState.c_str(), topCover ? "ON" : "OFF");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleFaultReset() {
  if (heaterSafetyLock()) {
    if (safetyFaultLatched) faultResetRequested = true;
    heaterSafetyUnlock();
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleToggle() {
  bool startBlocked = false;
  bool manualStop = false;
  if (!heaterSafetyLock()) {
    startBlocked = true;
  } else {
    if (!heaterSafetySystemReady || safetyFaultLatched || otaUpdateRequested) {
      masterOn = false;
      startBlocked = true;
    } else if (masterOn) {
      // Ręczny STOP ma najwyższy priorytet.
      masterOn = false;
      autoStopLock = true;
      manualStop = true;
    } else {
      // Ręczny START świadomie odblokowuje AUTO.
      autoStopLock = false;
      masterOn = true;
    }
    heaterSafetyUnlock();
  }
  if (startBlocked) Serial.println("SAFETY START BLOCKED: safety system or fault latch active");
  // BED ASSIST has its own explicit OFF semantics.  A normal Chamber STOP
  // releases only ordinary MANUAL preheat fan ownership.
  if (manualStop && preheatMode) {
    stopBedAssistManual();
    preheatMode = false;
    preheatReady = false;
  } else if (manualStop && !bedAssistActive && !bedAssistPending) {
    releaseU1FansIfOwned("MANUAL STOP");
  }

  server.sendHeader("Location", "/");
  server.send(303);
}
 
void handleSet() {

  if (server.hasArg("st")) {
    float requestedSetTemp = server.arg("st").toFloat();
    float savedSetTemp = 0.0;
    bool saveSetTemp = false;
    if (heaterSafetyLock()) {
      setTemp = requestedSetTemp;
      if (setTemp < 0) setTemp = 0;
      float allowedSetTemp = min(maxTempLimit, MAX_SET_TEMP);
      if (setTemp > allowedSetTemp) setTemp = allowedSetTemp;
      savedSetTemp = setTemp;
      saveSetTemp = true;
      heaterSafetyUnlock();
    }
    if (saveSetTemp) preferences.putFloat("setTemp", savedSetTemp);
  }

  server.sendHeader("Location", "/");
  server.send(303);
}
void handleAdjust() {

  if (server.hasArg("d")) {
    int delta = server.arg("d").toInt();
    float savedSetTemp = 0.0;
    bool saveSetTemp = false;
    if (heaterSafetyLock()) {
      setTemp += delta;
      if (setTemp < 0) setTemp = 0;
      float allowedSetTemp = min(maxTempLimit, MAX_SET_TEMP);
      if (setTemp > allowedSetTemp) setTemp = allowedSetTemp;
      savedSetTemp = setTemp;
      saveSetTemp = true;
      heaterSafetyUnlock();
    }
    if (saveSetTemp) preferences.putFloat("setTemp", savedSetTemp);
  }

  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  unsigned long bootTime = millis();
  Serial.println();
  Serial.println("========== ESP32 CHAMBER BOOT ==========");
  Serial.print("BOOT start: ");
  Serial.print(bootTime);
  Serial.println(" ms");

  preferences.begin("chamber", false);
  wifiSsid = preferences.getString("wifiSsid", "");
  wifiPassword = preferences.getString("wifiPass", "");
  printerIp = preferences.getString("printerIp", "");
  otaPassword = preferences.getString("otaPass", "");
  userNetworkConfigured = preferences.getBool("cfgReady", false) &&
                          wifiSsid.length() > 0 && validPrinterIp(printerIp);
  rebuildMoonrakerUrls();
  if (!userNetworkConfigured) {
    deviceConfigMessage = "Initial setup required: enter Wi-Fi and a valid U1 IP address.";
  }
  if (preferences.getBytesLength("lastFault") == sizeof(PersistedFaultRecord)) {
    PersistedFaultRecord persistedFault = {};
    if (preferences.getBytes("lastFault", &persistedFault, sizeof(PersistedFaultRecord)) == sizeof(PersistedFaultRecord) &&
        persistedFaultRecordIsValid(persistedFault)) {
      lastFaultDetails = makeRuntimeFaultDetails(persistedFault);
    }
  }
  setTemp = preferences.getFloat("setTemp", 30.0);
  hyst = preferences.getFloat("hyst", 0.8);
  preFanTime = preferences.getUInt("preFan", 20000);
  postFanTime = preferences.getUInt("postFan", 30000);
  maxTempLimit = preferences.getFloat("maxTemp", 65.0);
  if (maxTempLimit < 1.0 || maxTempLimit > SAFETY_TEMP_LIMIT) {
    maxTempLimit = SAFETY_TEMP_LIMIT;
    preferences.putFloat("maxTemp", maxTempLimit);
  }
  if (setTemp < 0) setTemp = 0;
  float allowedSetTemp = min(maxTempLimit, MAX_SET_TEMP);
  if (setTemp > allowedSetTemp) {
    setTemp = allowedSetTemp;
    preferences.putFloat("setTemp", setTemp);
  }

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(NTC_PIN, INPUT);
  digitalWrite(RELAY_PIN, LOW);
  digitalWrite(FAN_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(NTC_PIN, ADC_11db);

  if (userNetworkConfigured) startStationWiFi();
  else startConfigPortal();

  server.on("/", handleRoot);
  server.on("/toggle", handleToggle);
  server.on("/mode", handleMode);
  server.on("/set", handleSet);
  server.on("/adjust", handleAdjust);
  server.on("/virtualu1", handleVirtualU1);
  server.on("/faultreset", handleFaultReset);

  server.on("/temp.json", handleTempJSON);
  server.on("/history.json", handleHistoryJSON);
  server.on("/cfg", handleConfig);
  server.on("/devicecfg", HTTP_POST, handleDeviceConfig);
  server.on("/otapassword", HTTP_POST, handleOtaPassword);
  server.on("/update", HTTP_GET, handleOtaPage);
  server.on("/update", HTTP_POST, handleOtaUploadComplete, handleOtaUpload);
  server.on("/restart", handleControllerRestart);
  server.on("/factoryreset", handleFactoryReset);
  server.begin();

  heaterSafetyMutex = xSemaphoreCreateMutex();
  if (heaterSafetyMutex != nullptr) {
    heaterSafetyTaskReady = xTaskCreatePinnedToCore(heaterSafetyTask, "heaterSafety", 8192, nullptr, 2, &heaterSafetyTaskHandle, 1) == pdPASS;
    if (!heaterSafetyTaskReady) {
      Serial.println("CRITICAL SAFETY: heater task creation failed; heating locked OFF");
      masterOn = false;
      digitalWrite(RELAY_PIN, LOW);
      digitalWrite(FAN_PIN, HIGH);
    }
  } else {
    Serial.println("CRITICAL SAFETY: heater mutex creation failed; heating locked OFF");
    masterOn = false;
    digitalWrite(RELAY_PIN, LOW);
    digitalWrite(FAN_PIN, HIGH);
  }

  moonrakerQueue = xQueueCreate(1, sizeof(MoonrakerSnapshot));
  moonrakerCommandQueue = xQueueCreate(1, sizeof(MoonrakerCommand));
  moonrakerCommandResultQueue = xQueueCreate(1, sizeof(MoonrakerCommandResult));
  u1CommandMutex = xSemaphoreCreateMutex();
  if (moonrakerQueue != nullptr && moonrakerCommandQueue != nullptr &&
      moonrakerCommandResultQueue != nullptr && u1CommandMutex != nullptr) {
    moonrakerTaskReady = xTaskCreatePinnedToCore(moonrakerTask, "moonraker", 8192, nullptr, 1, nullptr, 0) == pdPASS;
    if (!moonrakerTaskReady) Serial.println("Moonraker task creation failed");
  } else {
    Serial.println("Moonraker queue or U1 command mutex creation failed");
  }
}

void loop() {
  uint32_t diagLoopStartUs = micros();
  uint32_t diagServerStartUs = micros();
  server.handleClient();
  diagServerLastUs = micros() - diagServerStartUs;
  diagnosticMax(diagServerLastUs, diagServerMaxUs);

  static uint32_t lastFaultNvsAttemptGeneration = 0;
  static uint32_t lastFaultNvsNextAttemptMs = 0;
  FaultDetails faultToSave = {};
  uint32_t faultGenerationToSave = 0;
  if (getLastFaultForNvs(faultToSave, faultGenerationToSave)) {
    uint32_t nowMs = millis();
    bool newFaultGeneration = faultGenerationToSave != lastFaultNvsAttemptGeneration;
    bool retryDue = (int32_t)(nowMs - lastFaultNvsNextAttemptMs) >= 0;
    if (newFaultGeneration || retryDue) {
      PersistedFaultRecord persistedFault = makePersistedFaultRecord(faultToSave);
      bool saved = preferences.putBytes("lastFault", &persistedFault, sizeof(PersistedFaultRecord)) == sizeof(PersistedFaultRecord);
      lastFaultNvsAttemptGeneration = faultGenerationToSave;
      if (saved) {
        if (heaterSafetyLock()) {
          // Do not clear a newer event which may have arrived during flash I/O.
          if (lastFaultGeneration == faultGenerationToSave) lastFaultSavePending = false;
          heaterSafetyUnlock();
        }
        lastFaultNvsNextAttemptMs = nowMs;
      } else {
        lastFaultNvsNextAttemptMs = nowMs + LAST_FAULT_NVS_RETRY_MS;
        Serial.println("LAST FAULT NVS SAVE FAILED; RETRY SCHEDULED");
      }
    }
  }
    // -------- Moonraker watchdog - diagnostyka --------
  bool u1LinkOnline =
    moonrakerValid &&
    (millis() - lastMoonrakerSuccess <= moonrakerWatchdogTimeout);

  static bool lastU1LinkOnline = false;

  if (u1LinkOnline != lastU1LinkOnline) {
    Serial.print("U1 LINK: ");
    Serial.println(u1LinkOnline ? "ONLINE" : "OFFLINE");
    lastU1LinkOnline = u1LinkOnline;
  }
  heaterSafetySetU1LinkOnline(u1LinkOnline);
  unsigned long currentMillis = millis();
  recordHistorySample(currentMillis);
  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);
  // A critical local fault is acted on by the Safety Task immediately.  This
  // normal-loop hand-off is only a best-effort U1 fan release and never gates
  // the local relay/FAN safety behavior.
  if (takeU1FansOffRequestedBySafety()) {
    releaseU1FansIfOwned("LOCAL SAFETY FAULT");
  }
    // -------- DIAGNOSTYKA MASTER / FSM --------
  static bool lastMasterOn = thermal.masterOn;
  static HeatingState lastChamberState = thermal.chamberState;

  if (thermal.masterOn != lastMasterOn) {
    Serial.print("### MASTER CHANGE: ");
    Serial.print(lastMasterOn ? "ON" : "OFF");
    Serial.print(" -> ");
    Serial.println(thermal.masterOn ? "ON" : "OFF");
    lastMasterOn = thermal.masterOn;
  }

  if (thermal.chamberState != lastChamberState) {
    Serial.print("### STATE CHANGE: ");
    Serial.print(stateText(lastChamberState));
    Serial.print(" -> ");
    Serial.println(stateText(thermal.chamberState));
    lastChamberState = thermal.chamberState;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (stationConnectInProgress) {
      stationConnectInProgress = false;
      Serial.print("WiFi connected: http://");
      Serial.println(WiFi.localIP());
    }
    stopConfigPortal();
  } else if (stationConnectInProgress &&
             (uint32_t)(currentMillis - stationConnectStartedMs) >= WIFI_CONNECT_TIMEOUT_MS) {
    stationConnectInProgress = false;
    Serial.println("WiFi connection timed out; opening configuration AP");
    startConfigPortal();
  }

  // Auto reconnect only when a complete user configuration exists.  The AP
  // remains available after a failed connection so credentials can be fixed.
  if (userNetworkConfigured && currentMillis - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED && !stationConnectInProgress) {
      WiFi.disconnect();
      startStationWiFi();
    }
  }

// Stop only after this ESP32 has actually observed a PRINTING -> terminal-state
// transition. Standby before a print must not stop a manual chamber preheat.
if (printEndPending) {
  Serial.print("!!! PRINT END: ");
  Serial.println(printerState);

  if (heaterSafetyLock()) {
    masterOn = false;
    heaterSafetyUnlock();
  }
  if (bedAssistActive || bedAssistPending) stopBedAssist();
  preheatMode = false;
  preheatReady = false;
  printWasActive = false;
  printEndPending = false;
}

if (printerState == "printing" && preheatMode) {
  // U1/slicer has priority: release bed and U1 fans once, but keep the local
  // chamber controller operating with ordinary MANUAL semantics.
  preheatMode = false;
  preheatReady = false;
}

updateBedAssistCirculationFan();

if (bedAssistActive && preheatMode && printerState != "printing" && !bedAssistRenewPending &&
    (uint32_t)(currentMillis - lastBedAssistRenew) >= BED_ASSIST_RENEW_INTERVAL) {
  uint32_t generation = 0;
  bedAssistRenewPending = requestU1DesiredState(bedAssistCirculationRequested ? 0.60f : 0.0f, 0.3f, U1_BED_TARGET_100,
                                                 U1_DESIRED_BED_ASSIST_RENEW, generation);
}

// READY is edge-triggered once per PREHEAT selection.  It does not alter the
// relay/FSM and the buzzer request is consumed by the Safety Task.
if (preheatMode && !preheatReady && thermal.masterOn &&
    thermal.currentTemp >= (thermal.setTemp - (thermal.hyst / 2.0f))) {
  preheatReady = true;
  if (heaterSafetyLock()) {
    if (!safetyFaultLatched) preheatReadyBeepRequested = true;
    heaterSafetyUnlock();
  }
  Serial.println("PREHEAT: READY");
}

MoonrakerCommandResult commandResult;
if (moonrakerCommandResultQueue != nullptr && xQueueReceive(moonrakerCommandResultQueue, &commandResult, 0) == pdPASS) {
  applyMoonrakerCommandResult(commandResult);
}

// Moonraker result is applied here, where synchronous polling previously occurred.
MoonrakerSnapshot moonSnapshot;
if (moonrakerQueue != nullptr && xQueueReceive(moonrakerQueue, &moonSnapshot, 0) == pdPASS) {
  applyMoonrakerSnapshot(moonSnapshot);
}

diagnosticFinishLoop(diagLoopStartUs);

}
