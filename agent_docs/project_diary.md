# Project Diary

## 2026-09-13 - Model-neutral reboot

- Chose a PCM16 passthrough baseline with no active neural model, CPU fallback, placeholder binary, or automatic model discovery.
- Archived the dirty worktree outside the repository and verified every snapshot by SHA-256 before deleting legacy build output, model packages, services, generators, tests, and runtime backends.
- Kept the dependency direction `app -> services -> drivers -> board -> libs`; vendor libraries and board recovery configuration remain untouched.
- Future DeepFIR and Wave-U-Net packages require explicit package manifests plus host, Vela, board parity, realtime, soak, and capture-to-DAC evidence.
