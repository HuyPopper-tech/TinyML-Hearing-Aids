# Debug initialization and first-hop stage logging

Scope: inference service, audio pipeline, existing board progress callback, their native host checks and the Debug operation guide. Keep model artifacts, DSP arithmetic, pins/clocks/DMA settings and Release UART policy unchanged.

Acceptance:
- Debug emits flushed BEGIN/OK/FAIL checkpoints before audio starts, including transport report, tensor actual/expected metadata, FFT and separate feature/FIR MVE self-tests.
- The first capture/frontend/invoke/reconstruction path has bounded nonblocking markers; runtime recovery logs each fault category once rather than flooding UART.
- No blocking UART drain after stream start or on automatic restart. Initialization failure remains visible before main enters its safe-stop loop.
- Native host checks cover init/start failure and runtime nonblocking logging; all regression tests and Debug/Release builds pass. Release image contains no diagnostic trace strings.
- No flash or new board correctness claim; provide the Debug binary and log interpretation for the user to run.
