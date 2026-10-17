# Schematic notes (basis for the model)

Values are read directly from the drawings; `‖` means in parallel, `A` = logarithmic potentiometer, `B/L` = linear, `RA` = reverse logarithmic. Ported from the analog-recreation project's docs/reference/schematics.md (section 15 there), with the filter section added. Primary sources: [T82242·70 issue 8a in the SSL 4000/6000 individual-cards archive](https://archive.org/details/82-e-12-logic-card), [82E242 Rev. 4](https://archive.org/details/ssl_ssl_82E242), and the [82E242 Rev. 4 engineering change](https://archive.org/details/ssl_ssl_82E242_eco).

## SSL 4000 E channel EQ, card 82E242 ("black knob")

Sources: SSL drawing T82242·70 "Equaliser Filters", three issues (archive.org, "82E242 Equalizer Filters" in the SSL 4000/6000 card collection, and item ssl_ssl_82E242): rev 3 (Nov 85), rev 4 (Jan 86) and issue 8a (June 86, the latest; it adds R100 47k at the input pin and numbers the 220p across the HF frequency pot C79). SSL's engineering change of 15 November 1985, "The 82E242 Rev. 4" (archive.org item ssl_ssl_82E242_eco): the specification, the set-up procedure and the parts that changed from the prototypes. Edge connector list T82002·50 (82E242 → 611G motherboard): which pot ends are CW and ACW. The brown knob 82E02 (drawing T82002·71) for comparison. All ICs NE5534N on ±18 V; `*` = 0.5 % resistor.

| Stage | Values (82E242 issue 8a) |
|---|---|
| Input | pin 48 → C22 220µ → R1 100k to ground (R50 1M to +18 V polarises) → C1 220µ → the EQ input line A; R100 47k pin to ground |
| Section 1, T3 | R10 4k75* from A, R12 4k75* ‖ C8 220p feedback, R13 4k75* from the HMF output, R14 4k75* from the HF output: out = −(A + HMF + HF) |
| Section 2, T8 | the same from T3's output: R38 4k75*, R35 4k75* ‖ C15 220p, R36 (LMF), R37 (LF) |
| Boost / cut (each band) | 25k lin pot: CW end through 1k0* (R2, R11, R25, R39) to the section input, ACW end through 1k0* (R15, R16, R27, R40) to the section output; centre tap 4k7 to ground on the 611 motherboard; wiper 1k8 to ground (R3, R17, R26, R41). CW = boost (the filter in the forward path), ACW = cut (in the feedback) |
| Cross-coupling | each band's output feeds the other band of its section's wiper through 12k: R94 HF → HMF, R95 HMF → HF, R96 LF → LMF, R97 LMF → LF (on the 82E02 the same four are "on back of PCB") |
| HF, T1 | wiper → HF f gang a (50k "Alog", rheostat; C79 220p across it) → R4 3k0 → C3 2n2 → T1 −; feedback R5 3k6 + gang b, C4 220p; C5 2n2 from T1 − to P1; T1 + : R6 5k1 to ground, R7 2k4 + RV1 (SET HF Q, 10k) to P1; P1 → C6 100µ → J112 F1 (R79 100k across it) → T1 out |
| HF, T2 | C7 100µ → R8 3k0 → T2 −; R9 43k feedback ‖ (R81 33k + C2 2µ2 + J112 F2, R82 100k across it) |
| HF switch | pins 1–2 (BELL): +18 V through R90 10k onto the gate line (R83 / R80 10M to the gates, R84 330k to −18 V). Bell: both J112s on: C5 to the output and positive feedback (Deliyannis band-pass), T2 gain 43k ‖ 33k / 3k = 6.2. Shelf: both off: T1 is a high-pass (C3), T2 gain 43k ‖ 133k / 3k = 10.8 |
| HMF, T4 / T5 | wiper → HMF f gang a → R18 3k0 → C9 4n7 → T4 −; R19 3k6 + gang b ‖ C10 4n7 feedback. T4 +: R62 100k to ground and the wiper of Q gang a (10k "log log") between R20 470R (to ground, CW end) and R22 910R + RV2 (SET HMF Q, 500R) to C11 100µ from T4's output (ACW end); R21 1k8 across gang a. T5: R23 750R from C11, R24 750R + Q gang b (rheostat from the ACW end) feedback |
| LMF, T6 / T7 | as HMF with R28 3k0, C12 22n, R29 3k6, C13 22n, R98, R30 470, R31 1k8, R32 910, RV3 500R, C14, R33 / R34 750R |
| LF, T9 | wiper → LF f gang a → C16 33n ‖ C17 47n (shorted by the bell switch's contact 4 on SHELF) → R42 4k3 → T9 −; R43 4k7 + gang b ‖ C18 33n ‖ C19 47n feedback; T9 +: R44 5k1 to ground, R45 2k4 + RV4 (SET LF Q; 5k on the drawing, 10k in the ECO) to P; P → C20 100µ → J112 F4 (R85 100k) → T9 out |
| LF, T10 | C21 100µ → R47 3k0 → T10 −; R48 43k ‖ (R87 7k5 + C51 100µ + J112 F5, R88 100k): bell 2.1, shelf 10.2. Contact 3 (pins 38–39) turns F4 / F5 on with BELL |
| Output | T8 → C23 + C24 (2 × 220µ, R51 / R52 bias) → R54 10R → pin 43, R53 10k to ground |
| HPF input | pin 42 → RV5 500R (UNITY GAIN SET; a rheostat, its wiper tied to its R56 end) → X; R56 300R X to ground; C34 220n X → P11 (C25 N/C across it) |
| HPF, T11 | P11: R79 1M to ground, R55 1k8 → pin 7 → gang c of the 47k A/log triple (rheostat) → ground; T11 a follower (C35 22p compensation) |
| HPF, T12 | T11 out → C36 220n → M → C37 220n → P12 (T12 +). M: R59 1k8 → pin 56 → gang a (rheostat) → pin 54 (after R64: the Sallen–Key feedback). P12: R60 1k8 → pin 55 → gang b (rheostat) → ground. R58 7k5 output to − ; − → RV6 5k (LF FILTER CORNER, rheostat) → R57 5k6 → ground: K = 1 + R58 / (R57 + RV6). C38 10p compensation |
| HPF output | T12 → C39 220µ (R61 100k across it; R63 1M to +18 V polarises) → C40 220µ → R65 10k to ground → R64 10R → pin 54 → S4 (on the pot) → pin 51; S4's other contact is the input line from pin 42. R66 10k pin 51 to ground |
| LPF | pin 51 → C41 220µ (R68 100k to ground, R67 1M to +18 V) → C42 220µ → R70 10k to ground → R69 680R → pin 44 → gang a of the 10k lin dual (rheostat) → pin 45 (N) → gang b (rheostat) → pin 46 → R71 680R → T13 +, C44 3n3 to ground; C43 "68n" (modelled as 6n8) from N to T13's output; T13 a follower (C45 22p compensation) |
| LPF output | T13 → C46 220µ (R72 100k across it; R74 1M to +18 V) → C47 220µ → R76 10k to ground → R75 10R → pin 47 → the switch on the pot; its other contact is pin 51 |

ECO "The 82E242 Rev. 4" (15 Nov 1985), the specification the factory set-up aims at:

| Band | Gain | Frequency | Q |
|---|---|---|---|
| HF | ±15 dB shelf, ±18 dB bell (±1 dB) | 1.5–20 kHz (±10 %) | bell 1.0 (±10 %), down from about 1.7 on the prototypes ('02: about 0.6) |
| HMF | ±15 dB at the lowest Q, ±18 dB at the highest (±1 dB) | 600 Hz – 10 kHz (±10 %) | 0.5–4.5 (±15 %) |
| LMF | as HMF | 160 Hz – 2.2 kHz (±10 %) | 0.5–4.5 (±15 %) |
| LF | ±15 dB shelf, ±18 dB bell (±1 dB) | 40–450 Hz (±10 %) | bell 1.7 (±10 %) |

Changes to bring old cards to Rev. 4: R6, R44 5k1; R7, R45 2k4; R81 33k; R87 7k5; R69, R71 680R; RV1, RV4 10k. Set-up: the Q trimmers for +18 dB of boost with the frequency pots at mid position, the highest Q (mid bands) or bell (HF, LF).

The filter section's two readings: RV5 is a rheostat (its wiper joins its lower end, R56 and C34;
the first transcription had a potentiometer), and C43 is taken as 6n8 against the drawing's "68n"
(Q 0.72, 3.19–49.7 kHz, against Q 2.25 with a 7.3 dB peak and 1.5–23 kHz). The 82E02 (T82002·71)
has the same LPF with 470R legs and C24 6n8 / C25 3n3. Details in
[the filter note](../research/82e242-filters.md).

Brown knob 82E02 differences (T82002·71 rev 6, read from a low-resolution scan): HMF / LMF R18 / R28 4k3, R19 / R29 5k1, LMF caps 15n (so 0.6–7 kHz and 0.2–2.5 kHz, the ranges on the panel legends), HF R4 4k3, R5 5k1, C6 330p, T2 750R / 7k5, a mechanical BELL / SHELF switch instead of J112s, LF R43 5k1, T10 750R / 3k3.

The model (`Source/dsp/SslE242.cpp`, `ssl_e242`): the whole card, with the EQ trimmers set as the ECO says (RV1 1992 Ω, RV2 182.9 Ω, RV3 176.8 Ω, RV4 714 Ω) and the filter trimmers for a maximally flat HPF at unity gain (RV6 1879.6 Ω for K = 2, RV5 288.15 Ω for 0 dB at 1 kHz with the pot at mid position), all from `tools/ssl_eq_ac.cpp`. The 5534s are op-amp devices with a gain of 1500, linear to ±14.85 V and saturating at ±16.5 V; the J112s are 50 Ω switches; the frequency pots' "mid position" for the set-up is taken as 7k5 (15 % of the track, a log law). The LF bell keeps the set-up's +18 dB, which gives Q 2.16 against the ECO's 1.7 (no trim meets both with R87 7k5). The trapezoidal rule's frequency warping is corrected by pre-warping capacitors: C8 / C15 at 20 kHz, each band's frequency-setting capacitors at its knob, the HPF's C34 / C36 / C37 and the LPF's C43 / C44 at their corner. The filter section is its own network ahead of the EQ, joined by an ideal unity buffer (the 611 motherboard's buffering). Details, the four decisions and the response check in [SSL EQ research](../research/ssl-eq.md) and [the filter note](../research/82e242-filters.md).
