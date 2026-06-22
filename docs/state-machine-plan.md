# State Machine Plan

## Purpose

The state machine is the center of bnt product logic. It makes the MVP testable without hardware and keeps simulator and firmware behavior aligned.

## States

Required MVP states:

```text
BOOT
IDLE
RECORDING
SENDING
SPEAKING
ERROR
```

Do not add future states until needed.

## Events

Required transition events:

```text
BOOT_COMPLETE
BUTTON_PRESSED
BUTTON_RELEASED
MAX_RECORDING_REACHED
SEND_SUCCEEDED
SEND_FAILED
PLAYBACK_FINISHED
PLAYBACK_FAILED
ERROR_HANDLED
TIMEOUT
```

Optional telemetry/internal events:

```text
SEND_STARTED
PLAYBACK_STARTED
```

These optional events may be logged by the orchestrator, but they should not be required for state transitions in the MVP.

Events should be emitted by the layer that observes the result:

- button adapter emits `BUTTON_PRESSED` and `BUTTON_RELEASED`;
- timer or orchestrator emits `MAX_RECORDING_REACHED`, `TIMEOUT`, and `ERROR_HANDLED`;
- backend client emits or returns the condition that becomes `SEND_SUCCEEDED` or `SEND_FAILED`;
- audio output emits or returns the condition that becomes `PLAYBACK_FINISHED` or `PLAYBACK_FAILED`.

## Transition Table

| Current state | Event | Next state | Action |
| --- | --- | --- | --- |
| `BOOT` | `BOOT_COMPLETE` | `IDLE` | initialize dependencies |
| `IDLE` | `BUTTON_PRESSED` | `RECORDING` | start recording |
| `RECORDING` | `BUTTON_RELEASED` with duration < min | `IDLE` | stop recording and discard audio |
| `RECORDING` | `BUTTON_RELEASED` with duration >= min | `SENDING` | stop recording and start send |
| `RECORDING` | `MAX_RECORDING_REACHED` | `SENDING` | stop recording and start send |
| `SENDING` | `SEND_SUCCEEDED` | `SPEAKING` | start playback |
| `SENDING` | `SEND_FAILED` or `TIMEOUT` | `ERROR` | record error |
| `SPEAKING` | `PLAYBACK_FINISHED` | `IDLE` | clear response |
| `SPEAKING` | `BUTTON_PRESSED` | `RECORDING` | stop playback and start recording |
| `SPEAKING` | `PLAYBACK_FAILED` | `ERROR` | record error |
| `ERROR` | `ERROR_HANDLED` | `IDLE` | clear error |

## Canonical Event Sequences

Happy path:

```text
BOOT
BOOT_COMPLETE -> IDLE
BUTTON_PRESSED -> RECORDING, start_recording
BUTTON_RELEASED -> SENDING, stop_recording, start_send
SEND_SUCCEEDED -> SPEAKING, start_playback
PLAYBACK_FINISHED -> IDLE
```

Short press:

```text
IDLE
BUTTON_PRESSED -> RECORDING, start_recording
BUTTON_RELEASED before MIN_RECORDING_MS -> IDLE, stop_recording, discard_audio
```

Backend error:

```text
SENDING
SEND_FAILED or TIMEOUT -> ERROR, log_error
ERROR_HANDLED -> IDLE
```

Playback interrupt:

```text
SPEAKING
BUTTON_PRESSED -> RECORDING, stop_playback, start_recording
```

The state machine may request side effects, but it should not block on microphone, network, or speaker operations inside transition logic.

## Handled And Ignored Events

| State | Event | Behavior |
| --- | --- | --- |
| `IDLE` | `BUTTON_RELEASED` | ignore and remain `IDLE` |
| `RECORDING` | duplicate `BUTTON_PRESSED` | ignore and remain `RECORDING` |
| `RECORDING` | `BUTTON_RELEASED` after `MAX_RECORDING_REACHED` already moved to `SENDING` | ignore in `SENDING` |
| `SENDING` | `BUTTON_PRESSED` | ignore for MVP and keep waiting for response |
| `SENDING` | `BUTTON_RELEASED` | ignore |
| `SPEAKING` | `BUTTON_RELEASED` without active recording | ignore |
| `ERROR` | button events | ignore until `ERROR_HANDLED` |

For MVP, `ERROR_HANDLED` should be emitted immediately after logging or an optional short error beep. Do not require user action to recover.

## Required Interrupt Behavior

Pressing the button while speaking must immediately stop playback and start a new recording:

```text
SPEAKING -> RECORDING
```

This behavior is a product requirement. The physical button must always feel in control.

## Timing Rules

Initial configurable constants:

```text
MIN_RECORDING_MS=300
MAX_RECORDING_MS=platform-specific
SERVER_TIMEOUT_MS=15000
BUTTON_DEBOUNCE_MS=30
```

Initial platform defaults:

```text
simulator MAX_RECORDING_MS=20000
first firmware hardware MAX_RECORDING_MS=5000
```

Both platforms use the same `MAX_RECORDING_REACHED` rule. Only the configured limit differs.

Behavior:

- stable press starts recording;
- release before `MIN_RECORDING_MS` discards audio and returns to `IDLE`;
- recording automatically stops at `MAX_RECORDING_MS`;
- backend timeout enters `ERROR`, then returns to `IDLE`;
- errors should not trap the device.

## Side Effects

State-machine side effects should be explicit and injectable:

```text
start_recording
stop_recording
send_audio
start_playback
stop_playback
log_transition
handle_error
```

Tests should be able to replace these actions with fakes.

## Simulator and Firmware Alignment

The simulator and firmware can have separate implementations, but they must share:

- state names;
- transition rules;
- timing constants or equivalent defaults;
- error behavior;
- playback interrupt behavior.

When behavior changes, update this document and both test suites.

## Test Scenarios

Required tests:

- boot enters idle;
- normal flow: idle, recording, sending, speaking, idle;
- short press returns to idle without sending;
- max recording sends automatically;
- backend success starts playback;
- backend failure enters error then idle;
- backend timeout enters error then idle;
- playback finished returns to idle;
- button press during speaking stops playback and starts recording;
- invalid event does not corrupt state.

## Error Handling

Use `ERROR` as a short recovery state:

```text
failure -> ERROR -> log/beep -> IDLE
```

The MVP does not need spoken error explanations. A log and optional short beep are enough.

## Out Of Scope

- Wake-word listening state.
- Low-battery state.
- OTA state.
- LTE connecting state.
- Account/authentication state.
- Multi-turn conversation state.
- Background streaming state.
