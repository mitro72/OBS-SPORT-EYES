# OBS-SPORT-EYES Director AI v2 Alpha

This branch introduces the first modular Director AI architecture for OBS-SPORT-EYES.

The goal is to evolve the system from a reactive detector-driven crop into a temporal, prediction-based virtual camera director.

## Alpha 1 - Temporal Director

Alpha 1 adds `TemporalDirector`.

Responsibilities:

- keep a short temporal memory of the action bbox;
- estimate action center velocity;
- estimate acceleration;
- predict the action center several milliseconds ahead;
- degrade confidence when the target disappears.

No heavy model is introduced in this phase.

## Alpha 2 - World State + Camera Planner

Alpha 2 adds:

- `WorldState`
- `CameraPlanner`

`WorldState` converts raw prediction data into sport-oriented state:

- action cluster;
- direction;
- transition probability;
- confidence.

`CameraPlanner` converts the predicted action center into a 180-degree pan-only crop plan.

## Alpha 3 - Court Awareness Foundation

Alpha 3 adds:

- `CourtModel`
- `DirectorAIEngine`

`CourtModel` is intentionally lightweight in this alpha. It maps frame pixels into normalized court coordinates and classifies horizontal court zones.

This prepares the codebase for a future real court detector without coupling it to OBS crop logic.

`DirectorAIEngine` is the single facade that should be called by the existing OBS filter code.

Expected integration flow:

```text
Group bbox
  -> DirectorAIEngine.update()
  -> TemporalDirector prediction
  -> WorldState transition analysis
  -> CourtModel normalized zone
  -> CameraPlanner crop plan
  -> OBS crop/pad filter
```

## Alpha 4 - Safety Guardrails

Alpha 4 adds rollout protections around the prediction path:

- `max_prediction_lead_px`: caps how far the predicted center may move from the current cluster center;
- `min_confidence_to_apply`: prevents low-confidence predictions from driving the crop;
- `DirectorAIDiagnostics`: stores current center, prediction lead, clamp status, and confidence gate result.

Default rollout remains conservative:

```text
directorAIEnabled = false
max_prediction_lead_px = 480
min_confidence_to_apply = 0.05
```

## Next integration step

The wiring into `Sport Eyes filter facade / pipeline modules` is present, but legacy behavior remains default unless `directorAIEnabled` is enabled.

Recommended safe rollout:

1. compile with `directorAIEnabled = false`;
2. enable Director AI only in local debug;
3. verify that prediction lead and clamping behave as expected;
4. compare legacy center vs predicted center in diagnostics;
5. enable predicted center for group auto mode only after visual validation.

## Current design principle

Do not add new heavy inference models yet.

The first target is to reduce perceived delay in fast transitions using temporal prediction and camera planning.
