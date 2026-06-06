# HaoRig AI Retarget Architecture

This module follows Blender/Rigify ideas without copying Blender GPL code.

## Blender-inspired principles

- Bone names are metadata, not the retargeting solution.
- Rest pose, local axes, and bone roll must be represented explicitly.
- Root motion, hips motion, limb IK, and hand/finger roll are separate passes.
- Diagnostics should be generated before solving, so an LLM agent can explain what is risky.

## Pipeline

1. Import target character and source motion.
2. Build a humanoid semantic mapping.
3. Build a retarget profile:
   - skeleton heights and translation scale
   - core segment direction checks
   - hand/finger segment checks
   - local-axis / bone-roll risk checks
   - root/hips translation strategy notes
4. Retarget animation delta into the target skeleton.
5. Apply preview safeguards:
   - in-place horizontal hips motion
   - floor lock
6. Future solver passes:
   - rest-pose offset solver
   - bone-roll/local-axis solver
   - foot lock / foot IK
   - hand and finger pose correctives

## Why hand retargeting still looks hard

The current mapping can correctly pair bones such as:

```text
mixamorig:LeftHandMiddle3 -> J_Bip_L_Middle3
mixamorig:LeftHandThumb2  -> J_Bip_L_Thumb2
```

But the profile often reports high roll risk for hand/finger bones. This means the source
and target bones agree on identity, but not on local rotation axes. A source animation that
means "curl around local X" may need to become "curl around target local Z".

## Current implementation

- `src/rigging/retarget_profile.h/.cpp`
  Builds a Blender-style diagnostic profile.
- `src/tools/rig_batch_test.cpp`
  Emits profile columns and detailed diagnostics.
- `src/app/rig_ai_window.cpp`
  Shows profile summaries in the Rig AI agent panel.

## Next engineering steps

1. Add `RestPoseSolver`:
   Compute per-bone rest delta from source rest pose to target rest pose.
2. Add `BoneRollSolver`:
   Build per-bone correction frames from child segment direction and a stable pole vector.
3. Add `FingerPoseSolver`:
   Treat fingers as chains and solve curl/spread separately.
4. Add `IKPostSolver`:
   Foot lock, knee pole, elbow pole, optional hand lock.
5. Feed `RetargetProfile` JSON to the LLM:
   Let the LLM explain low-confidence mappings and choose solver presets.
