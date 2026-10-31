/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Developer-only Node built-in WebAssembly test of the actual production payload. No npm/world. */
'use strict';
const fs = require('fs');
const assert = require('assert/strict');
const crypto = require('crypto');
const path = require('path');
const bytes = fs.readFileSync(process.argv[2] || path.join(__dirname, '../dist/cc2x_island_airfields_core.wasm'));
const mod = new WebAssembly.Module(bytes);
assert.deepEqual(WebAssembly.Module.imports(mod), []);
const e = new WebAssembly.Instance(mod, {}).exports;
e._initialize();
assert.equal(e.iaf_module_query(0), 1);
const reqSize = e.iaf_module_query(2), resSize = e.iaf_module_query(3);
assert.equal(reqSize, 344); assert.equal(resSize, 320);
function range(off, size) {
    assert(Number.isInteger(off) && off >= 0 && off <= e.memory.buffer.byteLength);
    assert(Number.isInteger(size) && size >= 0 && size <= e.memory.buffer.byteLength - off);
    return off;
}
const q = range(e.iaf_module_request_offset() >>> 0, reqSize);
const r = range(e.iaf_module_response_offset() >>> 0, resSize);
assert(q + reqSize <= r || r + resSize <= q);
const d = new DataView(e.memory.buffer);
const rqState = q + e.iaf_module_query(5), landing = q + e.iaf_module_query(6);
const a = q + e.iaf_module_query(7), b = q + e.iaf_module_query(8), args = q + e.iaf_module_query(9);
const flag = q + e.iaf_module_query(11), state = r + e.iaf_module_query(12);
const command = r + e.iaf_module_query(13), output = r + e.iaf_module_query(14), runway = r + e.iaf_module_query(15);
const values = r + e.iaf_module_query(16);
function f(off, value) { d.setFloat64(off, value, true); }
function fget(off) { return d.getFloat64(off, true); }
function i(off, value) { d.setInt32(off, value, true); }
function iget(off) { return d.getInt32(off, true); }
function vec(off, list) { list.forEach((value, n) => f(off + n * 8, value)); }
function copy(dst, src, size) { new Uint8Array(e.memory.buffer, dst, size).set(new Uint8Array(e.memory.buffer, src, size)); }
function call(op, h = 0) {
    i(q, 1); i(q + 4, reqSize); i(q + 8, op); i(q + 12, h);
    const status = e.iaf_module_dispatch(reqSize);
    assert.equal(iget(r), 1); assert.equal(iget(r + 4), resSize); assert.equal(iget(r + 8), status);
    return status;
}
let h = e.iaf_module_create(2);
assert.notEqual(h, 0);
assert.equal(call(1, h), 0); assert.equal(fget(state), 1e9); assert.equal(iget(state + 48), 2);
assert.equal(call(1, (h & ~255) | 255), 1);
assert.equal(call(1, h ^ 256), 1);
assert.equal(call(0, h), 2); assert.equal(call(19, h), 2);
assert.equal(e.iaf_module_dispatch(0xffffffff), 3);
vec(a, [30, -2, 40]); i(flag, 0);
assert.equal(call(3, h), 0); assert.equal(fget(state + 8), 50); assert.equal(fget(state + 16), -2);
// Known input plane-fly step. Old rates: +.4 speed, -.8 vz, heading +.014, bank -.08.
copy(rqState, state, 80); vec(rqState, [0, 70, 0, 0]); assert.equal(call(2, h), 0);
vec(args, [100, .1, .14, 75, 50, 0, 15, 0]);
assert.equal(call(11, h), 0);
assert(Math.abs(fget(state) - .014) < 1e-15);
assert(Math.abs(fget(state + 8) - 70.4) < 1e-12);
assert(Math.abs(fget(state + 16) + .8) < 1e-15);
assert(Math.abs(fget(command + 40) + .08) < 1e-15);
// Runway creation + point/frame round trip through module only.
vec(a, [100, 20, -100]); vec(b, [100, 20, 1400]); i(flag, 0);
assert.equal(call(6), 0); assert.equal(iget(r + 12), 1); assert.equal(fget(runway + 40), 1500);
copy(landing, runway, 48); vec(args, [2000, 0]); assert.equal(call(8), 0);
copy(landing + 48, values, 24); f(landing + 56, 270);
vec(landing + 72, [.25, 20, 250]); i(landing + 96, 1);
copy(rqState, state, 80); f(rqState, 0); i(rqState + 48, 2); assert.equal(call(2, h), 0);
assert.equal(call(14, h), 0); assert.equal(iget(state + 48), 5); assert.equal(iget(output + 88), 1);
// Staging: not committed until SET_STATE.
copy(rqState, state, 80); assert.equal(call(1, h), 0); assert.equal(iget(state + 48), 2);
assert.equal(call(2, h), 0); assert.equal(call(1, h), 0); assert.equal(iget(state + 48), 5);
// Contact threshold strictness. s=150 doesn't touch; s=149 and contact does; still staged.
vec(args, [150, 2, 62]); i(flag, 1); assert.equal(call(15, h), 0); assert.equal(iget(r + 12), 0);
f(args, 149); assert.equal(call(15, h), 0); assert.equal(iget(r + 12), 1); assert.equal(iget(state + 48), 6);
assert.equal(call(1, h), 0); assert.equal(iget(state + 48), 5);
// Preserve nonfinite core semantics: NaN doesn't rotate/reset vz, clamp preserves NaN.
copy(rqState, state, 80); f(rqState + 8, NaN); f(rqState + 16, 3); assert.equal(call(2, h), 0);
vec(args, [0, .1, 0]); assert.equal(call(13, h), 0); assert.equal(iget(r + 12), 0);
assert(Number.isNaN(fget(state + 8))); assert.equal(fget(state + 16), 3);
vec(args, [NaN, 0, 1]); assert.equal(call(17), 0); assert(Number.isNaN(fget(values)));
// SET_STATE is a trust boundary: invalid retries cannot mutate a live slot or overflow ++.
assert.equal(call(1, h), 0);
const before = new Uint8Array(e.memory.buffer, state, 80).slice();
for (const bad of [2147483647, -2147483648, -1, 3]) {
    new Uint8Array(e.memory.buffer, rqState, 80).set(before);
    i(rqState + 60, bad);
    assert.equal(call(2, h), 5);
    assert.equal(call(1, h), 0);
    assert.deepEqual(new Uint8Array(e.memory.buffer, state, 80), before);
}
new Uint8Array(e.memory.buffer, rqState, 80).set(before);
i(rqState + 48, 5); i(rqState + 60, 2); f(rqState, 0); f(rqState + 8, 70);
assert.equal(call(2, h), 0);
vec(args, [250, 61]); assert.equal(call(8), 0);
copy(landing + 48, values, 24); f(landing + 56, 70);
assert.equal(call(14, h), 0);
assert.equal(iget(output + 88), 3); assert.equal(iget(state + 60), 3);
copy(rqState, state, 80); assert.equal(call(2, h), 5); // terminal abort is not committed
assert.equal(call(1, h), 0); assert.equal(iget(state + 60), 2);
// Capacity, stale slot reuse, reset invalidate, no caller-supplied pointer to any operation.
const handles = [h];
for (let n = 1; n < e.iaf_module_query(1); ++n) { const x = e.iaf_module_create(1); assert.notEqual(x, 0); handles.push(x); }
assert.equal(e.iaf_module_create(1), 0);
assert.equal(e.iaf_module_drop(h), 0); assert.equal(call(1, h), 1);
const next = e.iaf_module_create(1); assert.notEqual(next, h); assert.equal(next & 255, h & 255);
assert.equal(call(1, h), 1); assert.equal(e.iaf_module_reset(), 0);
for (const x of [...handles, next]) assert.equal(call(1, x), 1);
assert.notEqual(e.iaf_module_create(1), next);
assert.equal(e.memory.buffer.byteLength, 131072);
assert.throws(() => e.memory.grow(1), RangeError);
assert.throws(() => range(0xffffffff, 344));
assert.throws(() => range(131000, 344));
console.log('module_wasm_test: ok; imports=0, memory=128KiB fixed; sha256=' + crypto.createHash('sha256').update(bytes).digest('hex'));
