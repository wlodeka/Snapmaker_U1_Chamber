#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <cstring>
#include <map>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using std::min;
struct FaultDetails {};
enum SafetyFaultReason { SAFETY_FAULT_NONE, SAFETY_FAULT_NTC, SAFETY_FAULT_OVERTEMP, SAFETY_FAULT_RUNAWAY, SAFETY_FAULT_SYSTEM };
#include "chamber-state.inc"
#include "../firmware/Snapmaker_U1_Chamber/chamber_virtual_u1.h"

enum { LOW = 0, HIGH = 1 };
bool heaterSafetySystemReady, safetyFaultLatched, otaUpdateRequested, otaSafetyReady;
bool preheatReadyBeepRequested, preheatReadyBeepActive, faultResetRequested;
bool linkUp, ntcValid;
SafetyFaultReason safetyFaultReason;
FaultDetails currentFaultDetails;
struct SerialMock {
  void println(const char*) {}
  template<class... T> void printf(const char*, T...) {}
} Serial;
using String = std::string;
struct ServerMock {
  std::map<String, String> args;
  int response = 0;
  String body;
  String arg(const char *name) { return args.contains(name) ? args.at(name) : ""; }
  void sendHeader(const char*, const char*) {}
  void send(int status, const char* = "", const String &payload = {}) { response = status; body = payload; }
} server;
enum class StorageError { None, Open, Lookup, Read, Write } storageError;
std::optional<String> savedCredential;
String otaPassword, deviceConfigMessage;
int clears, restartChecks, restarts;
struct PreferencesMock {
  size_t putFloat(const char*, float) { return sizeof(float); }
  size_t putString(const char*, const String &value) {
    if (storageError == StorageError::Write) return 0;
    savedCredential = value;
    return value.length();
  }
  bool clear() { ++clears; savedCredential.reset(); return true; }
} preferences;
bool prepareControllerRestart() { ++restartChecks; return true; }
void delay(unsigned long) {}
const int WL_CONNECTED = 1;
struct { void disconnect(bool, bool) {} int status() { return WL_CONNECTED; } } WiFi;
struct { void restart() { ++restarts; } } ESP;
float constrain(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }
void sendSettingsSaved() {}

int queuedCommands, u1Writes, lastBedWrite;
bool bedAssistActive, bedAssistPending, bedAssistRenewPending, bedAssistCirculationRequested;
bool u1FansReleasePending, u1CirculationFanOwned, u1CavityFanOwned, u1ReleaseRequestedBySafety;
enum { CIRCULATION_SYNCED };
int bedAssistCirculationSync;
std::atomic<bool> u1FreshPrintingDetected{false};
U1DesiredState u1DesiredState{};
bool moonrakerTaskReady = true;
bool u1BedAssistTargetSet, u1BedAssistTargetSimulated;
void *moonrakerCommandResultQueue = nullptr;
const int HTTP_CODE_OK = 200;
const unsigned long moonrakerPollInterval = 5000;
bool u1CommandLock() { return true; }
void u1CommandUnlock() {}
bool enqueueMoonrakerCommand(MoonrakerCommandType, uint32_t) { ++queuedCommands; return true; }
int virtualU1Mux, u1TopCoverMux;
void portENTER_CRITICAL(int*) {}
void portEXIT_CRITICAL(int*) {}
bool virtualU1Enabled, virtualU1TopCoverDetected, u1TopCoverDetectionValid, u1TopCoverDetected;
MoonPrinterStateCode virtualU1PrinterState;
VirtualU1Trace virtualU1Trace;
String printerState, moonrakerGcodeUrl = "test-double";
bool printWasActive;
uint32_t lastBedAssistRenew;
int httpBegins;
std::function<void()> httpOpened;
struct HTTPClient {
  void setConnectTimeout(int) {}
  void setTimeout(int) {}
  bool begin(const String&) { ++httpBegins; if (httpOpened) httpOpened(); return true; }
  void addHeader(const char*, const char*) {}
  int POST(const String &body) {
    ++u1Writes;
    if (body.find("TARGET=0") != String::npos) lastBedWrite = 0;
    return HTTP_CODE_OK;
  }
  void end() {}
};
void xQueueOverwrite(void*, const MoonrakerCommandResult*) {}
struct HeaterTelemetrySnapshot { bool masterOn; HeatingState chamberState; };
bool getHeaterTelemetrySnapshot(HeaterTelemetrySnapshot &snapshot) {
  snapshot = {masterOn, chamberState};
  return true;
}
namespace FreshJson {
enum FreshPrintState { FRESH_PRINT_NOT_RUNNING, FRESH_PRINT_RUNNING };
FreshPrintState printerState;
FreshPrintState queryFreshMoonrakerPrintState(MoonrakerSnapshot *bedCheck) {
  if (bedCheck) { bedCheck->bedTargetValid = true; bedCheck->bedTarget = BED_ASSIST_TEMP_C; }
  if (virtualU1Enabled && virtualU1PrinterState == MOON_PRINT_PRINTING) return FRESH_PRINT_RUNNING;
  return printerState;
}
}

unsigned long nowMs;
float ntc;
int relayLevel, fanLevel;
std::vector<std::pair<unsigned long,int>> edges;
std::function<void()> afterTick;
int ticksRemaining;
struct EndScenario {};
unsigned long millis() { return nowMs; }
bool heaterSafetyLock() { return true; }
void heaterSafetyUnlock() {}
bool heaterSafetyU1LinkOnline() { return linkUp; }
bool readNTCTemperature(float &result) { result=ntc; return ntcValid; }
void digitalWrite(int pin,int value) {
  if (pin == RELAY_PIN) {
    if (relayLevel != value) edges.emplace_back(nowMs, value);
    relayLevel = value;
  }
  if (pin == FAN_PIN) fanLevel = value;
}
void latchSafetyFault(SafetyFaultReason reason,unsigned long,bool,bool) {
  safetyFaultReason = reason;
  safetyFaultLatched = true;
}
const char* safetyFaultReasonText(SafetyFaultReason) { return "stub fault"; }
void updateAlarmBuzzer(unsigned long) {}
void updatePreheatReadyBuzzer(unsigned long) {}
using TickType_t = unsigned long;
TickType_t xTaskGetTickCount() { return nowMs; }
TickType_t pdMS_TO_TICKS(TickType_t ms) { return ms; }
void vTaskDelay(TickType_t) { throw EndScenario{}; }
void vTaskDelayUntil(TickType_t*,TickType_t period) {
  if (afterTick) afterTick();
  if (--ticksRemaining == 0) throw EndScenario{};
  nowMs += period;
}
#define ESP_IDF_VERSION_MAJOR 5
using esp_err_t=int;
const esp_err_t ESP_OK=0, ESP_ERR_INVALID_STATE=1, ESP_ERR_NOT_FOUND=2;
struct esp_task_wdt_config_t { uint32_t timeout_ms, idle_core_mask; bool trigger_panic; };
int reportedWdtStatus, wdtAddResult, wdtInitCalls, wdtAddCalls, wdtResetCalls;
esp_task_wdt_config_t configuredWdt;
esp_err_t esp_task_wdt_status(void*) { return reportedWdtStatus; }
esp_err_t esp_task_wdt_init(const esp_task_wdt_config_t *cfg) { ++wdtInitCalls; configuredWdt=*cfg; return ESP_OK; }
esp_err_t esp_task_wdt_add(void*) { ++wdtAddCalls; return wdtAddResult; }
esp_err_t esp_task_wdt_reset() { ++wdtResetCalls; return ESP_OK; }
using nvs_handle_t = int;
const int NVS_READONLY = 0, ESP_ERR_NVS_NOT_FOUND = 3;
esp_err_t nvs_open(const char*, int, nvs_handle_t *handle) {
  if (storageError == StorageError::Open) return ESP_ERR_INVALID_STATE;
  *handle = 1;
  return ESP_OK;
}
esp_err_t nvs_get_str(nvs_handle_t, const char*, char *out, size_t *size) {
  if (storageError == (out ? StorageError::Read : StorageError::Lookup)) return ESP_ERR_INVALID_STATE;
  if (!savedCredential) return ESP_ERR_NVS_NOT_FOUND;
  if (out) std::memcpy(out, savedCredential->c_str(), savedCredential->size()+1);
  *size = savedCredential->size()+1;
  return ESP_OK;
}
void nvs_close(nvs_handle_t) {}
#include "chamber-functions.inc"

void require(bool passed, const char *message) {
  if (!passed) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
void setupScenario() {
  nowMs=1000; ntc=25.0f; ntcValid=true; linkUp=true;
  edges.clear(); afterTick=nullptr; relayLevel=LOW; fanLevel=LOW;
  currentTemp=0.0f; setTemp=30.0f; hyst=0.8f;
  preFanTime=20000; postFanTime=30000; maxTempLimit=65.0f;
  masterOn=false; autoStopLock=false; preheatMode=false; preheatReady=false;
  chamberState=IDLE; stateStartTime=0; lastTempRequest=0;
  heaterSafetySystemReady=false; heaterRelayOn=false; relayOffTimestampValid=false;
  minOffBlockedReported=false; thermistorFault=true; overTemperatureFault=false;
  hasValidNtcTemperature=false; safetyAlarmActive=false; buzzerOn=false;
  safetyFaultLatched=false; faultResetRequested=false;
  otaUpdateRequested=false; otaSafetyReady=false; runawayMonitoring=false;
  preheatReadyBeepRequested=false; preheatReadyBeepActive=false;
  queuedCommands=0; u1Writes=0; lastBedWrite=-1; u1DesiredState={};
  u1FreshPrintingDetected=false; u1ReleaseRequestedBySafety=false;
  virtualU1Enabled=false; virtualU1PrinterState=MOON_PRINT_STANDBY; virtualU1TopCoverDetected=true;
  u1TopCoverDetectionValid=true; u1TopCoverDetected=true; virtualU1Trace={};
  printWasActive=false; printerState="standby"; lastBedAssistRenew=0;
  u1BedAssistTargetSet=false; u1BedAssistTargetSimulated=false; httpBegins=0; httpOpened=nullptr;
  FreshJson::printerState=FreshJson::FRESH_PRINT_NOT_RUNNING;
  server={}; otaPassword.clear(); savedCredential.reset();
  storageError=StorageError::None; clears=0; restarts=0; restartChecks=0;
  bedAssistActive=false; bedAssistPending=false; bedAssistRenewPending=false;
  bedAssistCirculationRequested=false; u1FansReleasePending=false;
  u1CirculationFanOwned=false; u1CavityFanOwned=false;
  reportedWdtStatus=ESP_ERR_NOT_FOUND; wdtAddResult=ESP_OK;
  configuredWdt={}; wdtInitCalls=0; wdtAddCalls=0; wdtResetCalls=0;
}
void runTask(int ticks) {
  ticksRemaining=ticks;
  try { heaterSafetyTask(nullptr); } catch (const EndScenario&) {}
}

void testPower() {
  setupScenario();
  heaterSafetySystemReady = true;
  handleStart();
  require(masterOn && server.response == 303, "first START arms");
  chamberState = HEATING_ACTIVE;
  stateStartTime = 4321;
  heaterRelayOn = true;
  handleStart();
  require(masterOn && heaterRelayOn && chamberState == HEATING_ACTIVE && stateStartTime == 4321, "repeated START preserves cycle");
  u1CirculationFanOwned = true;
  handleStop();
  handleStop();
  require(!masterOn && queuedCommands == 1 && server.response == 303, "two STOP disarm and queue fan release once");
  for (int blocker = 0; blocker < 3; ++blocker) {
    heaterSafetySystemReady = blocker != 0;
    safetyFaultLatched = blocker == 1;
    otaUpdateRequested = blocker == 2;
    handleStart();
    require(!masterOn && server.response == 409, "START is blocked by safety system, fault or OTA");
  }
  std::puts("PASS: repeated START/STOP and START lockouts");
}

void testBedAssist(bool armed, bool printing) {
  setupScenario();
  heaterSafetySystemReady = true;
  masterOn = armed;
  preheatMode = true;
  preheatReady = armed;
  bedAssistActive = armed;
  bedAssistPending = !armed;
  u1BedAssistTargetSet = true;
  u1CavityFanOwned = true;
  handleStop();
  handleStop();
  require(!masterOn && !preheatMode && !preheatReady && !bedAssistActive && !bedAssistPending &&
          queuedCommands == 1 && u1DesiredState.operation == U1_DESIRED_BED_ASSIST_MANUAL_OFF,
          "STOP cancels PREHEAT and queues one assisted stop");
  FreshJson::printerState = printing ? FreshJson::FRESH_PRINT_RUNNING : FreshJson::FRESH_PRINT_NOT_RUNNING;
  processMoonrakerCommand({MOON_COMMAND_SYNC_U1_DESIRED, u1DesiredState.generation});
  if (printing) require(u1Writes == 0 && u1FreshPrintingDetected, "fresh PRINTING blocks all writes");
  else require(u1Writes == 3 && lastBedWrite == 0, "owned bed and fans stop for idle U1");
  std::printf("PASS: PREHEAT STOP (%s, %s)\n", armed ? "armed" : "unarmed", printing ? "printing" : "idle");
}

void testPreFan(int scenario) {
  setupScenario();
  masterOn = true;
  const bool deferred = scenario == 3;
  if (deferred) {
    preFanTime = 2000;
    relayOffTimestampValid = true;
    lastRelayOffTime = 1000;
  }
  afterTick = [&] {
    if (nowMs == 1000) require(chamberState == PRE_FAN_WAITING, "enter PRE-FAN at 25 C / 30 C");
    if (nowMs == 5000 && scenario == 0) saveTargetTemperature(20.0f, false);
    if (nowMs == 5000 && scenario == 1) ntc = 31.0f;
    if (nowMs == 5000 && scenario == 2) ntc = 29.8f;
    if (deferred && nowMs == 3000) require(chamberState == PRE_FAN_WAITING && minOffBlockedReported, "minimum OFF defers relay");
    if (deferred && nowMs == 6000) saveTargetTemperature(20.0f, false);
  };
  runTask(210);
  require(masterOn, "PRE-FAN preserves arming");
  if (scenario == 4) {
    require(edges.size() == 1 && edges[0] == std::make_pair(21000UL, int(HIGH)) && chamberState == HEATING_ACTIVE,
            "heat demand energizes relay after PRE-FAN");
  } else {
    require(edges.empty() && chamberState == POST_FAN_COOLING && fanLevel == HIGH, "lost demand cancels PRE-FAN without relay pulse");
  }
  const char *cases[] = {"lower target", "warmer chamber", "hysteresis band", "minimum-OFF deferral", "continued demand"};
  std::printf("PASS: PRE-FAN (%s)\n", cases[scenario]);
}

void testOta() {
  setupScenario();
  server.args["otaPassword"] = "first-test-password";
  handleOtaPassword();
  require(server.response == 303 && otaPassword == "first-test-password" && savedCredential == otaPassword, "first setup");
  server.args["otaPassword"] = "second-test-password";
  server.args["currentOtaPassword"] = "incorrect-test-password";
  handleOtaPassword();
  require(server.response == 403 && savedCredential == "first-test-password", "wrong existing password");
  server.args["currentOtaPassword"] = otaPassword;
  handleOtaPassword();
  require(server.response == 303 && otaPassword == "second-test-password" && savedCredential == otaPassword, "authenticated rotation");
  for (auto error : {StorageError::Open, StorageError::Lookup, StorageError::Read}) {
    storageError = error;
    server.args["confirm"] = "1";
    server.args["currentOtaPassword"] = otaPassword;
    server.args["otaPassword"] = "third-test-password";
    handleOtaPassword();
    require(server.response == 503 && savedCredential == otaPassword, "read failure blocks rotation");
    handleFactoryReset();
    require(server.response == 503 && clears == 0 && restartChecks == 0, "read failure blocks reset");
  }
  storageError = StorageError::Write;
  handleOtaPassword();
  require(server.response == 500 && otaPassword == "second-test-password" && savedCredential == otaPassword, "write failure preserves old password");
  std::puts("PASS: OTA first setup, rotation and NVS failures");

  storageError = StorageError::None;
  server.args.erase("confirm");
  handleFactoryReset();
  require(server.response == 400 && clears == 0 && restartChecks == 0, "reset requires confirmation");
  server.args["confirm"] = "1";
  server.args.erase("currentOtaPassword");
  handleFactoryReset();
  require(server.response == 403 && clears == 0 && restartChecks == 0, "reset requires existing password before safety handshake");
  server.args["currentOtaPassword"] = otaPassword;
  handleFactoryReset();
  require(server.response == 200 && clears == 1 && restartChecks == 1 && restarts == 1 && !savedCredential, "confirmed authenticated reset");
  std::puts("PASS: Factory Reset confirmation and authentication");
}
void testVirtualU1() {
  setupScenario();
  setVirtualU1State(true, MOON_PRINT_STANDBY, true);
  require(virtualU1Trace.count == 3 && virtualU1Trace.oldest(0).event == VirtualEvent::Enabled &&
          virtualU1Trace.oldest(2).event == VirtualEvent::TopCover, "enable records a simulated state and cover");
  setVirtualU1State(true, MOON_PRINT_STANDBY, true);
  require(virtualU1Trace.count == 3, "unchanged virtual state is not logged");
  preheatMode = bedAssistActive = u1CavityFanOwned = u1CirculationFanOwned = true;
  uint32_t generation;
  requestU1DesiredState(0.6f, 0.3f, U1_BED_TARGET_100, U1_DESIRED_BED_ASSIST_START, generation);
  processMoonrakerCommand({MOON_COMMAND_SYNC_U1_DESIRED, generation});
  require(httpBegins == 0 && u1Writes == 0 && virtualU1Trace.commands == 3, "simulator sends no HTTP");
  require(virtualU1Trace.bed == 100 && virtualU1Trace.cavity == 30 && virtualU1Trace.topCover == 60, "recorded values match actual command path");
  require(virtualU1Trace.oldest(7).event == VirtualEvent::BedTarget &&
          virtualU1Trace.oldest(8).event == VirtualEvent::CommandOk, "command and simulated reply are ordered");
  requestU1DesiredState(0.6f, 0.3f, U1_BED_TARGET_100, U1_DESIRED_BED_ASSIST_RENEW, generation);
  processMoonrakerCommand({MOON_COMMAND_SYNC_U1_DESIRED, generation});
  require(virtualU1Trace.oldest(9).event == VirtualEvent::BedRenewal && virtualU1Trace.commands == 4, "actual renewal recorded");
  handleVirtualDiagnostics();
  require(server.body.find("\"targets\":[100,30,60],\"active\":[1,1,1]") != String::npos &&
          server.body.find("\"renew\":239000") != String::npos, "active indicators and renewal timer");
  setVirtualU1State(true, MOON_PRINT_PRINTING, true);
  const auto previous = u1DesiredState;
  require(sendU1BedTarget(previous, 100) != HTTP_CODE_OK && virtualU1Trace.commands == 4, "printing blocks an admitted simulation write");
  handoffU1ToPrint();
  require(virtualU1Trace.oldest(12).event == VirtualEvent::RenewalCancelled &&
          virtualU1Trace.oldest(13).event == VirtualEvent::PreheatToManual &&
          virtualU1Trace.oldest(14).event == VirtualEvent::Handoff, "print handoff records actual transitions");
  handoffU1ToPrint();
  require(virtualU1Trace.count == 15 && virtualU1Trace.commands == 4, "handoff adds no repeated events or zero commands");
  handleVirtualDiagnostics();
  require(server.body.find("\"targets\":[100,30,60],\"active\":[0,0,0]") != String::npos &&
          server.body.find("\"renew\":-1") != String::npos, "handoff preserves last values and clears active outputs");
  setVirtualU1State(true, MOON_PRINT_COMPLETE, false);
  setVirtualU1State(true, MOON_PRINT_CANCELLED, false);
  require(virtualU1Trace.oldest(15).event == VirtualEvent::Complete &&
          virtualU1Trace.oldest(16).event == VirtualEvent::TopCover && virtualU1Trace.oldest(16).value == 0 &&
          virtualU1Trace.oldest(17).event == VirtualEvent::Cancelled, "terminal states and cover loss recorded");
  u1TopCoverDetected = false;
  require(sendMoonrakerFanSpeed(previous, "circulation_fan", 0.6f) == HTTP_CODE_OK &&
          virtualU1Trace.commands == 4, "skipped circulation is not logged as TX");
  setVirtualU1State(false, MOON_PRINT_STANDBY, true);
  require(virtualU1Trace.oldest(18).event == VirtualEvent::Disabled &&
          sendU1BedTarget(previous, 100) != HTTP_CODE_OK && httpBegins == 0, "simulator work cannot fall through to HTTP after exit");
  U1DesiredState real{};
  httpOpened = [] { setVirtualU1State(true, MOON_PRINT_STANDBY, true); };
  require(sendU1BedTarget(real, 100) != HTTP_CODE_OK && u1Writes == 0, "mode switch before POST blocks a real write");
  require(virtualU1Trace.commands == 0 && virtualU1Trace.bed == -1, "new simulation clears last values and count");
  std::puts("PASS: simulator commands, source isolation, renewal and print handoff");
}

void testVirtualLogCapacity() {
  VirtualU1Trace trace;
  trace.observeLocal(0, false, false, false);
  trace.observeLocal(1, false, false, false);
  trace.observeLocal(2, true, true, true);
  require(trace.count == 5 && trace.oldest(2).event == VirtualEvent::PreFan &&
          trace.oldest(3).event == VirtualEvent::Heater && trace.oldest(4).event == VirtualEvent::Fan,
          "local output observations record edges rather than polling");
  for (uint32_t i = 100; i < 180; ++i) trace.add(i, VirtualEvent::Standby);
  require(trace.count == VirtualU1Trace::capacity && trace.oldest(0).at == 132 &&
          trace.oldest(VirtualU1Trace::capacity - 1).at == 179, "ring retains newest entries in order");
  std::puts("PASS: bounded 48-event RAM ring and local output transitions");
}

int main() {
  testPower();
  testBedAssist(true, false);
  testBedAssist(false, false);
  testBedAssist(false, true);
  for (int scenario = 0; scenario < 5; ++scenario) testPreFan(scenario);
  testOta();
  testVirtualU1();
  testVirtualLogCapacity();
}
