# OBS Sport Eyes v1.10.0d – CSV Logging

Adds one visible checkbox: **Abilita log CSV**.

When enabled, two file destinations become visible:

1. **CSV Diagnostica unificata** (`obs_sport_eyes_diagnostics.csv` by default)
   - writes tracking state, visible objects, selected action box, crop box, Safe ROI state, and Director AI application state.
2. **CSV Director AI** (`obs_sport_eyes_director_ai.csv` by default)
   - writes the existing Director AI telemetry (prediction, velocity, confidence, transition probability, court zone, crop plan).

Sampling is fixed at 10 Hz (100 ms) to avoid excessive file growth. Both files are opened on first usable sample, overwritten at the beginning of each enabled logging session, and flushed after each sample so they remain usable if OBS is stopped unexpectedly.

The log implementation is kept in `src/diagnostics/SportEyesCsvLogger.cpp` and does not add logging logic back into the OBS callback orchestration file.
