# AFMS DOP-107CV HMI Generator

This toolchain will generate repeatable Delta DOP-107CV screen layouts from a machine-readable specification.

## Design principle

DOPSoft project files are treated as proprietary template containers. The generator will not invent or directly write an undocumented binary format. Instead it will:

1. Read a known-good DOPSoft project created for the DOP-107CV.
2. Extract or map the editable screen/object representation available in that DOPSoft version.
3. Validate an AFMS JSON layout specification.
4. Apply object coordinates, dimensions, labels, addresses and screen references.
5. Produce a new project copy for opening, compiling and downloading through DOPSoft.

## Screen target

- HMI: Delta DOP-107CV
- Resolution: 800 x 480 pixels
- Coordinate origin: top-left
- X increases to the right
- Y increases downward

## Current input

Use `examples/main_screen.json` as the initial screen manifest. Each object contains:

- `id`
- `type`
- `x`, `y`, `width`, `height`
- object-specific properties such as `text`, `address`, `action` and `target_screen`

## Commands

```bash
python tools/dop107cv_generator/validate_layout.py \
  tools/dop107cv_generator/examples/main_screen.json
```

## Required reverse-engineering sample

Create a minimal DOPSoft project using the same DOPSoft version intended for production. It should contain one screen with:

- one text label
- one momentary button
- one maintained button
- one bit lamp
- one numeric display
- one numeric entry
- one screen-change button

Use obvious coordinates and PLC addresses. Save an unchanged copy, then move or edit exactly one object and save a second copy. Comparing the two files will reveal which project components are safely editable.

Do not use the generated project on production equipment until it opens without repair warnings, compiles successfully in DOPSoft and passes an offline/simulator test.