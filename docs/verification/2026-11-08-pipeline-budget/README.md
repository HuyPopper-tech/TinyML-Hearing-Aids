# Pipeline budget investigation — 2026-11-08

Analysis of firmware revision `6bf6b37`; no product source/configuration/image or
source package was modified. Full reasoning and next-step acceptance are in
[the analysis](../../pipeline_submillisecond_analysis.md).

Native GCC 13.2 compiled and executed [feedback_q31_proof.c](feedback_q31_proof.c)
with `-std=c11 -O2 -Wall -Wextra -Werror`. The reference function was extracted
unchanged from the current adapter; the candidate emulates nonnegative-lane
rounded Q31 multiplication. Both contract multipliers/shifts are tested over
all 65536 INT16 codes. [Command log](command_1.log): 131072 cases, zero mismatches,
max code error zero. Source/contract/function hashes and exit codes are in
[proof.json](proof.json). The C file is an evidence extraction, not runtime code.

Reproduce with `rtk proxy gcc -std=c11 -O2 -Wall -Wextra -Werror` and this C file,
choosing an ignored output path, then execute the resulting EXE through
`rtk proxy`. On this host, use the existing isolated compiler wrapper in
`diagnostics/i2c_stuck_trace_20261108_162019/host_compiler_runtime/`; do not modify
global PATH. The recorded command contains the actual timestamped EXE path.

[fifo_model.json](fifo_model.json) records a deliberately simplified FIFO/TD
model, assumptions and limits. Its generating script is retained alongside the
raw run in `diagnostics/pipeline_budget_20261108/`. Exact PDMA request timing and
target FIFO phase remain unverified. These values are not board measurements.

Raw timestamped run: `diagnostics/pipeline_budget_20261108/221230/`, including
the EXE and current feedback Thumb disassembly. Native arithmetic is PASS;
hardware MVE execution, optimization timing and physical audio are NOT_RUN.
No target build was needed for analysis-only changes; previous build evidence
remains at [first-hop-deadline](../2026-11-08-first-hop-deadline/README.md).

Fresh full host regression: **128 tests PASS, no skips**; see [tests.log](tests.log)
and [verification.json](verification.json). Current adapter/contract hashes and
local Markdown links were checked. This does not prove target timing or MVE
execution of the candidate formula.
