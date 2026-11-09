# Codec I2C initialization diagnosis and transaction completion

The supplied Debug boot has board_status=0 and codec_status=5. GPIO bus clear
completed; this does not prove that the codec acknowledged subsequent writes.
The current log cannot distinguish NACK, missing SI, or incomplete STOP.

## Scope and ownership

- `drivers/tlv320dac3100/`: foreground-only bounded transaction, STOP completion,
  failure snapshot before abort; driver context owns the snapshot until next init.
- `drivers/audio_transport/`: copy exhausted-operation diagnostics into the
  caller-owned Debug init report without exposing vendor types to services.
- `services/audio_pipeline/`: print the report during stopped boot only.
- `tests/`: execute production transaction functions against register-model
  ACK/NACK/timeout/STOP transitions and verify the Debug failure output.
- Documentation/evidence: preserve the supplied failure and exact verification.

No pins, clocks, DMA, codec gain, settle delays, DSP, model or vendor changes.
PA4 SDA / PA5 SCL remain unchanged. No flash/reset operations; the probe lacks
nRESET, so physical power cycling remains the available recovery procedure.

## Acceptance

1. A new START cannot overlap a pending STOP; waits terminate on a fixed bound.
2. ACK success requires completed STOP. NACK and SI timeout preserve the failing
   status/control/byte step before abort, including a separate STOP-timeout flag.
3. Existing retry budgets remain bounded; an exhausted operation reports its
   requested page/register, phase and attempt count through ordinary Debug UART.
4. Native regression tests fail on the original implementation and pass after
   the change. Run the full unittest suite, diff check and Debug/Release/Probe
   builds. No UART added to ordinary Release and no logging in ISR/audio polling.
5. Hardware recurrence remains NOT_RUN until the revised image is exercised on
   the user's board. Do not equate a simulated STOP race with the physical cause.
