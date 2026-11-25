const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');

const image = fs.readFileSync(process.argv[2]);
assert.ok(image.length >= 80, 'Truncated ESP image');
assert.equal(image[0], 0xe9, 'ESP image magic');
assert.equal(image.readUInt16LE(12), 9, 'ESP32-S3 target');
assert.equal(image.readUInt32LE(32), 0xabcd5432, 'Application descriptor');
assert.equal(image[23], 1, 'Appended image digest');
const segments = image[1];
assert.ok(segments > 0 && segments <= 16, 'Segment count');

let offset = 24;
let checksum = 0xef;
for (let index = 0; index < segments; index++) {
  assert.ok(offset + 8 <= image.length - 32, 'Truncated segment header');
  const end = offset + 8 + image.readUInt32LE(offset + 4);
  assert.ok(end <= image.length - 32, 'Truncated segment data');
  for (const byte of image.subarray(offset + 8, end)) checksum ^= byte;
  offset = end;
}
const checksumOffset = Math.ceil((offset + 1) / 16) * 16 - 1;
const digestOffset = checksumOffset + 1;
assert.equal(image[checksumOffset], checksum, 'ESP segment checksum');
assert.equal(image.length, digestOffset + 32, 'ESP image length');
const digest = crypto.createHash('sha256').update(image.subarray(0, digestOffset)).digest();
assert.ok(digest.equals(image.subarray(digestOffset)), 'ESP image digest');
const sha = crypto.createHash('sha256').update(image).digest('hex');
console.log('Verified ESP32-S3 OTA image: ' + image.length + ' bytes, SHA-256 ' + sha);
