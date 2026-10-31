#pragma once

#include <stdint.h>

// These declarations are intentionally available before Arduino injects
// automatic .ino function prototypes.  Full definitions remain in the sketch.
enum HeatingState { IDLE, PRE_FAN_WAITING, HEATING_ACTIVE, POST_FAN_COOLING };
enum MoonPrinterStateCode : uint8_t;
enum MoonKlipperStateCode : uint8_t;
enum MoonrakerCommandType : uint8_t;
enum U1BedTargetAction : uint8_t;
enum U1DesiredOperation : uint8_t;
enum SafetyFaultReason : uint8_t;

struct MoonrakerSnapshot;
struct MoonrakerCommand;
struct MoonrakerCommandResult;
struct U1DesiredState;
struct FaultDetails;
struct PersistedFaultRecord;
struct HeaterTelemetrySnapshot;
