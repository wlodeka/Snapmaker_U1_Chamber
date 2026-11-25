const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

const sketchDir = path.join(__dirname, '..', 'firmware', 'Snapmaker_U1_Chamber');
const readSketch = name => fs.readFileSync(path.join(sketchDir, name), 'utf8').replaceAll('\r\n', '\n');
const sketch = readSketch('Snapmaker_U1_Chamber.ino');
const types = readSketch('chamber_forward_types.h');

function matched(text, pattern) {
  const match = text.match(pattern);
  assert.ok(match, 'Missing production code: ' + pattern);
  return match[0];
}

function functionFromSketch(name) {
  return matched(sketch, new RegExp('^\\w+ ' + name + '\\([\\s\\S]*?^\\}', 'm'));
}

const constants = [
  /^#define RELAY_PIN[\s\S]*?^#define BUZZER_PIN \d+/m,
  /^const uint8_t OTA_PASSWORD_MIN_LENGTH.*$/m,
  /^float currentTemp[\s\S]*?^bool masterOn.*$/m,
  /^const float MAX_SET_TEMP.*$/m,
  /^const float SAFETY_TEMP_LIMIT.*$/m,
  /^float hyst[\s\S]*?^const unsigned long tempInterval.*$/m,
  /^unsigned long buzzerLastChange[\s\S]*?^const float RUNAWAY_TARGET_MARGIN_C.*$/m,
  /^bool heaterRelayOn[\s\S]*?^bool minOffBlockedReported.*$/m,
  /^const int BED_ASSIST_TEMP_C.*$/m,
  /^enum MoonPrinterStateCode.*$/m,
  /^struct MoonrakerSnapshot \{[\s\S]*?^\};/m,
  /^enum MoonrakerCommandType[\s\S]*?^struct MoonrakerCommandResult \{[\s\S]*?^\};/m,
  /^enum U1BedTargetAction[\s\S]*?^struct U1DesiredState \{[\s\S]*?^\};/m,
];
const functions = [
  'heaterSafetyWriteRelay', 'heaterSafetyRelayMayTurnOn', 'heaterHasStartDemand', 'heaterSafetyTask',
  'bedAssistAllowed', 'getPendingU1DesiredState', 'u1CommandGenerationCurrent', 'u1CommandCanContinue',
  'authorizeU1Write', 'finishU1DesiredSync', 'requestU1DesiredState', 'processMoonrakerCommand',
  'releaseU1FansIfOwned', 'clearBedAssistState', 'stopBedAssistManual', 'handleStart', 'handleStop',
  'saveTargetTemperature', 'authorizeOtaSettingsChange', 'handleOtaPassword', 'handleFactoryReset',
];
const state = [matched(types, /^enum HeatingState.*$/m), ...constants.map(pattern => matched(sketch, pattern))];
const output = process.argv[2];
assert.ok(output, 'Pass an output directory for generated test includes.');
fs.mkdirSync(output, { recursive: true });
fs.writeFileSync(path.join(output, 'chamber-state.inc'), state.join('\n') + '\n');
fs.writeFileSync(path.join(output, 'chamber-functions.inc'), functions.map(functionFromSketch).join('\n\n') + '\n');

const routes = functionFromSketch('setup');
for (const [route, handler] of [['start', 'Start'], ['stop', 'Stop'], ['factoryreset', 'FactoryReset']]) {
  assert.ok(routes.includes('server.on("/' + route + '", HTTP_POST, handle' + handler + ');'));
}
assert.ok(!routes.includes('"/toggle"'));
const ui = readSketch('chamber_web_ui.h');
const powerBinding = matched(ui, /^    byId\('power'\)\.addEventListener\('click', \(\) => \{[\s\S]*?^    \}\);/m);
for (const label of ['STOP', 'STOP', 'START']) {
  const power = { addEventListener(event, listener) { this.click = listener; } };
  const calls = [];
  vm.runInNewContext(powerBinding, {
    byId: id => id === 'power' ? power : { textContent: label },
    runAction: request => request(),
    controllerAction: (request, options) => calls.push([request, options.method]),
  });
  power.click();
  assert.deepEqual(calls, [[label === 'STOP' ? '/stop' : '/start', 'POST']]);
}
console.log('PASS: explicit POST routes and START/STOP label binding in two clients');
