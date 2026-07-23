# Phase 2 — DOP-107CV Project Generation

## Current status

Phase 2 has started on the `feature/dop107cv-hmi-generator` branch.

The supplied DOPSoft V1010 sample archives establish this container structure:

1. A 164 × 98, 24-bit BMP preview.
2. A gzip member beginning at byte offset 48,290.
3. A decompressed editable payload beginning with `Delta-HMI Screen Editor DOP V1010`.

The base sample payload is 99,821 bytes. The modified sample payload is 99,822 bytes.

## Implemented

- Lossless structural DPA inspection.
- Preview and payload extraction.
- Deterministic gzip repacking.
- Structural round-trip verification.
- Unit tests for archive validation and round trips.

## Important finding

A normal DOPSoft save after moving an object rewrites a large encoded section of the editable payload. Therefore, changing guessed coordinate bytes directly would risk producing a project that DOPSoft cannot safely open.

## Next mapping dataset

Create controlled samples where exactly one property changes at a time:

- X: 100 → 110
- Y: 100 → 110
- Width: 100 → 110
- Height: 50 → 60
- PLC address only
- Caption text only

Each sample should start from the same base project and be saved separately. These samples will be used to identify record boundaries, checksums, string tables, and coordinate encoding before enabling generated project writes.
