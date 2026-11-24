#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <StreamString.h>
#include <Update.h>
#include <atomic>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <esp_task_wdt.h>
#include <esp_idf_version.h>
#include "chamber_forward_types.h"
#include "chamber_web_ui.h"

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
// Only loop() writes these flags. The Moonraker task reads them as a pair.
portMUX_TYPE u1TopCoverMux = portMUX_INITIALIZER_UNLOCKED;
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
// PREHEAT is a local ESP32 workflow with no custom U1 macro/profile: the
// operator still explicitly starts the chamber.
std::atomic<bool> preheatMode{false};
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

// The chamber target is capped below the fixed software overtemperature threshold.
const float MAX_SET_TEMP = 60.0;
const float SAFETY_TEMP_LIMIT = 65.0;
const unsigned long BUZZER_ON_TIME = 250;
const unsigned long BUZZER_OFF_TIME = 750;

// Configurable thermostat settings.
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
  bool valid;
  int httpCode;
  uint32_t receivedAtMs;
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
  bool printCheckFailed;
};

QueueHandle_t moonrakerCommandQueue = nullptr;
QueueHandle_t moonrakerCommandResultQueue = nullptr;
bool moonrakerTaskReady = false;

// Virtual U1 is kept in RAM and starts disabled after each reboot.
// Virtual U1 supplies simulated Moonraker responses and logs simulated G-code.
// A real request already in flight can still finish after switching sources.
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
const int BED_ASSIST_TEMP_C = 100;
std::atomic<bool> u1FreshPrintingDetected{false};
bool bedAssistActive = false;
bool bedAssistPending = false;
bool bedAssistRenewPending = false;
bool bedAssistCirculationRequested = false;
enum { CIRCULATION_SYNCED, CIRCULATION_PENDING, CIRCULATION_RESYNC } bedAssistCirculationSync = CIRCULATION_SYNCED;
uint32_t lastBedAssistRenew = 0;

// These flags express ESP32 ownership of the U1 fans, never their measured
// physical state.  They are changed only by loop()/WebServer handlers, not by
// the Heater Safety Task.  A release can send FAN=0 only while U1 is idle.
bool u1CirculationFanOwned = false;
bool u1CavityFanOwned = false;
bool u1FansReleasePending = false;
bool u1ReleaseRequestedBySafety = false;

enum U1BedTargetAction : uint8_t { U1_BED_NO_TOUCH, U1_BED_TARGET_100, U1_BED_TARGET_0 };
enum U1DesiredOperation : uint8_t {
  U1_DESIRED_MANUAL_CIRCULATION_ON,
  U1_DESIRED_BED_ASSIST_START,
  U1_DESIRED_BED_ASSIST_RENEW,
  U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE,
  U1_DESIRED_BED_ASSIST_MANUAL_OFF,
  U1_DESIRED_RELEASE_CIRCULATION,
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
  bool simulated;
};

SemaphoreHandle_t u1CommandMutex = nullptr;
U1DesiredState u1DesiredState = { 0, false, 0, 0.0f, 0.0f, U1_BED_NO_TOUCH, U1_DESIRED_RELEASE_FANS, false };
// Protected by u1CommandMutex. A pending start alone does not own the bed.
bool u1BedAssistTargetSet = false;
bool u1BedAssistTargetSimulated = false;

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
bool heaterSafetyU1TelemetryValid = false;
uint32_t heaterSafetyLastU1SuccessMs = 0;
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


String stateText(int s) {
  switch (s) {
    case 0: return "IDLE (heating OFF)";
    case 1: return "PRE-FAN";
    case 2: return "HEATING";
    case 3: return "COOLING";
    default: return "UNKNOWN";
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

// The producer updates these fields directly, independently of loop().
// The caller holds heaterSafetyMutex and samples millis() under the same lock.
bool heaterSafetyU1LinkOnline() {
  if ((uint32_t)(millis() - heaterSafetyLastU1SuccessMs) > moonrakerWatchdogTimeout) {
    heaterSafetyU1TelemetryValid = false;
  }
  return heaterSafetyU1TelemetryValid;
}

bool bedAssistAllowed() {
  if (!heaterSafetyLock()) return false;
  const bool allowed = !safetyFaultLatched && !u1ReleaseRequestedBySafety;
  heaterSafetyUnlock();
  return allowed;
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
  // loop() cancels BED ASSIST and requests the release of any owned U1 fans.
  u1ReleaseRequestedBySafety = true;
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
      latchSafetyFault(SAFETY_FAULT_SYSTEM, millis(), heaterSafetyU1LinkOnline(), false);
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

    bool u1LinkOnline = heaterSafetyU1LinkOnline();
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
  // Send circulation fan commands only when U1 confirms a TopCover.
  // An absent or unknown cover skips the POST at any requested speed.
  // Treat a skipped POST as successful so other BED ASSIST commands can proceed.
  if (strcmp(fanName, "circulation_fan") == 0) {
    portENTER_CRITICAL(&u1TopCoverMux);
    const bool available = u1TopCoverDetectionValid && u1TopCoverDetected;
    portEXIT_CRITICAL(&u1TopCoverMux);
    if (!available) {
      Serial.println("U1 FANS: circulation command skipped (TopCover absent/unknown)");
      return HTTP_CODE_OK;
    }
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
enum FreshJsonContext : uint8_t {
  FRESH_JSON_ROOT, FRESH_JSON_RESULT, FRESH_JSON_STATUS, FRESH_JSON_PRINT_STATS,
  FRESH_JSON_CAVITY, FRESH_JSON_BED, FRESH_JSON_PURIFIER, FRESH_JSON_OTHER
};

// ArduinoJson is not otherwise a dependency of this firmware.  This compact
// local parser validates the JSON and uses exact paths under result.status
// for periodic telemetry and fresh checks before writes.
struct FreshJsonParser {
  const char *data;
  size_t length;
  size_t position;
  uint8_t depth;
  uint8_t objectsSeen;
  bool stateSeen;
  String state;
  MoonrakerSnapshot *telemetry;
};

void freshJsonSkipWhitespace(FreshJsonParser &parser);
bool freshJsonParseString(FreshJsonParser &parser, String *output);
bool freshJsonParseValue(FreshJsonParser &parser, FreshJsonContext context);
bool freshJsonParseArray(FreshJsonParser &parser);
bool freshJsonParseObject(FreshJsonParser &parser, FreshJsonContext context);
bool freshJsonParseLiteral(FreshJsonParser &parser, const char *literal);
bool freshJsonParseNumber(FreshJsonParser &parser);
bool freshJsonParseField(FreshJsonParser &parser, FreshJsonContext context, const String &key);
FreshPrintState queryFreshMoonrakerPrintState(MoonrakerSnapshot *bedCheck = nullptr);

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
    const uint8_t lead = (uint8_t)character;
    if (lead >= 0x80) {
      if (lead < 0xc2 || lead > 0xf4) return false;
      const uint8_t extraBytes = lead < 0xe0 ? 1 : lead < 0xf0 ? 2 : 3;
      if (parser.length - parser.position < extraBytes) return false;
      const uint8_t first = (uint8_t)parser.data[parser.position];
      // Reject overlong encodings, UTF-16 surrogate code points and > U+10FFFF.
      if ((lead == 0xe0 && first < 0xa0) || (lead == 0xed && first >= 0xa0) ||
          (lead == 0xf0 && first < 0x90) || (lead == 0xf4 && first >= 0x90)) return false;
      for (uint8_t index = 0; index < extraBytes; index++) {
        if (((uint8_t)parser.data[parser.position++] & 0xc0) != 0x80) return false;
      }
      character = '\x01';  // Non-ASCII cannot match a recognized key or state.
    } else if (character == '\\') {
      if (parser.position >= parser.length) return false;
      char escaped = parser.data[parser.position++];
      if (escaped == 'u') {
        uint16_t codePoint = 0;
        for (uint8_t index = 0; index < 4; index++) {
          if (parser.position >= parser.length) return false;
          char hex = parser.data[parser.position++];
          codePoint <<= 4;
          if (hex >= '0' && hex <= '9') codePoint |= hex - '0';
          else if (hex >= 'a' && hex <= 'f') codePoint |= hex - 'a' + 10;
          else if (hex >= 'A' && hex <= 'F') codePoint |= hex - 'A' + 10;
          else return false;
        }
        // Only printable ASCII can match an object name, field or known state.
        character = codePoint >= 0x20 && codePoint < 0x7f ? (char)codePoint : '\x01';
      } else if (!(escaped == '\"' || escaped == '\\' || escaped == '/' || escaped == 'b' || escaped == 'f' || escaped == 'n' || escaped == 'r' || escaped == 't')) {
        return false;
      } else {
        character = escaped == '\"' || escaped == '\\' || escaped == '/' ? escaped : '\x01';
      }
    }
    if (output != nullptr && !output->concat(character)) return false;
  }
  return false;
}

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
  if (context != FRESH_JSON_OTHER) {
    const uint8_t bit = 1U << context;
    if (parser.objectsSeen & bit) return false;
    parser.objectsSeen |= bit;
  }
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

    if (!freshJsonParseField(parser, context, key)) return false;

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

bool freshJsonReadFloat(FreshJsonParser &parser, float minValue, float maxValue, bool &valid, float &value) {
  const char *start = parser.data + parser.position;
  if (valid || !freshJsonParseNumber(parser)) return false;
  char *end;
  const float parsed = strtof(start, &end);
  if (end != parser.data + parser.position || !isfinite(parsed) || parsed < minValue || parsed > maxValue) return false;
  value = parsed;
  valid = true;
  return true;
}

bool freshJsonParseField(FreshJsonParser &parser, FreshJsonContext context, const String &key) {
  if (context == FRESH_JSON_ROOT && key == "error") return false;
  if (context == FRESH_JSON_ROOT && key == "result") return freshJsonParseObject(parser, FRESH_JSON_RESULT);
  if (context == FRESH_JSON_RESULT && key == "status") return freshJsonParseObject(parser, FRESH_JSON_STATUS);
  if (context == FRESH_JSON_STATUS && key == "print_stats") return freshJsonParseObject(parser, FRESH_JSON_PRINT_STATS);
  if (context == FRESH_JSON_PRINT_STATS && key == "state") {
    if (parser.stateSeen || !freshJsonParseString(parser, &parser.state)) return false;
    parser.stateSeen = true;
    return true;
  }

  if (parser.telemetry != nullptr) {
    MoonrakerSnapshot &sample = *parser.telemetry;
    if (context == FRESH_JSON_STATUS) {
      if (key == "temperature_sensor cavity") return freshJsonParseObject(parser, FRESH_JSON_CAVITY);
      if (key == "heater_bed") return freshJsonParseObject(parser, FRESH_JSON_BED);
      if (key == "purifier") return freshJsonParseObject(parser, FRESH_JSON_PURIFIER);
    } else if (context == FRESH_JSON_CAVITY && key == "temperature") {
      return freshJsonReadFloat(parser, -20.0f, 120.0f, sample.cavityValueValid, sample.cavityTemp);
    } else if (context == FRESH_JSON_BED) {
      if (key == "temperature") return freshJsonReadFloat(parser, -20.0f, 150.0f, sample.bedTemperatureValid, sample.bedTemperature);
      if (key == "target") return freshJsonReadFloat(parser, 0.0f, 150.0f, sample.bedTargetValid, sample.bedTarget);
      if (key == "power") return freshJsonReadFloat(parser, 0.0f, 1.0f, sample.bedPowerValid, sample.bedPower);
    } else if (context == FRESH_JSON_PURIFIER && key == "power_detected") {
      if (sample.topCoverDetectedValid || parser.position >= parser.length) return false;
      sample.topCoverDetected = parser.data[parser.position] == 't';
      sample.topCoverDetectedValid = freshJsonParseLiteral(parser, sample.topCoverDetected ? "true" : "false");
      return sample.topCoverDetectedValid;
    }
  }
  return freshJsonParseValue(parser, FRESH_JSON_OTHER);
}

MoonPrinterStateCode parseMoonrakerStatus(const String &response, MoonrakerSnapshot *telemetry,
                                        bool bedTargetOnly = false) {
  FreshJsonParser parser = {};
  parser.data = response.c_str();
  parser.length = response.length();
  parser.telemetry = telemetry;
  freshJsonSkipWhitespace(parser);
  if (!freshJsonParseObject(parser, FRESH_JSON_ROOT)) return MOON_PRINT_UNCHANGED;
  freshJsonSkipWhitespace(parser);
  if (parser.position != parser.length || !parser.stateSeen || parser.state.length() == 0) return MOON_PRINT_UNCHANGED;
  // Moonraker may omit the purifier on a U1 without a TopCover.
  if (telemetry != nullptr && !bedTargetOnly &&
      (!telemetry->cavityValueValid || !telemetry->bedTemperatureValid ||
       !telemetry->bedTargetValid || !telemetry->bedPowerValid)) return MOON_PRINT_UNCHANGED;
  return parsePrinterState(parser.state);
}

FreshPrintState classifyFreshPrintState(MoonPrinterStateCode state) {
  if (state == MOON_PRINT_PRINTING) return FRESH_PRINT_RUNNING;
  if (state == MOON_PRINT_STANDBY || state == MOON_PRINT_COMPLETE || state == MOON_PRINT_CANCELLED) return FRESH_PRINT_NOT_RUNNING;
  return FRESH_PRINT_UNKNOWN;
}

// Called only from moonrakerTask's command path, before an external write.
FreshPrintState queryFreshMoonrakerPrintState(MoonrakerSnapshot *bedCheck) {
  if (virtualU1IsEnabled()) {
    MoonPrinterStateCode state;
    bool topCover;
    getVirtualU1State(state, topCover);
    if (bedCheck != nullptr) {
      bedCheck->bedTargetValid = true;
      bedCheck->bedTarget = BED_ASSIST_TEMP_C;
    }
    return classifyFreshPrintState(state);
  }
  if (WiFi.status() != WL_CONNECTED || moonrakerPrintStateUrl.length() == 0) return FRESH_PRINT_UNKNOWN;

  HTTPClient http;
  http.setConnectTimeout(500);
  http.setTimeout(600);
  String url = moonrakerPrintStateUrl;
  if (bedCheck != nullptr) url += "&heater_bed=target";
  if (!http.begin(url)) return FRESH_PRINT_UNKNOWN;

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    http.end();
    return FRESH_PRINT_UNKNOWN;
  }

  StreamString response;
  int bytesRead = http.writeToStream(&response);
  http.end();
  if (bytesRead <= 0 || (size_t)bytesRead != response.length()) return FRESH_PRINT_UNKNOWN;
  return classifyFreshPrintState(parseMoonrakerStatus(response, bedCheck, true));
}

} // namespace FreshJson

bool enqueueMoonrakerCommand(MoonrakerCommandType type, uint32_t generation);

bool u1CommandLock() {
  return u1CommandMutex != nullptr && xSemaphoreTake(u1CommandMutex, portMAX_DELAY) == pdTRUE;
}

void u1CommandUnlock() {
  if (u1CommandMutex != nullptr) xSemaphoreGive(u1CommandMutex);
}

bool getPendingU1DesiredState(U1DesiredState &state) {
  if (!u1CommandLock()) return false;
  bool due = u1DesiredState.syncPending && (int32_t)(millis() - u1DesiredState.nextAttemptMs) >= 0;
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

bool u1CommandCanContinue(const U1DesiredState &desired) {
  if (u1FreshPrintingDetected.load() || desired.simulated != virtualU1IsEnabled()) return false;
  if (desired.operation != U1_DESIRED_RELEASE_CIRCULATION &&
      desired.operation != U1_DESIRED_RELEASE_FANS && !bedAssistAllowed()) return false;
  if (desired.operation == U1_DESIRED_MANUAL_CIRCULATION_ON) {
    HeaterTelemetrySnapshot thermal = {};
    if (preheatMode || !getHeaterTelemetrySnapshot(thermal) ||
        !thermal.masterOn || thermal.chamberState != HEATING_ACTIVE) return false;
  }
  return u1CommandGenerationCurrent(desired.generation);
}

bool authorizeU1Write(const U1DesiredState &desired, MoonrakerCommandResult &result,
                      MoonrakerSnapshot *bedCheck) {
  if (!u1CommandCanContinue(desired)) {
    result.stale = true;
    return false;
  }
  const auto state = FreshJson::queryFreshMoonrakerPrintState(bedCheck);
  if (virtualU1IsEnabled() != desired.simulated) {
    result.stale = true;
    return false;
  }
  if (state == FreshJson::FRESH_PRINT_RUNNING) {
    u1FreshPrintingDetected.store(true);
    result.stale = true;
    return false;
  }
  if (!u1CommandCanContinue(desired)) {
    result.stale = true;
    return false;
  }
  result.printCheckFailed = state != FreshJson::FRESH_PRINT_NOT_RUNNING ||
                            (bedCheck != nullptr && !bedCheck->bedTargetValid);
  return !result.printCheckFailed;
}

bool hasU1BedAssistTarget() {
  const bool simulated = virtualU1IsEnabled();
  if (!u1CommandLock()) return false;
  const bool targetSet = u1BedAssistTargetSet && u1BedAssistTargetSimulated == simulated;
  u1CommandUnlock();
  return targetSet;
}

int sendU1BedTarget(const U1DesiredState &desired, int target) {
  const bool simulated = virtualU1IsEnabled();
  char script[80];
  snprintf(script, sizeof(script), "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=%d", target);
  const int code = sendMoonrakerGcode(script);
  if (code == HTTP_CODE_OK && virtualU1IsEnabled() == simulated && u1CommandLock()) {
    // A STOP can arrive while TARGET=100 is in flight. It may still turn off
    // that target, but a canceled generation from PRINTING cannot own it.
    if (u1DesiredState.generation == desired.generation ||
        (target != 0 && u1DesiredState.syncPending &&
         u1DesiredState.operation == U1_DESIRED_BED_ASSIST_MANUAL_OFF)) {
      u1BedAssistTargetSet = target == BED_ASSIST_TEMP_C;
      u1BedAssistTargetSimulated = simulated;
    }
    u1CommandUnlock();
  }
  return code;
}

void finishU1DesiredSync(uint32_t generation, bool success) {
  if (!u1CommandLock()) return;
  if (u1DesiredState.generation == generation) {
    if (success) {
      u1DesiredState.syncPending = false;
      if (u1DesiredState.operation == U1_DESIRED_BED_ASSIST_MANUAL_OFF) u1BedAssistTargetSet = false;
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
  u1DesiredState.nextAttemptMs = millis();
  u1DesiredState.circulationSpeed = circulationSpeed;
  u1DesiredState.cavitySpeed = cavitySpeed;
  u1DesiredState.bedAction = bedAction;
  u1DesiredState.operation = operation;
  u1DesiredState.simulated = virtualU1IsEnabled();
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
  if (!getPendingU1DesiredState(desired)) return;
  if (command.generation != desired.generation || !u1CommandGenerationCurrent(desired.generation)) return;

  MoonrakerCommandResult result = { MOON_COMMAND_SYNC_U1_DESIRED, desired.generation,
    false, -1, -1, -1, (uint8_t)desired.operation, false, false };
  bool allOk = false;
  if (desired.operation == U1_DESIRED_BED_ASSIST_RENEW) {
    if (authorizeU1Write(desired, result, nullptr)) {
      result.primaryHttpCode = sendU1BedTarget(desired, BED_ASSIST_TEMP_C);
      allOk = result.primaryHttpCode == HTTP_CODE_OK;
    }
  } else if (authorizeU1Write(desired, result, nullptr)) {
    result.primaryHttpCode = sendMoonrakerFanSpeed("circulation_fan", desired.circulationSpeed);
    allOk = result.primaryHttpCode == HTTP_CODE_OK;

    const bool startsAssist = desired.operation == U1_DESIRED_BED_ASSIST_START;
    const bool controlsCavity = startsAssist || desired.operation == U1_DESIRED_BED_ASSIST_MANUAL_OFF ||
                                desired.operation == U1_DESIRED_RELEASE_FANS;
    if (controlsCavity && (!startsAssist || allOk) && authorizeU1Write(desired, result, nullptr)) {
      result.secondaryHttpCode = desired.cavitySpeed > 0.0f ?
        sendMoonrakerGcode("SET_FAN_SPEED FAN=cavity_fan SPEED=0.3") :
        sendMoonrakerGcode("SET_FAN_SPEED FAN=cavity_fan SPEED=0");
      allOk = allOk && result.secondaryHttpCode == HTTP_CODE_OK;

      if (desired.bedAction == U1_BED_TARGET_100 && allOk && authorizeU1Write(desired, result, nullptr)) {
        result.tertiaryHttpCode = sendU1BedTarget(desired, BED_ASSIST_TEMP_C);
        allOk = result.tertiaryHttpCode == HTTP_CODE_OK;
      } else if (desired.bedAction == U1_BED_TARGET_0) {
        MoonrakerSnapshot bedCheck = {};
        if (!hasU1BedAssistTarget()) {
          Serial.println("BED ASSIST: STOP WITHOUT A CONFIRMED BED TARGET");
        } else if (authorizeU1Write(desired, result, &bedCheck)) {
          if (bedCheck.bedTarget == BED_ASSIST_TEMP_C) {
            result.tertiaryHttpCode = sendU1BedTarget(desired, 0);
            allOk = allOk && result.tertiaryHttpCode == HTTP_CODE_OK;
          } else {
            Serial.println("BED ASSIST: BED TARGET ALREADY CHANGED; LEAVE UNTOUCHED");
          }
        }
      }
    }
  }

  if (!result.stale && !u1CommandCanContinue(desired)) result.stale = true;
  result.success = !result.stale && !result.printCheckFailed && allOk;
  finishU1DesiredSync(desired.generation, result.stale || result.success);
  if (moonrakerCommandResultQueue != nullptr) xQueueOverwrite(moonrakerCommandResultQueue, &result);
}

void publishMoonrakerSnapshot(MoonrakerSnapshot &snapshot, bool simulated) {
  const bool locked = heaterSafetyLock();
  // A real poll that finishes after entering simulation must not replace it.
  if (simulated == virtualU1IsEnabled()) {
    snapshot.receivedAtMs = millis();
    if (locked) {
      heaterSafetyU1TelemetryValid = snapshot.valid;
      if (snapshot.valid) heaterSafetyLastU1SuccessMs = snapshot.receivedAtMs;
    }
    xQueueOverwrite(moonrakerQueue, &snapshot);
  }
  if (locked) heaterSafetyUnlock();
}

// This task owns the blocking HTTP request and publishes status without touching GPIO.
void moonrakerTask(void *parameter) {
  uint32_t lastPollMs = millis();

  for (;;) {
    // The persistent desired state is authoritative.  This makes a lost or
    // overwritten wake-up harmless and gives the newest epoch priority.
    U1DesiredState pendingDesired = {};
    if (getPendingU1DesiredState(pendingDesired)) {
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

    const bool simulated = virtualU1IsEnabled();
    if (simulated) {
      bool topCover = false;
      getVirtualU1State(snapshot.printerStateCode, topCover);
      snapshot.valid = true;
      snapshot.httpCode = HTTP_CODE_OK;
      snapshot.topCoverDetectedValid = true;
      snapshot.topCoverDetected = topCover;
    } else if (WiFi.status() == WL_CONNECTED && moonrakerCavityUrl.length() > 0) {
      HTTPClient http;
      http.setConnectTimeout(500);
      http.setTimeout(600);
      if (http.begin(moonrakerCavityUrl)) {
        uint32_t getStartUs = micros();
        snapshot.httpCode = http.GET();
        snapshot.getUs = micros() - getStartUs;
        if (snapshot.httpCode == HTTP_CODE_OK) {
          StreamString response;
          const int bytesRead = http.writeToStream(&response);
          if (bytesRead > 0 && (size_t)bytesRead == response.length()) {
            snapshot.printerStateCode = FreshJson::parseMoonrakerStatus(response, &snapshot);
            snapshot.valid = snapshot.printerStateCode != MOON_PRINT_UNCHANGED;
          }
        }
        http.end();
      }
    }

    snapshot.totalUs = micros() - totalStartUs;
    snapshot.heap = ESP.getFreeHeap();
    snapshot.minHeap = ESP.getMinFreeHeap();
    publishMoonrakerSnapshot(snapshot, simulated);
  }
}

bool enqueueMoonrakerCommand(MoonrakerCommandType type, uint32_t generation) {
  if (!moonrakerTaskReady || moonrakerCommandQueue == nullptr) return false;
  MoonrakerCommand command = { type, generation };
  return xQueueOverwrite(moonrakerCommandQueue, &command) == pdPASS;
}

void releaseU1FansIfOwned(const char *reason) {
  if ((!u1CirculationFanOwned && !u1CavityFanOwned) || u1FansReleasePending) return;

  uint32_t generation = 0;
  const auto operation = u1CavityFanOwned ? U1_DESIRED_RELEASE_FANS : U1_DESIRED_RELEASE_CIRCULATION;
  const bool queued = requestU1DesiredState(0.0f, 0.0f, U1_BED_NO_TOUCH, operation, generation);
  u1FansReleasePending = queued;
  Serial.printf("U1 FANS: RELEASE (%s) epoch=%lu %s\n", reason,
    (unsigned long)generation, queued ? "QUEUED" : "PENDING");
}

void updateManualCirculationFan(bool u1LinkOnline) {
  if (preheatMode || u1CavityFanOwned || u1FansReleasePending) return;

  HeaterTelemetrySnapshot thermal = {};
  const bool shouldRun = u1LinkOnline && u1TopCoverDetectionValid && u1TopCoverDetected &&
                         (printerState == "standby" || printerState == "complete" || printerState == "cancelled") &&
                         getHeaterTelemetrySnapshot(thermal) && thermal.masterOn &&
                         thermal.chamberState == HEATING_ACTIVE && bedAssistAllowed();
  if (!shouldRun) {
    releaseU1FansIfOwned("MANUAL CIRCULATION OFF");
  } else if (!u1CirculationFanOwned) {
    uint32_t generation = 0;
    u1CirculationFanOwned = requestU1DesiredState(0.60f, 0.0f, U1_BED_NO_TOUCH,
                                                 U1_DESIRED_MANUAL_CIRCULATION_ON, generation);
  }
}

void updateBedAssistCirculationFan() {
  // A speed change must wait for renewal. STOP and PRINTING can cancel it.
  if (bedAssistRenewPending) return;

  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);
  // In PREHEAT, BED ASSIST changes circulation when the thermostat state changes.
  // Use the desired speed to avoid repeated commands during steady operation.
  const bool shouldRun = preheatMode && bedAssistActive &&
                         u1TopCoverDetectionValid && u1TopCoverDetected &&
                         thermal.masterOn && thermal.chamberState == HEATING_ACTIVE &&
                         printerState != "printing";
  if ((bedAssistCirculationSync != CIRCULATION_RESYNC && shouldRun == bedAssistCirculationRequested) ||
      !bedAssistAllowed()) return;

  uint32_t generation = 0;
  if (requestU1DesiredState(shouldRun ? 0.60f : 0.0f, 0.0f, U1_BED_NO_TOUCH,
                            U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE, generation)) {
    bedAssistCirculationRequested = shouldRun;
    bedAssistCirculationSync = CIRCULATION_PENDING;
    Serial.printf("BED ASSIST: CIRCULATION %s epoch=%lu\n", shouldRun ? "60%" : "OFF",
      (unsigned long)generation);
  }
}

void clearBedAssistState() {
  bedAssistActive = false;
  bedAssistPending = false;
  bedAssistRenewPending = false;
  bedAssistCirculationRequested = false;
  bedAssistCirculationSync = CIRCULATION_SYNCED;
}

void stopBedAssist() {
  clearBedAssistState();
  if (u1CommandLock()) {
    u1BedAssistTargetSet = false;
    u1CommandUnlock();
  }
  releaseU1FansIfOwned("BED ASSIST AUTO OFF");
}

void discardU1Control() {
  if (u1CommandLock()) {
    if (++u1DesiredState.generation == 0) ++u1DesiredState.generation;
    u1DesiredState.syncPending = false;
    u1BedAssistTargetSet = false;
    u1CommandUnlock();
  }
  clearBedAssistState();
  u1CirculationFanOwned = false;
  u1CavityFanOwned = false;
  u1FansReleasePending = false;
}

void handoffU1ToPrint() {
  // The print may already have set its targets, so discard even a queued STOP.
  discardU1Control();
  printerState = "printing";
  printWasActive = true;
  preheatMode = false;
  preheatReady = false;
}

void stopBedAssistManual() {
  const bool bedAssistMayBeRunning = bedAssistActive || bedAssistPending;
  clearBedAssistState();
  if (bedAssistMayBeRunning) {
    u1FansReleasePending = true;
    uint32_t generation = 0;
    bool queued = requestU1DesiredState(0.0f, 0.0f, U1_BED_TARGET_0,
                                        U1_DESIRED_BED_ASSIST_MANUAL_OFF, generation);
    Serial.printf("BED ASSIST: MANUAL OFF epoch=%lu %s\n", (unsigned long)generation,
      queued ? "QUEUED" : "PENDING");
  }
}

void handleU1ReleaseRequestedBySafety() {
  if (!heaterSafetyLock()) return;
  const bool requested = u1ReleaseRequestedBySafety;
  const uint32_t faultGeneration = lastFaultGeneration;
  heaterSafetyUnlock();
  if (!requested) return;

  if (bedAssistActive || bedAssistPending || hasU1BedAssistTarget()) {
    Serial.println("BED ASSIST: LOCAL SAFETY FAULT; BED STATE UNCONFIRMED");
  }
  // Replace any queued manual bed STOP with a fan-only release.
  u1FansReleasePending = false;
  stopBedAssist();

  // Keep blocking old commands until cancellation, even after a rapid reset.
  if (heaterSafetyLock()) {
    if (lastFaultGeneration == faultGeneration) u1ReleaseRequestedBySafety = false;
    heaterSafetyUnlock();
  }
}

void applyMoonrakerCommandResult(const MoonrakerCommandResult &result) {
  if (result.type != MOON_COMMAND_SYNC_U1_DESIRED) return;
  if (result.stale || !u1CommandGenerationCurrent(result.generation)) {
    Serial.printf("U1 SYNC: STALE epoch=%lu\n", (unsigned long)result.generation);
    return;
  }

  U1DesiredOperation operation = (U1DesiredOperation)result.desiredOperation;
  if (result.printCheckFailed) {
    // Unconfirmed state leaves the operation pending for the normal retry.
    Serial.println("U1 SYNC: PRINT STATE OR BED TARGET UNCONFIRMED; WRITE BLOCKED");
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
      bedAssistCirculationSync = CIRCULATION_SYNCED;
      Serial.printf("BED ASSIST: START NOT ACTIVE circulation=%d cavity=%d heater=%d\n",
        result.primaryHttpCode, result.secondaryHttpCode, result.tertiaryHttpCode);
      releaseU1FansIfOwned("BED ASSIST START FAILED");
    }
  } else if (operation == U1_DESIRED_BED_ASSIST_RENEW) {
    bedAssistRenewPending = false;
    if (result.success) {
      lastBedAssistRenew = millis();
      // Reconcile a fan request that the renewal may have superseded.
      if (bedAssistCirculationSync == CIRCULATION_PENDING) bedAssistCirculationSync = CIRCULATION_RESYNC;
    } else {
      Serial.printf("BED ASSIST: RENEW FAILED heater=%d\n", result.primaryHttpCode);
      stopBedAssist();
    }
  } else if (operation == U1_DESIRED_BED_ASSIST_CIRCULATION_UPDATE) {
    if (result.success) bedAssistCirculationSync = CIRCULATION_SYNCED;
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
  } else if (operation == U1_DESIRED_RELEASE_FANS || operation == U1_DESIRED_RELEASE_CIRCULATION) {
    if (result.success) {
      u1CirculationFanOwned = false;
      u1CavityFanOwned = false;
      u1FansReleasePending = false;
    }
    Serial.printf("U1 FANS: OFF circulation=%d cavity=%d\n", result.primaryHttpCode, result.secondaryHttpCode);
  } else if (operation == U1_DESIRED_MANUAL_CIRCULATION_ON) {
    Serial.printf("U1 FANS: MANUAL ON circulation=%d\n", result.primaryHttpCode);
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
  if (!snapshot.valid) diagMoonHttpErrors++;

  // Unknown or invalid telemetry disables circulation_fan commands.
  portENTER_CRITICAL(&u1TopCoverMux);
  u1TopCoverDetectionValid = snapshot.valid && snapshot.topCoverDetectedValid;
  u1TopCoverDetected = u1TopCoverDetectionValid && snapshot.topCoverDetected;
  portEXIT_CRITICAL(&u1TopCoverMux);

  if (snapshot.valid) {
    moonrakerValid = true;
    lastMoonrakerSuccess = snapshot.receivedAtMs;

    if (snapshot.printerStateCode != MOON_PRINT_UNCHANGED) {
      if (snapshot.printerStateCode == MOON_PRINT_STANDBY) printerState = "standby";
      else if (snapshot.printerStateCode == MOON_PRINT_PRINTING) printerState = "printing";
      else if (snapshot.printerStateCode == MOON_PRINT_COMPLETE) printerState = "complete";
      else if (snapshot.printerStateCode == MOON_PRINT_CANCELLED) printerState = "cancelled";
      else printerState = "unknown";

      if (snapshot.printerStateCode == MOON_PRINT_PRINTING) {
        handoffU1ToPrint();
      } else if (printWasActive &&
                 (snapshot.printerStateCode == MOON_PRINT_COMPLETE ||
                  snapshot.printerStateCode == MOON_PRINT_CANCELLED)) {
        printEndPending = true;
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

  } else {
    moonrakerValid = false;
    if (snapshot.httpCode == HTTP_CODE_OK) {
      // A failed payload cannot mark any of its measurements current.
      printerCavityValid = false;
      printerBedValid = false;
      printerBedTargetValid = false;
      printerBedPowerValid = false;
    }
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


bool parseSettingFloat(const String &input, float &value) {
  const char *start = input.c_str();
  if (strpbrk(start, "xX") != nullptr) return false;
  char *end;
  errno = 0;
  const float parsed = strtof(start, &end);
  if (end == start || end != start + input.length() || errno == ERANGE || !isfinite(parsed)) return false;
  value = parsed;
  return true;
}

bool parseSettingInt(const String &input, long &value) {
  const char *start = input.c_str();
  char *end;
  errno = 0;
  const long parsed = strtol(start, &end, 10);
  if (end == start || end != start + input.length() || errno == ERANGE) return false;
  value = parsed;
  return true;
}

void sendSettingsSaved() {
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleConfig() {
  String hystInput = server.arg("hyst");
  String preInput = server.arg("pre");
  String postInput = server.arg("post");
  String maxInput = server.arg("max");
  hystInput.trim();
  preInput.trim();
  postInput.trim();
  maxInput.trim();

  const bool hasHyst = hystInput.length() > 0;
  const bool hasPre = preInput.length() > 0;
  const bool hasPost = postInput.length() > 0;
  const bool hasMax = maxInput.length() > 0;
  float requestedHyst = 0.0f, requestedMax = 0.0f;
  long requestedPre = 0, requestedPost = 0;
  if ((!hasHyst && !hasPre && !hasPost && !hasMax) ||
      (hasHyst && (!parseSettingFloat(hystInput, requestedHyst) || requestedHyst < 0.1f || requestedHyst > 10.0f)) ||
      (hasPre && (!parseSettingInt(preInput, requestedPre) || requestedPre < 0 || requestedPre > 600)) ||
      (hasPost && (!parseSettingInt(postInput, requestedPost) || requestedPost < 0 || requestedPost > 600)) ||
      (hasMax && (!parseSettingFloat(maxInput, requestedMax) || requestedMax < 1.0f || requestedMax > SAFETY_TEMP_LIMIT))) {
    server.send(400, "text/plain", "Invalid or missing thermal settings.");
    return;
  }

  if (!heaterSafetyLock()) {
    server.send(503, "text/plain", "Safety lock unavailable. Settings were not saved.");
    return;
  }
  const float previousTarget = setTemp;
  heaterSafetyUnlock();

  // Flash writes run outside the Safety Task mutex. Store a reduced target
  // before its new limit so a failed write cannot leave an excessive target.
  const float newTarget = min(previousTarget, min(requestedMax, MAX_SET_TEMP));
  const bool reduceTarget = hasMax && newTarget < previousTarget;
  const uint32_t requestedPreMs = (uint32_t)requestedPre * 1000UL;
  const uint32_t requestedPostMs = (uint32_t)requestedPost * 1000UL;
  const bool savedHyst = hasHyst && preferences.putFloat("hyst", requestedHyst) == sizeof(float);
  const bool savedPre = hasPre && preferences.putUInt("preFan", requestedPreMs) == sizeof(uint32_t);
  const bool savedPost = hasPost && preferences.putUInt("postFan", requestedPostMs) == sizeof(uint32_t);
  const bool savedTarget = reduceTarget && preferences.putFloat("setTemp", newTarget) == sizeof(float);
  const bool savedMax = hasMax && (!reduceTarget || savedTarget) &&
                        preferences.putFloat("maxTemp", requestedMax) == sizeof(float);

  if (!heaterSafetyLock()) {
    server.send(503, "text/plain", "Safety lock unavailable. Saved settings could not be applied.");
    return;
  }
  if (savedHyst) hyst = requestedHyst;
  if (savedPre) preFanTime = requestedPreMs;
  if (savedPost) postFanTime = requestedPostMs;
  if (savedTarget) setTemp = newTarget;
  if (savedMax) maxTempLimit = requestedMax;
  heaterSafetyUnlock();

  if ((hasHyst && !savedHyst) || (hasPre && !savedPre) ||
      (hasPost && !savedPost) || (hasMax && !savedMax)) {
    server.send(500, "text/plain", "One or more thermal settings could not be saved.");
    return;
  }
  sendSettingsSaved();
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

bool prepareControllerRestart() {
  // The Safety Task owns the relay and fan and must acknowledge RELAY_PIN=LOW.
  if (!requestOtaSafetyLockout()) {
    server.send(503, "text/html", "<html><body>Safety lock unavailable. Restart rejected.</body></html>");
    return false;
  }

  const uint32_t deadline = millis() + 500UL;
  while (!otaSafetyLockoutReady() && (int32_t)(millis() - deadline) < 0) delay(5);
  if (!otaSafetyLockoutReady()) {
    server.send(503, "text/html", "<html><body>Safety lock acknowledgement timed out. Restart rejected.</body></html>");
    return false;
  }
  return true;
}

bool saveWifiPassword(const String &password) {
  if (password.length() > 0) {
    return preferences.putString("wifiPass", password) == password.length();
  }
  // An empty string returns 0 from putString even when the write succeeds.
  preferences.putString("wifiPass", "");
  return preferences.getStringLength("wifiPass") == 1;
}

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

  if (!prepareControllerRestart()) return;

  // Mark the configuration ready only after every key has been written.
  if (preferences.putBool("cfgReady", false) != sizeof(uint8_t) ||
      preferences.putString("wifiSsid", requestedSsid) != requestedSsid.length() ||
      !saveWifiPassword(passwordToStore) ||
      preferences.putString("printerIp", requestedPrinterIp) != requestedPrinterIp.length() ||
      preferences.putBool("cfgReady", true) != sizeof(uint8_t)) {
    server.send(500, "text/plain", "Connection settings could not be saved. Controller restart cancelled.");
    return;
  }

  server.send(200, "text/html", "<html><body>Settings saved. Restarting ESP32...</body></html>");
  delay(250);
  ESP.restart();
}

void handleFactoryReset() {
  if (!prepareControllerRestart()) return;
  if (!preferences.clear()) {
    server.send(500, "text/plain", "Factory reset could not clear saved settings. Controller restart cancelled.");
    return;
  }
  server.send(200, "text/html", "<html><body>Factory Reset complete. Restarting in configuration mode...</body></html>");
  delay(250);
  WiFi.disconnect(true, true);
  ESP.restart();
}

void handleControllerRestart() {
  if (!prepareControllerRestart()) return;

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
    if (preferences.putString("otaPass", requestedPassword) != requestedPassword.length()) {
      server.send(500, "text/plain", "Firmware update password could not be saved.");
      return;
    }
    otaPassword = requestedPassword;
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


void handleRoot() {
  const uint32_t diagStartUs = micros();
  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);

  const bool virtualU1 = virtualU1IsEnabled();
  MoonPrinterStateCode virtualState = MOON_PRINT_UNCHANGED;
  bool virtualCover = false;
  if (virtualU1) getVirtualU1State(virtualState, virtualCover);
  const char *virtualPrint = "standby";
  switch (virtualState) {
    case MOON_PRINT_PRINTING: virtualPrint = "printing"; break;
    case MOON_PRINT_COMPLETE: virtualPrint = "complete"; break;
    case MOON_PRINT_CANCELLED: virtualPrint = "cancelled"; break;
    default: break;
  }

  // Values only used by the page. Keep the telemetry and action routes unchanged.
  String view;
  view.reserve(384 + wifiSsid.length() + deviceConfigMessage.length());
  view += F("<meta id=\"controller-view\" data-hyst=\"");
  view += String(thermal.hyst, 6);
  view += F("\" data-pre=\"");
  view += String(thermal.preFanTime / 1000UL);
  view += F("\" data-post=\"");
  view += String(thermal.postFanTime / 1000UL);
  view += F("\" data-max=\"");
  view += String(thermal.maxTempLimit, 6);
  view += F("\" data-ssid=\"");
  view += htmlEscape(wifiSsid);
  view += F("\" data-ip=\"");
  view += htmlEscape(printerIp);
  view += F("\" data-message=\"");
  view += htmlEscape(deviceConfigMessage);
  view += F("\" data-ota=\"");
  view += otaPassword.length() >= OTA_PASSWORD_MIN_LENGTH ? '1' : '0';
  view += F("\" data-portal=\"");
  view += configPortalActive ? '1' : '0';
  view += F("\" data-virtual=\"");
  view += virtualU1 ? '1' : '0';
  view += F("\" data-virtual-print=\"");
  view += virtualPrint;
  view += F("\" data-virtual-cover=\"");
  view += virtualCover ? '1' : '0';
  view += F("\" data-print=\"");
  view += htmlEscape(printerState);
  view += F("\" data-assist=\"");
  view += bedAssistActive ? "on" : bedAssistPending ? "pending" : "off";
  view += F("\">\n");

  const size_t topLength = sizeof(CHAMBER_PAGE_TOP) - 1;
  const size_t bodyLength = sizeof(CHAMBER_PAGE_BODY) - 1;
  server.setContentLength(topLength + view.length() + bodyLength);
  server.send(200, "text/html", "");
  server.sendContent_P(CHAMBER_PAGE_TOP, topLength);
  server.sendContent(view);
  server.sendContent_P(CHAMBER_PAGE_BODY, bodyLength);
  diagRootLastUs = micros() - diagStartUs;
  diagnosticMax(diagRootLastUs, diagRootMaxUs);
  diagnosticHeap();
}

void handleMode() {
  if (server.hasArg("m")) {
    String mode = server.arg("m");

    if (mode == "manual") {
      if (preheatMode) stopBedAssistManual();
      preheatMode = false;
      preheatReady = false;
    }

    if (mode == "preheat" && printerState != "printing" && bedAssistAllowed()) {
      preheatMode = true;
      preheatReady = false;
      if (!bedAssistActive && !bedAssistPending) {
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
  snapshot.valid = true;
  snapshot.httpCode = HTTP_CODE_OK;
  snapshot.topCoverDetectedValid = true;
  snapshot.topCoverDetected = topCover;
  snapshot.heap = ESP.getFreeHeap();
  snapshot.minHeap = ESP.getMinFreeHeap();
  publishMoonrakerSnapshot(snapshot, true);
}

void handleVirtualU1() {
  String mode = server.arg("mode");
  const bool sourceChanged = (mode == "on" && !virtualU1IsEnabled()) ||
                             (mode == "off" && virtualU1IsEnabled());
  if (sourceChanged) {
    // Do not apply simulated ownership or a queued operation to the other U1.
    discardU1Control();
    u1FreshPrintingDetected.store(false);
    printerState = "unknown";
    moonrakerValid = false;
    portENTER_CRITICAL(&u1TopCoverMux);
    u1TopCoverDetectionValid = false;
    u1TopCoverDetected = false;
    portEXIT_CRITICAL(&u1TopCoverMux);
  }
  if (mode == "off") {
    setVirtualU1State(false, MOON_PRINT_STANDBY, true);
    if (sourceChanged && moonrakerQueue != nullptr) xQueueReset(moonrakerQueue);
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
      // STOP disarms local heating before any U1 cleanup is scheduled.
      masterOn = false;
      autoStopLock = true;
      manualStop = true;
    } else {

      autoStopLock = false;
      masterOn = true;
    }
    heaterSafetyUnlock();
  }
  if (startBlocked) Serial.println("SAFETY START BLOCKED: safety system or fault latch active");
  if (manualStop) {
    if (preheatMode) {
      stopBedAssistManual();
      preheatMode = false;
      preheatReady = false;
    }
    releaseU1FansIfOwned("MANUAL STOP");
  }

  server.sendHeader("Location", "/");
  server.send(303);
}
 
void saveTargetTemperature(float requestedValue, bool relative) {
  if (!heaterSafetyLock()) {
    server.send(503, "text/plain", "Safety lock unavailable. Target temperature was not saved.");
    return;
  }
  const float requestedTarget = relative ? setTemp + requestedValue : requestedValue;
  const float target = constrain(requestedTarget, 0.0f, min(maxTempLimit, MAX_SET_TEMP));
  heaterSafetyUnlock();

  if (preferences.putFloat("setTemp", target) != sizeof(float)) {
    server.send(500, "text/plain", "Target temperature could not be saved.");
    return;
  }
  if (!heaterSafetyLock()) {
    server.send(503, "text/plain", "Safety lock unavailable. Saved target temperature could not be applied.");
    return;
  }
  setTemp = target;
  heaterSafetyUnlock();
  sendSettingsSaved();
}

void handleSet() {
  String input = server.arg("st");
  input.trim();
  float requestedTarget = 0.0f;
  if (!parseSettingFloat(input, requestedTarget)) {
    server.send(400, "text/plain", "Invalid or missing target temperature.");
    return;
  }
  saveTargetTemperature(requestedTarget, false);
}

void handleAdjust() {
  String input = server.arg("d");
  input.trim();
  long delta = 0;
  if (!parseSettingInt(input, delta) || delta < INT_MIN || delta > INT_MAX) {
    server.send(400, "text/plain", "Invalid or missing temperature adjustment.");
    return;
  }
  saveTargetTemperature((float)delta, true);
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

  if (!preferences.begin("chamber", false)) Serial.println("SETTINGS NVS OPEN FAILED");
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
  if (!isfinite(hyst) || hyst < 0.1f || hyst > 10.0f) hyst = 0.8f;
  if (preFanTime > 600000UL) preFanTime = 20000UL;
  if (postFanTime > 600000UL) postFanTime = 30000UL;
  if (!isfinite(maxTempLimit) || maxTempLimit < 1.0f || maxTempLimit > SAFETY_TEMP_LIMIT) {
    maxTempLimit = SAFETY_TEMP_LIMIT;
    if (preferences.putFloat("maxTemp", maxTempLimit) != sizeof(float)) Serial.println("MAX TEMP NVS SAVE FAILED");
  }
  const float allowedSetTemp = min(maxTempLimit, MAX_SET_TEMP);
  if (!isfinite(setTemp) || setTemp > allowedSetTemp) {
    setTemp = isfinite(setTemp) ? allowedSetTemp : 0.0f;
    if (preferences.putFloat("setTemp", setTemp) != sizeof(float)) Serial.println("TARGET TEMP NVS SAVE FAILED");
  } else if (setTemp < 0.0f) {
    setTemp = 0.0f;
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
  MoonrakerSnapshot moonSnapshot;
  if (moonrakerQueue != nullptr && xQueueReceive(moonrakerQueue, &moonSnapshot, 0) == pdPASS) {
    applyMoonrakerSnapshot(moonSnapshot);
  }
  if (u1FreshPrintingDetected.exchange(false)) handoffU1ToPrint();
  handleU1ReleaseRequestedBySafety();
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

  bool u1LinkOnline =
    moonrakerValid &&
    (millis() - lastMoonrakerSuccess <= moonrakerWatchdogTimeout);

  static bool lastU1LinkOnline = false;

  if (u1LinkOnline != lastU1LinkOnline) {
    Serial.print("U1 LINK: ");
    Serial.println(u1LinkOnline ? "ONLINE" : "OFFLINE");
    lastU1LinkOnline = u1LinkOnline;
  }
  unsigned long currentMillis = millis();
  recordHistorySample(currentMillis);
  HeaterTelemetrySnapshot thermal = {};
  getHeaterTelemetrySnapshot(thermal);

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

if (bedAssistActive && preheatMode && printerState != "printing" && !bedAssistRenewPending &&
    (uint32_t)(currentMillis - lastBedAssistRenew) >= BED_ASSIST_RENEW_INTERVAL && bedAssistAllowed()) {
  uint32_t generation = 0;
  bedAssistRenewPending = requestU1DesiredState(bedAssistCirculationRequested ? 0.60f : 0.0f, 0.3f, U1_BED_TARGET_100,
                                                 U1_DESIRED_BED_ASSIST_RENEW, generation);
}

updateBedAssistCirculationFan();
updateManualCirculationFan(u1LinkOnline);

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

diagnosticFinishLoop(diagLoopStartUs);

}
