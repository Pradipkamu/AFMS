# DOP-107CV Controlled Property Mapping Protocol

Use the known-good `DOP107CV_Base.dop.dpa` project and DOPSoft V1010.

Each sample must be created from the original base project, not from the
previous sample. Change exactly one property, save under the listed name, close
DOPSoft, and reopen the saved file once to confirm it is valid.

## Required samples

| File | Object | Only change |
|---|---:|---|
| `DOP107CV_R1_Address10.dpa` | 1 | Numeric object register address to holding register 10 |
| `DOP107CV_R1_Address20.dpa` | 1 | Numeric object register address to holding register 20 |
| `DOP107CV_R2_Text_AFMS.dpa` | 2 | Caption/text to `AFMS` |
| `DOP107CV_R2_Text_TEST.dpa` | 2 | Caption/text to `TEST` |
| `DOP107CV_R3_Address1.dpa` | 3 | Command button register address to holding register 1 |
| `DOP107CV_R3_Address2.dpa` | 3 | Command button register address to holding register 2 |
| `DOP107CV_R4_Target1.dpa` | 4 | Screen-change target to screen 1 |
| `DOP107CV_R4_Target2.dpa` | 4 | Screen-change target to screen 2 |

Do not move or resize any object. Do not change colours, fonts, states, labels,
communication settings, or screen properties.

## Analysis commands

```bash
python tools/dop107cv_generator/analyze_property_sample.py \
  DOP107CV_Base.dop.dpa DOP107CV_R1_Address10.dpa \
  --property numeric_address --object-record 1 \
  --output R1_Address10.report.json

python tools/dop107cv_generator/analyze_property_sample.py \
  DOP107CV_Base.dop.dpa DOP107CV_R2_Text_AFMS.dpa \
  --property caption --object-record 2 \
  --output R2_Text_AFMS.report.json
```

Repeat for each sample.

## Acceptance criteria

A property is considered mapped only when:

1. Two different controlled values identify the same payload field.
2. The candidate encoding decodes both known values correctly.
3. A generated third value opens in DOPSoft without a repair warning.
4. DOPSoft displays the generated value correctly after reopening.
5. Saving the generated project in DOPSoft preserves the intended property.

Until these checks pass, the field remains read-only in the AFMS generator.
