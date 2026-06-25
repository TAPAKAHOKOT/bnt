# MVP Milestones

## Build Order

1. Documentation and contracts.
2. State-machine tests.
3. Backend stub.
4. Simulator fixture flow.
5. Firmware fake-hardware contract spike.
6. Simulator live press-to-talk flow.
7. Backend OpenAI integration.
8. Real hardware integration.
9. Breadboard MVP acceptance.

## Milestone 1: Documentation And Contracts

Goal:

Define what will be built and what will not be built.

Deliverables:

- MVP plan docs.
- State-machine plan.
- Backend API contract.
- Simulator and firmware architecture plans.
- Testing plan.

Definition of done:

- Required docs exist under `docs/`.
- MVP scope is explicit.
- Out-of-scope list is explicit.
- First endpoint and state transitions are documented.

## Milestone 2: State Machine Tests

Goal:

Make product logic testable before audio, network, or hardware work.

Deliverables:

- State-machine implementation in simulator or shared test harness.
- Unit tests for all required transitions.
- Fake action hooks for recording, sending, playback, and errors.

Definition of done:

- Normal flow reaches `IDLE`.
- Short press is ignored.
- Backend failure recovers to `IDLE`.
- Playback interrupt enters `RECORDING`.
- Tests run without hardware.

## Milestone 3: Backend Stub

Goal:

Provide a stable local API for simulator and firmware development.

Deliverables:

- Local backend.
- `POST /ask-audio`.
- Static WAV response.
- Backend tests.

Definition of done:

- Endpoint accepts fixture WAV.
- Endpoint returns WAV that parses as mono, 16 kHz, 16-bit PCM and has non-empty PCM frames.
- Invalid and empty audio return controlled errors.
- No OpenAI key is required for stub mode.

## Milestone 4: Simulator Fixture Flow

Goal:

Prove end-to-end request/response without live audio devices.

Deliverables:

- Simulator fixture mode.
- Backend client.
- Fake audio output.
- Integration test using fixture WAV.

Definition of done:

- Fixture audio is sent to backend.
- Response audio reaches fake output.
- State machine returns to `IDLE`.
- Test can run without keyboard, microphone, speaker, or ESP32.

## Milestone 5: Firmware Fake-Hardware Contract Spike

Goal:

Validate firmware architecture and payload assumptions before live audio and OpenAI integration.

Deliverables:

- PlatformIO firmware project.
- Firmware state machine.
- Fake button, audio input, audio output, and network client.
- Fake payload limit check for MVP WAV shape.

Definition of done:

- Firmware compiles in fake-hardware mode.
- Fake flow logs required transitions.
- Firmware state-machine tests pass.
- Fake network can represent the agreed request/response WAV contract.
- No OpenAI key exists in firmware.

## Milestone 6: Simulator Live Press-To-Talk

Goal:

Validate the actual desktop interaction.

Deliverables:

- Spacebar button input.
- Laptop microphone recording.
- Laptop speaker playback.
- Manual UX checklist.

Definition of done:

- Hold Space starts recording.
- Release Space sends audio.
- Backend response plays through laptop speakers.
- Pressing Space during playback interrupts and starts a new recording.
- The loop works three times in a row without a stuck state.

## Milestone 7: Backend OpenAI Integration

Goal:

Replace stub response with AI response while preserving the device API.

Deliverables:

- OpenAI service behind backend interface.
- Environment-based API key config.
- Fake OpenAI service for tests.
- Timeout/error handling.

Definition of done:

- `OPENAI_API_KEY` is used only by backend.
- Simulator receives AI response audio that parses as MVP-format WAV, has non-empty PCM frames, and starts playback without error.
- Backend AI response audio stays within the MVP 30-second / ~1 MB response cap.
- OpenAI failures return controlled backend errors.
- Automated tests do not require real OpenAI.

## Milestone 8: Real Hardware Integration

Goal:

Replace fake firmware modules with real breadboard components one at a time.

Deliverables:

- Real GPIO button.
- Real Wi-Fi backend client.
- INMP441 recording.
- MAX98357 playback.
- Hardware notes.

Definition of done:

- Button press/release logs are stable.
- Firmware can call local backend over Wi-Fi.
- Microphone captures MVP-format WAV within the first hardware payload limit.
- Speaker plays known MVP-format WAV response.
- Each component can be tested independently.

## Milestone 9: Breadboard MVP Acceptance

Goal:

Demonstrate the complete bnt MVP on physical hardware.

Deliverables:

- Breadboard ESP32 prototype.
- Local backend.
- Full press-to-talk loop.
- Acceptance notes with latency and known issues.

Definition of done:

- Press and hold records voice.
- Release sends audio to backend.
- Backend returns AI response.
- Speaker plays response.
- Press during playback interrupts response and records a new request.
- Errors return to `IDLE`.
- The loop completes three consecutive hardware runs without a stuck state.
- Response audio is valid MVP-format WAV.
- Latency is logged for recording, backend request, and playback start.
- Interrupt behavior is verified at least once during playback.

## Explicitly Out Of Scope For All MVP Milestones

- LTE.
- Battery.
- Charging.
- Final PCB.
- Final enclosure.
- OTA.
- Mobile app.
- Account system.
- Screen.
- Wake word.
- Local LLM.
- Advanced memory.
- Fleet management.
