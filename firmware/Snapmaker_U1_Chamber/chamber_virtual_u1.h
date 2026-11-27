#pragma once

#include <stdint.h>

enum class VirtualEvent : uint8_t {
  Enabled, Disabled, Standby, Printing, Complete, Cancelled, TopCover,
  BedTarget, BedRenewal, CavityFan, TopCoverFan, CommandOk,
  RenewalQueued, RenewalCancelled, Handoff, PreheatToManual, PreheatSelected,
  PreFan, Heater, Fan, PrintEnded, PrintCancelled
};

struct VirtualLogEntry {
  uint32_t at;
  VirtualEvent event;
  int16_t value;
};

// All access on the ESP32 is serialized by virtualU1Mux. No I/O under that lock.
struct VirtualU1Trace {
  static constexpr uint8_t capacity = 48;
  VirtualLogEntry entries[capacity] = {};
  uint8_t next = 0;
  uint8_t count = 0;
  uint32_t revision = 0;
  uint32_t commands = 0;
  int16_t bed = -1;
  int16_t cavity = -1;
  int16_t topCover = -1;
  bool handoffReported = false;
  bool haveLocalState = false;
  bool previousPreFan = false;
  bool previousRelay = false;
  bool previousFan = false;

  void add(uint32_t now, VirtualEvent event, int16_t value = 0) {
    entries[next] = {now, event, value};
    next = (next + 1) % capacity;
    if (count < capacity) ++count;
    ++revision;
  }

  const VirtualLogEntry &oldest(uint8_t offset) const {
    return entries[(next + capacity - count + offset) % capacity];
  }

  void beginSession(uint32_t now) {
    commands = 0;
    bed = cavity = topCover = -1;
    handoffReported = false;
    haveLocalState = false;
    add(now, VirtualEvent::Enabled);
  }

  void command(uint32_t now, VirtualEvent event, int16_t value) {
    switch (event) {
      case VirtualEvent::BedTarget:
      case VirtualEvent::BedRenewal: bed = value; break;
      case VirtualEvent::CavityFan: cavity = value; break;
      case VirtualEvent::TopCoverFan: topCover = value; break;
      default: break;
    }
    ++commands;
    add(now, event, value);
    add(now, VirtualEvent::CommandOk);
  }

  void observeLocal(uint32_t now, bool preFan, bool relay, bool fan) {
    if (preFan && (!haveLocalState || !previousPreFan)) add(now, VirtualEvent::PreFan);
    if (!haveLocalState || relay != previousRelay) add(now, VirtualEvent::Heater, relay);
    if (!haveLocalState || fan != previousFan) add(now, VirtualEvent::Fan, fan);
    previousPreFan = preFan;
    previousRelay = relay;
    previousFan = fan;
    haveLocalState = true;
  }
};
