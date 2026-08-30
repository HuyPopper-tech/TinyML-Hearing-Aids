# DeepFilterNet U55 firmware contract

1. Reset every state input to zero at boot or stream discontinuity.
2. Quantize feature/state using `deepfilternet_u55_contract.h`; invoke Vela model; follow `tensor_contract.json.state_mapping`: raw copy is valid only when `copy_mode` is `raw_int8`, otherwise requantize next state.
3. Dequantize `gain_bins`, `df_coefficients`, `alpha` with their own scale/zero-point. Never treat INT8 gain as Q7 by assumption.
4. M55 DSP: apply 257 bin gains, retain five low-band spectra, execute causal complex 5-tap MAC to bin 143, alpha blend, IFFT/WOLA.
5. Build MVE path when `__ARM_FEATURE_MVE` is set; retain scalar reference only for parity. Log fft_us/npu_us/df_us/wola_us/e2e_us.
6. Run golden vectors before audio release. `board_mve_parity_pending` is not release-ready.
7. `deepfilternet_u55_postprocess.c` là reference chính xác cho gain/DF/alpha; thay inner loop bằng MVE sau khi golden parity pass.
8. Compile exactly one model-data source: `deepfilternet_u55_vela_model_data.c`; include its `.h` everywhere else. Inspect the final linker map: `.text + .rodata + model + DSP constants` must stay at or below 1.8 MiB.
9. Fill `board_gate.json` from `board_gate_template.json` after target run. Release needs `mve_scalar_max_lsb_q15 <= 1` and `e2e_us <= 16000`.
