# R2 replay reference fixture

verify_raw_replay.py is an unchanged copy of the supplied firmware package verifier.
Its SHA-256 is checked against models/deepfir/source_integration_manifest.json
before extracting four portable functions. Unit tests use synthetic traces and
make no controller parity claim. Full source-package/hash/golden audits and actual
replay still require the separately retained, Git-ignored training package.
