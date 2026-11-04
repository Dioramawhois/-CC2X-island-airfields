# Historical native/Wasm prototype

This evidence belongs to the earlier native/Wasm experiment retained from main.
Island Airfields 2.0 uses the separate Lua module. The current package builder
includes none of this prototype's native/Wasm files. Its integration-era tests
are historical evidence, not validation of the current Lua player package.

---

# Native Wasm host checkpoint вЂ” 2026-10-31

Status: experimental integration implemented; NOT approved for distribution.

## Provenance

`wasm-runtime.lock.json` records official wasm3 tag-ref and commit API URLs (HTTP200), exact commit `ac3c1dd1386e83be7de548211efd02805eb1dcee`, raw source digests independently matched by build worker, archive digest and MIT license. Commit unsigned; HTTPS SHA pin is not publisher signature assurance. No fork or installed interpreter/npm dependency.

## Implemented

- `plugin/wasm_host.c/.h`: actual embedded C interpreter; no native imports linked. Reject imported capabilities, start section, and non-128KiB fixed-memory modules before load. 64KiB module-file cap and interpreter stack. Validate complete ABI/layout query table, signatures, bounded module-owned buffers; copy records, never pointers. Reset intrinsic gas budget before every export, armed before lazy compilation. Permanent fail-closed on rejected dispatch/trap.
- `plugin/wasm_bridge.h` and adapter changes: existing production core calls redirected into reactor. Generation handles tracked against flight state; hybrid remaining-native helicopter state synchronized via SET_STATE. No linked native FD implementation fallback. Staged landing/terminal ABORT preserves adapter order; ABORT count3 leads to drop, not SET_STATE3. Native game effects and remaining business logic are NOT portable core.
- Module lookup beside own DLL only, named `cc2x_island_airfields_core.wasm`. No current-directory/mod directory scanning. Initialization deferred from plugin load to first server tick; owner-thread/reentrancy guards. Internal FD callbacks have a trap-unwind boundary, no fallback after fault.

## Executed evidence

1. Native arm64 cc O2 executable with actual vendored interpreter: own minimal infinite branch loop returns `[trap] out of gas`, under outer 15s hard-kill watchdog.
2. Same executable loads final production module `be3eb6adfd5d9ffe6839ab19aef9cb85143a182f6f1373f58bec137d7e6647db`; slots, line-up, stale-handle permanent failure PASS. Actual host terminal regression PASS: SET_STATE count2/FINAL в†’ unsafe LANDING returns staged ABORT3 в†’ GET still count2 в†’ DROP в†’ replacement generation fresh count0 в†’ stale handle permanently closes host. No SET_STATE3, no state leak.
3. 1000 sequential actual module FLY ticks compared with test-only native production reference; state and command error <1e-10 PASS. Native reference is not linked to DLL.
4. Actual host+interpreter Zig x86_64-macos shared-library compile PASS (Mach-O x86_64). Native execution is arm64. Existing plugin remains Windows-specific, not a complete Mac game adapter.
5. Full all-vendor ASan+UBSan executable FAILS during production module initialization: null stack dereference in upstream `m3_env.c:415`, EvaluateExpression / InitGlobals / m3_LoadModule. This is unresolved; normal O2 pass does not override it. No vendor patch, sanitizer suppression, or VM switch applied.
6. Win32 syntax-only approach stopped after two attempts: first caught/fixed log API arity; second Zig `-fsyntax-only` returned `FileNotFound` for both existing source files. Actual DLL build belongs to build worker and is pending at this checkpoint.

## Build handoff

DLL units: `plugin/island_airfields.c`, `plugin/wasm_host.c`, and `third_party/wasm3/source/{m3_bind,m3_code,m3_compile,m3_core,m3_deterministic,m3_env,m3_exec,m3_function,m3_info,m3_module,m3_parse,m3_validate}.c` (12 vendor,14 total). No `m3_api*.c`, core.c, or module.c in DLL. Extra include third_party/wasm3/source; explicit `-Dd_m3HasGasMetering=1 -Dd_m3HasValidation=1`. Mac links libm; Win uses SDK defaults. Upstream emits unused-parameter/sign warnings, no implicit-function-declaration disabling needed. MIT packaging license `third_party/wasm3/LICENSE`.

Frozen host snapshot for build:

- island_airfields.c f2d50d31a911d78f71d05336ff5db4492216b141c5bc6cb1ff238d5e828ae917
- wasm_host.c b1a340748b3aed72db35cc6b7b7b21a48cd2edf58454a3d6bc18b003d3a9d34d
- wasm_host.h a96ff5b8979a5d70c5792f1a1114bb8ad97b01a9617fffce42518e07a5048de4
- wasm_bridge.h 3e4099622fb0d017545485285f6571a61db1f2509244bd0da74163103c145506

## Unresolved gates

ASanUBSan failure; final independent security sentinel; Windows actual DLL build/package tests; malformed envelope/ABI negative tests beyond stale handle; plugin callback integration/gameplay; performance budget; cryptographic package authentication. No game launched/injected, no install/deploy/Workshop edit, no global dependencies, no commits/push. Runtime resource caps do not prove hostile-input safety. Failed runtime disables plugin effects; rollback of already-applied game effects is not implemented.
