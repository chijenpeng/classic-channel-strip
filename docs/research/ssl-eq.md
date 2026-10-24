# SSL 4000 E "black knob" channel EQ (82E242): model and response check

The plug-in models the EQ card of the SSL 4000 E console, 82E242 Rev. 4, from its schematic: a
circuit netlist (`Source/dsp/SslE242.cpp`, `SslE242Card.h`, module id `ssl_e242`) whose discrete-time
state space the plug-in runs (see "The plug-in's engine" below). The four bands were first
modelled in the analog-recreation project (its `ssl_e242` block, issue #23 there), and this note
was written there; the plug-in carries the bands over unchanged (same netlist, laws, trimmers,
pre-warping and op-amp model, as of v0.1.0, where the EQ section rendered bit for bit like
analog-recreation's module) and adds the card's HPF / LPF section, which has its own note,
[82e242-filters.md](82e242-filters.md). This note covers the sources, how the controls map onto
the card's pots, the level, and how the model's responses compare with the schematic and with
SSL's own specification. Component values are in the [schematic reference](../reference/schematic.md).

## Sources

- **SSL drawing T82242·70 "Equaliser Filters"**, three issues: rev 3 (Nov 1985), rev 4 (Jan 1986)
  and issue 8a (June 1986), in the [SSL 4000/6000 Individual Cards archive](https://archive.org/details/82-e-12-logic-card)
  and [archive item ssl_ssl_82E242](https://archive.org/details/ssl_ssl_82E242). The model follows issue 8a.
- **SSL engineering change "The 82E242 Rev. 4"**, 15 November 1985 ([archive item ssl_ssl_82E242_eco](https://archive.org/details/ssl_ssl_82E242_eco)): SSL's specification for the card (gain, frequency range and Q of each
  band, with tolerances), the factory set-up of the Q trimmers, and the parts that changed from
  the prototypes. This is the published hardware reference the response check uses.
- **Edge connector list T82002·50** (82E242 to the 611G motherboard): which end of each pot is CW,
  so which way the controls turn.
- **82E02 brown knob drawing T82002·71**, for comparison, and SSL's E-series 500 module user guide
  (the 242 has "±18 dB instead of ±15 dB" and "a different control law for tighter control of low
  frequencies"; the shelves are 6 dB/oct).

No published response plots or measurements of a 242 card turned up. The checks below are
against SSL's Rev. 4 specification and the schematic itself (an AC analysis of the same netlist,
`tools/ssl_eq_ac.cpp`).

## The circuit in short

Two inverting summing amplifiers in series (T3 for HF + HMF, T8 for LMF + LF), so the card is
non-inverting overall. Each band has a 25k linear boost / cut pot from its section's input (CW,
through 1k0) to its output (ACW, through 1k0), centre-tapped to ground through 4k7, with 1k8 from
the wiper to ground. The wiper drives the band's filter, whose output goes back into the summing
node through 4k75: turned CW the filter adds to the signal (boost), turned ACW it sits in the
feedback (cut). That is why a bell's -3 dB bandwidth narrows as it is boosted, and why boost and
cut mirror each other.

- **HMF / LMF**: a multiple-feedback band-pass (T4 / T6, series R-C in, R ‖ C feedback) whose Q is
  raised by positive feedback into its + input (Deliyannis). The first gang of the Q pot sets
  that feedback; the second raises the gain of the following inverting stage (T5 / T7) as the Q
  falls, which keeps most of the range at low Q (±15.5 dB against ±18 at the highest Q).
- **HF / LF**: the same band-pass (T1 / T9) and a gain stage (T2 / T10). Two J112s per band switch
  the positive feedback in and the gain stage down for the bell; with them off T1 is a high-pass
  (C3 in series) and T9, its input capacitors shorted by the bell switch, a low-pass, so the band
  is a 6 dB/oct shelf.
- Each band's output also reaches the other band of its section through 12k into the wiper
  (R94–R97). The brown knob card has the same four resistors on the back of its PCB. At flat they
  do nothing; with one band boosted they let it lean on its neighbour, and the neighbour's
  frequency and Q move a centre by up to a few percent (HF at 1.5 kHz: 1500 Hz with HMF at 3 kHz
  and the highest Q, 1532 Hz with HMF at 2.5 kHz and Q 1.5, 1450 Hz without the resistors).

## Controls

The plug-in's controls, in the host's order: Input (−20..+20 dB), HPF in / frequency, LPF in /
frequency, then the bands in console order (HF gain / freq / shape, HMF gain / freq / Q, LMF gain /
freq / Q, LF gain / freq / shape), EQ in, Output (−20..+20 dB) and Oversampling (1x / 2x). Input
and Output are plain gains before and after the card; the filters' controls are described in the
[filter note](82e242-filters.md). The bands':

| Control | Unit, range | Maps to |
|---|---|---|
| gain | dB, -18 .. +18 | the boost / cut pot's travel, through the band's own law from the circuit (dB at the centre of a full-Q / bell boost against travel, 41 points). 0 dB is the centre detent exactly |
| freq | Hz, log knob: HF 1.3–19 kHz, HMF 640 Hz–9.4 kHz, LMF 140 Hz–2 kHz, LF 37–420 Hz | both gangs of the frequency pot (rheostats of 0..50k), through the circuit's law: the peak of a full boost against the rheostat (21 points), the same at every rate. The band's capacitors are pre-warped at the knob (decision 2 below) |
| Q | log knob, 0.5 .. 4.1 (HMF) / 4.2 (LMF) | the Q pot's travel, through the circuit's law: the -3 dB bandwidth of a full boost, SSL's own figure (the ECO's 0.5–4.5) |
| shape | Shelf / Bell | the J112s (and for LF the bell switch's contact that shorts the input capacitors) |

The ranges are the circuit's own, end to end of the pots; SSL specifies 1.5–20 kHz, 600 Hz–10 kHz,
160 Hz–2.2 kHz and 40–450 Hz, all ±10 %. The panel legends (1.5–16 kHz, 0.6–7 kHz, 0.2–2.5 kHz,
30–450 Hz) are the brown knob card's ranges; the 242 kept the panels but moved the mid bands.

**Laws that differ from the console's.** The knobs are in the console's units but not its
rotation:
- Frequency: the console's frequency pots are 50k "Alog" (anti-log) rheostats, so its dial is
  S-shaped (on SSL's E-series 500 panel: 1.5, 2, 5, 10, 14, 16 kHz at every other dot, about 7 kHz
  at twelve o'clock). Here the knob is logarithmic in Hz.
- Gain: the console's pot is linear; in dB its law is flat in the middle and steep at the ends
  (HF: about ±6.8 dB a quarter turn either side of the detent, ±11.6 dB at 90 % of the travel, the
  last 2.2 dB in the final 2.5 %; the other bands within 0.7 dB of that). Here the knob is linear
  in dB.
- Q: the console's pot is a 10k log-log pair; at mid rotation it gives a Q of about 1.2. Here the
  knob is logarithmic in Q.
- The dB and Q scales are exact at the set-up reference (frequency pot at mid travel, the highest
  Q or bell). Elsewhere the circuit shows through, as on the console: the same knob position gives
  ±15.5 dB at the lowest Q and on the HF shelf, ±16.5 dB on the LF shelf, the centre moves a few
  percent with the neighbouring band, and the frequency pot's end changes the peak gain by a few
  tenths of a dB. This is kept on purpose (decision 4 below).
- Shelves use the same pot position as the bell at that knob frequency. Measured on the schematic
  at four settings: the HF shelf is 1–2 dB short of its full boost or cut at the knob frequency and
  half way (in dB) 1.6–1.8 octaves below it; the LF shelf is about 2.6 dB short at the knob
  frequency and half way about 1.5 octaves above it.

## Level

The plug-in maps 0 dBFS to +18 dBu (the EBU R68 alignment, -18 dBFS = 0 dBu): a full-scale sine is
8.70 V peak at the card, and 0 VU (+4 dBu) sits at -14 dBFS (after the Input trim, before the
Output trim). The 5534s on ±18 V are modelled linear to ±14.85 V and saturating at ±16.5 V. The
internal nodes never swing more than the output does (headroom table below), so:

- flat, the card is exactly linear to +4.6 dBFS (a 1 kHz sine at +4 dBFS: harmonics at -147 dB)
  and clips softly above; +12 dBFS in comes out at +5.3 dBFS peak;
- a band boosted by b dB starts to clip where that band's content is above (4.6 - b) dBFS, as on a
  console aligned that way: an 18 dB boost on full-scale material distorts on the hardware too;
- the filter section has no gain anywhere (its op-amps see at most the input, +0.15 dB), so with
  the filters in the card still clips at +4.6 dBFS out (`classic_channel_test`: +4 dBFS through the HPF at
  400 Hz and the LPF at 3 kHz is as linear as -20 dBFS, harmonics under -150 dB).

`classic_channel_measure 48000 --sine 1000 4`: +3.95 dBFS out, THD -153 dB; `--sine 1000 12`: +5.23 dBFS out,
THD -14 dB.

Largest gain from the input to each op-amp output, 10 Hz–40 kHz (dB, schematic):

| Setting | T3 out | HF filter (T1) | HF out (T2) | HMF out (T5) | T8 out | LMF out | LF filter (T9) | LF out | card out |
|---|---|---|---|---|---|---|---|---|---|
| flat | 0.0 | -40.6 | -19.9 | -40.5 | 0.0 | -52.7 | -79.1 | -58.9 | 0.0 |
| HF +18 bell | +18.0 | +1.0 | +16.8 | -12.3 | +18.0 | -43.2 | -64.0 | -43.9 | +18.0 |
| HMF +18 highest Q | +17.9 | -32.5 | -11.9 | +16.5 | +17.8 | -43.1 | -64.1 | -44.0 | +17.8 |
| LF +18 bell | 0.0 | -40.6 | -19.9 | -40.5 | +18.0 | -26.0 | +10.3 | +16.8 | +18.0 |
| all full boost | +21.2 | -6.7 | +13.9 | +17.7 | +23.0 | +20.5 | -5.1 | +15.1 | +23.0 |

## The model against SSL's specification

SSL's Rev. 4 figures against the schematic (AC analysis, trimmers set as the ECO says) at both
ends of each frequency pot and at mid travel, and against the module (time domain at 48 and
96 kHz; measured in analog-recreation with `fx_test --ssl`, now `classic_channel_test` here, which gives the
same figures, at 44.1 kHz too):

| | SSL Rev. 4 | Schematic | Module |
|---|---|---|---|
| HF frequency | 1.5–20 kHz ±10 % | 1.31–19.1 kHz | same, centres within 2.2 % of the knob |
| HF bell gain / Q | ±18 ±1 dB, Q 1.0 ±10 % | +17.7..+18.1 / -17.7..-18.3 dB, Q 0.85–0.97 | +17.6..+18.4 dB, Q 0.93 at 4 kHz (96 kHz) |
| HF shelf | ±15 ±1 dB | +15.4 / -15.6 dB plateau at the mid setting (+14.4 at the 1.3 kHz end) | +15.2 / -15.4 dB at 20 kHz (96 kHz, 6 kHz setting) |
| HMF frequency | 600 Hz–10 kHz ±10 % | 639 Hz–9.42 kHz | same at every rate, 44.1 kHz included |
| HMF gain | ±18 (highest Q), ±15 (lowest) ±1 dB | ±17.6..18.4, +15.4..+16.0 | within 0.4 dB of ±18; +15.4..+15.6 at Q 0.5 |
| HMF / LMF Q | 0.5–4.5 ±15 % | 0.49–4.24 (HMF), 0.48–4.52 (LMF) | Q 0.5, 1, 2, 4 within 0.9 % of the knob |
| LMF frequency | 160 Hz–2.2 kHz ±10 % | 142 Hz–2.02 kHz | same |
| LF frequency | 40–450 Hz ±10 % | 36.7–418 Hz | same |
| LF bell gain / Q | ±18 ±1 dB, Q 1.7 ±10 % | +17.7..+18.2 dB, Q 2.04–2.23 | same |
| LF shelf | ±15 ±1 dB | +15.6..+16.5 dB | +16.4 / -16.5 dB at 20 Hz |
| flat | — | -0.04 dB (1 kHz), -0.10 dB (20 kHz) | -0.046 dB at 1 kHz, within 0.06 dB from 20 Hz to 10 kHz; -0.02 / -0.05 / -0.10 dB at 20 kHz at 44.1 / 48 / 96 kHz |

Everything is inside SSL's tolerances except two figures, both straight from the drawing's
values:
- **LF bell Q**: 2.0–2.2 against SSL's 1.7. With the set-up the ECO prescribes (+18 dB with the
  bell in), the LF gain stage's bell gain of 2.1 (R87 7k5) leaves the Q trimmer to supply the rest
  through positive feedback; trimmed instead for Q 1.7, the bell would give +16 dB. The model keeps
  the ECO's set-up (decision 3 below).
- **The low ends**: HF reaches 1.31 kHz and LMF 142 Hz, 13 % and 11 % under SSL's 1.5 kHz and
  160 Hz (the drawing's 2n2 / 22n with 3k0 + 3k6 and the 50k pots give these); the high ends are
  4–8 % under.

## The model against the schematic

The time-domain module (impulse responses) against the AC analysis of the same netlist, at the pot
positions the knobs stand for, 20 Hz–20 kHz (measured in analog-recreation; the EQ section here
is the same netlist and renders bit for bit the same). Twenty-eight settings: flat, each band boosted and
cut at the reference, the shelves, the lowest Q, partial boosts, both ends of the frequency pots,
HF bells up to 19 kHz and HMF at 9.4 kHz, all four bands at once; the other bands where the laws'
reference has them. Worst difference in dB, module minus schematic, and in brackets the worst up
to 10 kHz:

| Setting | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| flat | +0.07 (0.03) | +0.05 (0.03) | -0.01 (0.01) | -0.01 (0.01) |
| HF bell +18 at 1.3 kHz | -0.85 (0.47) | -0.57 (0.40) | -0.14 (0.11) | -0.04 (0.04) |
| HF bell +18 at 6.2 kHz | -6.3 (1.1) | -5.2 (0.9) | -1.05 (0.22) | -0.26 (0.07) |
| HF bell +18 at 12.3 kHz | -9.8 (2.4) | -6.8 (2.0) | -0.95 (0.45) | -0.23 (0.12) |
| HF bell +18 at 16 kHz | -7.7 (4.7) | -4.6 (3.7) | -0.75 (0.75) | -0.19 (0.19) |
| HF bell +18 at 19 kHz | -8.3 (8.3) | -6.2 (6.2) | -1.08 (1.08) | -0.26 (0.26) |
| HF shelf +15 at 6.2 kHz | -1.4 (0.20) | -0.81 (0.17) | -0.10 (0.03) | -0.03 (0.02) |
| HF shelf +15 at 12.3 kHz | +0.85 (0.66) | +0.73 (0.55) | +0.18 (0.12) | +0.03 (0.02) |
| HMF +18, Q 4.1, 3.1 kHz | -0.43 (0.43) | -0.36 (0.36) | -0.10 (0.10) | -0.03 (0.03) |
| HMF +15, Q 0.5, 3.1 kHz | -5.6 (1.2) | -4.7 (1.0) | -0.97 (0.23) | -0.24 (0.07) |
| HMF +18, Q 4.1, 9.4 kHz | -3.7 (1.6) | -3.2 (1.35) | -0.86 (0.33) | -0.23 (0.09) |
| LMF and LF, every setting | 0.20 (0.20) | 0.17 (0.17) | 0.06 (0.06) | 0.05 (0.05) |
| all four +18 | -6.7 (1.1) | -5.4 (0.9) | -1.08 (0.22) | -0.27 (0.07) |

The differences are the trapezoidal rule's frequency warping, corrected where one step of the
rule can correct it (decisions 1 and 2 below): every bell peaks on its knob with the schematic's
gain at every rate, over the pots' whole range, and the flat response is the schematic's within
0.08 dB to 20 kHz. What is left at 1x is the shape of bells centred high up, which needs 2x
(decision 2).

A full HF bell at 6.2 kHz (dB):

| Hz | Schematic | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| 508 | +1.6 | +1.4 | +1.4 | +1.5 | +1.6 |
| 2045 | +9.5 | +9.0 | +9.1 | +9.4 | +9.5 |
| 4964 | +17.3 | +17.1 | +17.2 | +17.3 | +17.3 |
| 6396 | +18.0 | +17.9 | +17.9 | +18.0 | +18.0 |
| 9352 | +16.0 | +15.1 | +15.3 | +15.8 | +15.9 |
| 13677 | +12.7 | +9.8 | +10.4 | +12.2 | +12.5 |
| 20000 | +9.5 | +3.2 | +4.3 | +8.5 | +9.3 |

The widest HMF bell at 3.07 kHz (Q 0.5, full boost):

| Hz | Schematic | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|---|
| 209 | +2.0 | +1.9 | +1.9 | +2.0 | +2.0 |
| 956 | +10.7 | +10.5 | +10.6 | +10.6 | +10.6 |
| 3395 | +15.5 | +15.6 | +15.6 | +15.5 | +15.5 |
| 9352 | +11.3 | +10.3 | +10.5 | +11.1 | +11.3 |
| 13677 | +8.7 | +6.1 | +6.6 | +8.3 | +8.6 |
| 20000 | +6.2 | +0.6 | +1.4 | +5.2 | +5.9 |

The full LF shelf at the 162 Hz setting, same at every rate (dB): +16.5 at 19 Hz, +15.4 at 98 Hz,
+12.8 at 209 Hz, +7.7 at 508 Hz, +3.7 at 956 Hz, +1.1 at 2 kHz.

## Decisions on the four open questions (#23)

The first version left four questions (analog-recreation issue #23's closing comment). Each is
decided below from SSL's documents, the drawing and the AC analysis; no plug-in was consulted.
"Before" is analog-recreation commit 36a993a. The module-against-schematic numbers are impulse responses of the module against
`classic_channel_eq_ac` at the knobs' pot positions, as in the table above.

### 1. The summing amps' 220 pF at 1x: pre-warped at 20 kHz, kept in the circuit

C8 and C15 (220 pF across the summing amps' 4k75 feedback) put a pole at 152 kHz. The trapezoidal
rule maps an analog frequency f to fs / π · atan (π f / fs), so at 44.1 / 48 kHz those poles
land at 20.8 / 22.5 kHz and pull the flat response down: -1.6 / -0.58 dB at 20 kHz against the
schematic's -0.10.

Leaving the caps out below 88.2 kHz would have fixed the magnitude (-0.045 dB at 20 kHz) but
dropped part of the drawing and its phase (the schematic is at -12° at 20 kHz when flat, mostly
these two poles), and made the netlist depend on the rate. Instead each cap is pre-warped: its
trapezoidal companion conductance 2C / T is scaled by x / tan x, x = π · 20 kHz / fs, which makes
its admittance exactly the analog one at 20 kHz (the bilinear transform pre-warped at 20 kHz, for
that element alone). Below 20 kHz the caps are then slightly weaker than the analog ones; the
error is a fraction of their own effect there, under 0.06 dB.

| Flat, at 20 kHz | 44.1 kHz | 48 kHz | 96 kHz | 192 kHz |
|---|---|---|---|---|
| schematic | -0.10 | -0.10 | -0.10 | -0.10 |
| before | -1.60 | -0.58 | -0.13 | -0.11 |
| after | -0.02 | -0.05 | -0.10 | -0.11 |

(`fx_test --ssl` in analog-recreation, `classic_channel_test` here, the module's default settings; 192 kHz from
the comparison above.)

Worst flat difference from the schematic over 20 Hz–20 kHz: -1.40 / -0.47 / -0.03 / -0.01 dB
before, +0.07 / +0.05 / -0.01 / -0.01 dB after (the rest is the HF band's own warping, seen
through the centre tap). Settings that use only LMF, LF or the low end of HMF had the same -1.4 dB
at 20 kHz at 44.1 kHz (-0.5 to -0.6 at 48 kHz); they are now within 0.2 dB of the schematic
everywhere.

### 2. The HF and HMF tops at 1x: the bands' capacitors pre-warped at the knob; the top octave needs 2x

Before, the frequency knobs asked the circuit for the analog frequency that the rule would place
on the knob (the rheostat's law at fs / π · tan (π f / fs)). Above 13.2 / 13.6 kHz (HF) and
8.3 / 8.4 kHz (HMF) at 44.1 / 48 kHz that frequency is beyond the pot's end, so the bands stopped
there.

Now the pots go where the console has them for the knob's frequency, at every rate, and each
band's frequency-setting capacitors (HF: C3, C4, C5, C79; HMF: C9, C10; LMF: C12, C13; LF:
C16–C19) are pre-warped together at the knob frequency, the same element-wise correction as in
decision 1. Scaling all of a band's capacitors by one factor gives each of them its analog
admittance at the knob frequency, so at the knob the band is the schematic's; the rest of the
card, warped elsewhere, changes the peak by under 0.1 dB (a 16 kHz bell peaks at 16.0 kHz with
+18.1 dB at 44.1, 48, 96 and 192 kHz alike). A shelf
acts well below its knob (HF: half way 1.6–1.8 octaves lower; LF: 1.5 octaves higher), so on shelf
the band is pre-warped at its half-way point (knob / 3.2 for HF, knob × 2.8 for LF): pre-warped at
the knob, the HF shelf at 12.3 kHz was 1.5 dB off at 4 kHz.

Results (`fx_test --ssl`, now `classic_channel_test`, full boost and cut at the highest Q / bell, HF to
19 kHz and HMF to 9.4 kHz): centres within 2.2 % of the knob at 44.1, 48 and 96 kHz (the 2 % is the neighbouring
band through R94–R97, as on the schematic), boosts and cuts within 0.4 dB of ±18 dB. The flat and
low-band settings are as in decision 1. Before / after, worst difference from the schematic,
20 Hz–20 kHz (in brackets: to 10 kHz):

| Setting | 44.1 kHz before | after | 48 kHz before | after | 96 kHz before | after |
|---|---|---|---|---|---|---|
| HF bell +18 at 6.2 kHz | -8.9 (1.1) | -6.3 (1.1) | -6.1 (0.9) | -5.2 (0.9) | -1.10 (0.21) | -1.05 (0.22) |
| HF bell +18 at 12.3 kHz | -12.4 (2.4) | -9.8 (2.4) | -7.7 (1.9) | -6.8 (2.0) | -0.99 (0.44) | -0.95 (0.45) |
| HF bell +18 at 16 kHz | -13.7 (1.1) | -7.7 (4.7) | -8.3 (1.2) | -4.6 (3.7) | -0.76 (0.76) | -0.75 (0.75) |
| HF bell +18 at 19 kHz | -14.3 (1.7) | -8.3 (8.3) | -9.0 (1.4) | -6.2 (6.2) | -0.60 (0.27) | -1.08 (1.08) |
| HF shelf +15 at 6.2 kHz | -3.7 (0.4) | -1.4 (0.2) | -1.6 (0.3) | -0.8 (0.2) | -0.13 (0.10) | -0.10 (0.03) |
| HF shelf +15 at 12.3 kHz | -2.1 (1.9) | +0.85 (0.66) | -1.5 (1.5) | +0.73 (0.55) | -0.35 (0.35) | +0.18 (0.12) |
| HMF +18, Q 4.1, 9.4 kHz | -6.4 (6.2) | -3.7 (1.6) | -5.5 (5.3) | -3.2 (1.35) | -1.46 (1.14) | -0.86 (0.33) |
| HMF +15, Q 0.5, 3.1 kHz | -7.0 (1.2) | -5.6 (1.2) | -5.2 (1.0) | -4.7 (1.0) | -0.99 (0.24) | -0.97 (0.23) |

At 44.1 and 48 kHz the worst case over the audio band improved for every setting but the HF cut
shelf (+0.45 / +0.39 dB at 20 kHz before, +1.49 / +0.83 after: the summing caps' droop used to
offset the shelf's own error). Bells centred above about 14 kHz trade one error for another (at
96 kHz too, where the old top was 17.1 kHz). Before, a 16 kHz knob gave a bell at 13.6 kHz at
48 kHz; now the bell is at 16 kHz with the console's +18 dB, but narrower on both sides (dB):

| Hz | 2 k | 5 k | 7 k | 10 k | 12.5 k | 15 k | 17.5 k | 20 k |
|---|---|---|---|---|---|---|---|---|
| schematic, 16 kHz bell | +3.1 | +9.3 | +12.2 | +15.4 | +17.2 | +18.1 | +18.0 | +17.4 |
| before (peak at 13.6 kHz) | +2.4 | +8.2 | +11.2 | +15.2 | +17.7 | +17.6 | +14.3 | +8.8 |
| after | +1.4 | +5.8 | +8.5 | +12.2 | +15.3 | +17.8 | +17.3 | +12.6 |

Why the rest needs 2x. The trapezoidal rule maps the whole analog axis, 0 to infinity, onto 0 to
Nyquist: the response at Nyquist is the circuit's at infinite frequency (0 dB for every band,
nothing at all for the flat card, whose 220 pF caps short the summing amps there). A bell whose
analog skirt still stands at +10 dB at 24 kHz has to fall to 0 dB at 24 kHz, and around a centre
at f the rule narrows the bell, in octaves, by 2x / sin 2x (x = π f / fs) whatever it is
pre-warped at: 1.12 at 6.2 kHz, 1.36 at 10 kHz, 2.4 at 16 kHz and 4.1 at 19 kHz at 48 kHz (6.4 at
19 kHz at 44.1 kHz), against 1.03–1.31 at 96 kHz. The pre-warp frequency only chooses where the
error goes: anywhere but the knob puts the peak off the knob, and the top of the range out of
reach. Other one-step rules do worse at the top (backward Euler and the BDFs damp; the bilinear
map is the only first-order map of the frequency axis onto the unit circle, once and without loss,
and its scale, which pre-warping sets, is its only freedom). Sub-stepping inside the module
without filters, the only way to stay at zero latency, makes it worse: two linearly interpolated
sub-steps average H(f) with the mirrored H(fs - f), which costs the flat response 0.4 dB at
13–20 kHz and the 6.2 kHz bell 6.3 dB at 20 kHz (a frequency-domain model of the scheme). Real
oversampling needs anti-imaging filters, that is latency, and the plug-in has it: its
Oversampling control (2x by default) runs the card at 88.2–96 kHz behind JUCE's polyphase IIR
half-band filters (4 samples of latency at 2x), within 1.3 dB of the schematic at every setting
above (at 176.4–192 kHz, 4x, within 0.31 dB; the plug-in does not offer 4x).

So at 44.1 / 48 kHz the controls land where the console has them; up to 10 kHz the response is
within 0.5 dB of the console's for LMF, LF, narrow HMF settings and the HF shelf, within 1.2 dB
for HF bells centred up to 6 kHz and wide HMF bells, and bells centred higher are narrower as
above. For those, and for the top octave, use 2x (the default).

Moving a frequency knob near the top of HF at 1x changes the pre-warp quickly, which shows as small
steps while it moves: sweeping a +12 dB HF bell 2–19–2 kHz in 2 s over a 1 kHz tone, the largest
step is -71 dB re the signal at 44.1 kHz (-91 dB before, when the band stopped at 13.2 kHz),
smaller than a full sweep of the gain knob gives (-65 dB).

### 3. LF bell Q: keep the ECO's set-up (+18 dB, Q 2.16)

SSL's ECO gives one set-up step for the Q trimmers: "Set Q pots for +18dB boost with frequency
pots at mid position, and highest Q (MFs) or bell (HF, LF) selected". It also specifies the LF bell
as ±18 ±1 dB with Q 1.7 ±10 %. With the drawing's values (R87 7k5, the value both the ECO's
change list and the drawing give) the circuit cannot meet both (`classic_channel_eq_ac lfq`):

| RV4 trimmed for | LF bell at 162 Hz |
|---|---|
| +18 dB (the ECO's procedure) | Q 2.16 |
| +17 dB (the gain tolerance's edge) | Q 1.92 |
| Q 1.87 (the Q tolerance's edge) | +16.8 dB |
| Q 1.7 | +15.9 dB |

No trim is inside both tolerances. This is not the op-amp model: with an open-loop gain of 1e5
instead of 1500 the same trims give Q 2.14 and +16.0 dB. With R87 near 10k5 the LF bell would give
Q 1.7 at +18 dB (10k: Q 1.76; 11k: 1.66), but no SSL document shows such a value.

The model keeps the ECO's set-up, for three reasons. The set-up procedure is the only instruction
a technician had, and it sets gain, not Q: every Rev. 4 card, new or modified in the field, was
trimmed to +18 dB, so Q 2.16 is what a card built to the drawing would measure. The same procedure
gives the other three bands SSL's figures, within or at the edge of the tolerances (HF Q
0.85–0.97 against 1.0 ±10 %, the mid bands 0.49–4.24 / 0.48–4.52 against 0.5–4.5 ±15 %). And
the memo gives the LF figure as carried over: "Frequency ranges and Q values of other sections
are unchanged" (from the prototypes, whose HF bell it puts at about 1.7 too), in the memo that
changes R44, R45, R87 and RV4 of the LF section. Trimming for Q 1.7 would leave the LF bell 2.1 dB
short of the ±18 dB that SSL's amplitude spec and the gain knob promise.

### 4. Gain labels: keep the console's behaviour

The gain knob stays a calibrated boost / cut pot: its dB scale is exact at the set-up reference
(the highest Q or bell, frequency pot at mid travel) and elsewhere the circuit decides, as on the
console. HMF at 3.07 kHz in the module (96 kHz, other bands at their defaults, peak dB):

| Q \ knob | +18 | +12 | +6 | +3 | -6 | -18 |
|---|---|---|---|---|---|---|
| 4.1 | +17.85 | +11.92 | +5.98 | +2.99 | -5.98 | -17.85 |
| 2.0 | +16.29 | +11.05 | +5.56 | +2.77 | -5.57 | -16.30 |
| 1.5 | +15.91 | +10.84 | +5.46 | +2.71 | -5.47 | -15.92 |
| 1.0 | +15.58 | +10.66 | +5.38 | +2.67 | -5.39 | -15.59 |
| 0.5 | +15.54 | +10.73 | +5.45 | +2.71 | -5.46 | -15.56 |

The shelves at full travel give HF +15.2 / -15.4 dB at 20 kHz and LF +16.5 / -16.5 dB at 20 Hz.

Exact dB everywhere was rejected:
- SSL specifies the range per setting, not per knob: "±15dB – lowest Q, ±18dB – highest Q" for
  the mid bands and "±15dB Shelf, ±18dB Bell" for HF and LF. A Q-dependent range is the design:
  the Q pot's second gang raises T5's gain as the Q falls by just enough for ±15.5 dB at the
  lowest Q (with that gang left at its highest-Q end, the lowest Q would give under +3 dB).
- Exact dB means the boost / cut pot following the Q knob and the shape switch. On the console,
  widening the Q at a fixed boost lowers the peak by up to 2.4 dB; with exact dB it would not, so
  sweeping or automating Q would sound unlike the console. At the lowest Q and on the shelves the
  top of the knob (±18) could not be reached at all.
- A switch between the two would be a control the console does not have (in analog-recreation it
  would also have needed a 13th control in a 12-control block).

The knobs' help texts give the ranges (±18 at the narrowest Q, about ±15.5 at the wider ones, the
shelves' ±15.5 / ±16.5).

## The plug-in's engine: a linear state space of the same netlist

Version 0.2.0 no longer solves the netlist every sample. For fixed knobs, with every op-amp below its
limit, the pre-warped trapezoidal card is a linear time-invariant system, so the circuit solver is
asked once per control update (every 32 samples while a control moves) for that system's discrete
state space, `s[n+1] = A s[n] + B u[n]`, `y[n] = C s[n] + D u[n]` (`LinearNetwork::extractStateSpace`).
The states are the capacitors' trapezoidal companion histories; `A = -I - 2 diag(g) W` with `W` the
capacitor-voltage response to a unit history, found by N + 1 solves of the already factored matrix.
A pre-warp change keeps each capacitor's voltage and current (`hist' = hist + (g - g') v`, as
`setCapacitorWarp` does), a pot or switch change keeps the histories, so sweeps stay click-free.
Everything is derived from the netlist and the control laws; nothing is fitted to a curve.

Version 0.1.0 of this plug-in (our own previous release) ran the whole netlist per sample with each of
the 13 NE5534s as a nonlinear Newton device. The numbers below that compare with "v0.1.0's engine" were
measured against that version.

**Realisation.** Networks, cut where an op-amp output drives the next part (the part behind it loads
the one before it by the amp's closed-loop output impedance, a fraction of an ohm):

| Network | States | What |
|---|---|---|
| hpf | 4 | C34, C36, C37 and C39 / C40 (merged into 110 µF), T11, T12; loaded by S4's contact, R66 and R70 (the LPF's input load) |
| lpf | 2 | C43, C44, T13 |
| eq1 | 9 | T3's section: HF, HMF; C8, C79, C3, C4, C5, C9, C10, C2, C11 |
| eq2 | 8 | T8's section: LMF, LF; C15, C16 ‖ C17, C18 ‖ C19, C12, C13, C14, C20, C21 |
| dcb | 1 | C23 / C24 (110 µF) into R53: the output DC blocker |

The 24 states run as dense double-precision products, 4 to 9 states each, in registers (NEON or
SSE2, unrolled per state count), and for a stereo instance both channels in the two lanes of every
vector with shared matrices. The HPF and LPF switches and EQ IN route the signal as the contacts do;
every stage runs whether it is switched in or not.

**Which large capacitors stay.** Worst response change from shorting one capacitor, all the others
kept, 48 kHz, 61 settings, 20 Hz – 20 kHz:

| C22 | C1 | C2 | C6 | C7 | C11 | C14 | C20 | C21 | C51 | C39 + C40 | C41, C42, C46, C47 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0.018 | 0.018 | 0.060 | 0.009 | 0.026 | 0.42 | 2.05 | 0.28 | 0.090 | 0.031 | 2.38 | 0.0006 each |

(dB.) C14 and C11 sit in the Q-pot's positive-feedback path: a 1.6 Hz corner still matters at the
skirts of a narrow +18 dB bell. Kept: C2, C11, C14, C20, C21 and C39 / C40 (the series 110 µF); shorted:
the rest. With C7 (0.026), C51 (0.031) and the pair C22 / C1 (0.018 each) shorted too the total is
0.047 dB, inside a 0.1 dB budget, and each kept capacitor costs a state, so the line is drawn at
0.05 dB. A DC offset is blocked by the output capacitor only. The 50 Ω behind each op-amp in v0.1.0's
engine changes the grid by 0.001 dB and is left out. Leaving the HPF's load (R66 and R70) out of its
network costs 4.3 dB at 20 Hz: the 110 µF in its output network is not a short there, so the load
stays in.

**Accuracy against v0.1.0's engine.** 61 settings (each band at ±18 dB, frequency at both ends and the
middle, Q at both ends, shelf and bell; HPF in at 16, 80 and 390 Hz; LPF in at 3.2, 12 and 22 kHz; EQ
out; four mixed), impulse response at -40 dBFS, every FFT bin 20 Hz – 20 kHz:

| Rate | Worst | Setting | Next |
|---|---|---|---|
| 48 kHz | 0.0471 dB at 20 Hz | HF +18 dB, 1.3 kHz, bell | 0.043 (mixed D, 20 kHz), 0.040 (LF -18 dB 37 Hz bell, 20 Hz), 0.039, 0.034 |
| 96 kHz | 0.0472 dB at 20 Hz | the same | 0.040, 0.039, 0.034, 0.033 |

Time-domain null on noise (4 s, the last 2^17 samples, residual energy against v0.1.0's output,
20 Hz – 20 kHz): -40 dBFS peak, worst -46.2 dB (48 kHz) / -46.5 dB (96 kHz) (mixed B), mean -58.8 /
-58.4 dB. At -12 dBFS peak the worst is -36.7 / -44.1 dB (all four bands +18, which clips: the
saturation differs, below), mean -58.0 / -57.6 dB. Over the whole band the worst is -26 dB: the
subsonic range, where this engine has fewer high-pass poles.

Control sweep (each control and then all together moved over its range in 2 s while a 1 kHz tone at
-12 dBFS plays, blocks of 512): all finite; the largest sample-to-sample jump equals v0.1.0's to the
fourth decimal in every case (0.1140 against 0.1138 for all together); the largest difference between
the two engines is 0.0012.

**Saturation.** The op-amp curve (exactly linear to ±14.85 V, where the clipper returns its input bit
for bit; tanh only above the knee, into ±16.5 V) is applied in feed-forward form at the section outputs
T3 and T8 and, with the HPF / LPF in, at T12 and T13. v0.1.0's engine put it in every op-amp's feedback
loop. Headroom map (measured on v0.1.0's engine, impulse response at -40 dBFS read at its worst
frequency; the level at which the amp's output reaches ±14.85 V):

| Op-amp | Flat | Worst gain | Worst setting | Reaches the limit at (dBFS in) |
|---|---|---|---|---|
| T1 (HF band-pass) | -46.6 dB | +1.2 dB | HF +18 bell 19 kHz | +3.4 |
| T2 (HF out) | -26.0 | +17.1 | HF +18 bell 19 kHz | -12.4 |
| T3 (section 1 sum) | 0.0 | +18.3 | all four +18 (alone: +18.2, HMF 9.4 kHz Q 4.1) | -13.7 (alone -13.5) |
| T4 (HMF band-pass) | -58.8 | +16.6 | HMF +18 9.4 kHz Q 4.1 | -12.0 |
| T5 (HMF out) | -51.1 | +16.7 | the same | -12.0 |
| T6 (LMF band-pass) | -71.8 | +18.5 | all four +18 (alone: +16.7, Q 4.2) | -13.8 (alone -12.0) |
| T7 (LMF out) | -64.0 | +26.3 | all four +18 (alone: +16.8) | -21.6 (alone -12.1) |
| T8 (section 2 sum) | 0.0 | +28.5 | all four +18 (alone: +18.1) | -23.9 (alone -13.5) |
| T9 (LF band-pass) | -88.0 | +12.1 | all four +18 (alone, bell 37 Hz: +10.5) | -7.5 (alone -5.8) |
| T10 (LF out) | -67.8 | +18.7 | all four +18 (alone: +17.1) | -14.1 (alone -12.4) |
| T11 (HPF follower) | -6.0 | -5.9 | HPF in | +10.5 |
| T12 (HPF out) | 0.0 | +0.1 | HPF in | +4.5 |
| T13 (LPF out) | 0.0 | +0.1 | HPF in 16 Hz | +4.5 |

Flat, the first amps to reach the limit are T3, T8, T12 and T13 at +4.6 dBFS in, which is the card's
output level there. With one band at +18 dB, T3 or T8 reaches it first, 1.0 dB (HF bell: T2),
1.2 – 1.5 dB (HMF / LMF at the narrowest Q: T4 / T5, T6 / T7) and 1.1 dB (LF bell: T10) before the
internal amp; with all four bands at +18 dB the order is T8 (-23.9), T7 (-21.6), T3 (-13.7), then the
others. So the section output is the right place for the clipper, and the amps it leaves out are 1 – 2
dB behind it in the worst cases. A typical setting (HF +4 shelf, HMF +3 dB at 2.5 kHz, LMF -3 at 300,
LF +3 at 80, HPF 40 Hz in) puts T3 and T8 at +3.6 dB: a snare peaking at -6 dBFS is at -2.4 dBFS
there and stays linear (the limit is +1.0 dBFS in). A +18 dB narrow bell leaves the linear region at
-13.5 dBFS in at its centre frequency, a +12 dB bell at about -7.5. The CARD lamp in the editor lights
when any clipper is reached.

THD, 1 kHz, 48 kHz, this engine against v0.1.0's:

| Setting | Level | This engine: peak, THD | v0.1.0: peak, THD |
|---|---|---|---|
| flat | -6 dBFS | -6.04, 0.000 % | -6.05, 0.000 % |
| flat | +6 | +5.30, 2.92 % | +5.24, 2.71 % |
| flat | +10 | +5.34, 18.9 % | +5.25, 14.6 % |
| HPF 80 Hz + LPF 12 kHz in | +6 | +5.13, 3.73 % | +5.23, 3.15 % |
| HPF 80 Hz + LPF 12 kHz in | +10 | +5.16, 19.6 % | +5.24, 19.1 % |
| HMF +12 dB, 1 kHz, Q 1.5 | +6 | +5.35, 34.9 % | +5.25, 34.3 % |
| HMF +18 dB, 1 kHz, Q 4.1 (narrow) | -6 | +5.34, 23.9 % | +5.25, 13.3 % |
| HMF +18 dB, 1 kHz, Q 4.1 (narrow) | +6 | +5.34, 41.5 % | +5.26, 30.0 % |
| HMF +18 dB, 1 kHz, Q 4.1 (narrow) | +10 | +5.36, 41.6 % | +5.24, 34.3 % |
| LF +18 dB shelf, 100 Hz | +6 | +5.58, 38.0 % | +5.38, 30.9 % |

The clipper matches where the section output is what clips; where it is a boosted band's own loop
(the narrow +18 dB bell), v0.1.0's amps clipped inside their feedback, which lowers the band's gain
and the harmonics; the feed-forward clipper overstates the distortion there (41 % against 30 %), all
of it at levels where the output is already 10 dB over the limit. Feeding a band-internal amp's output
back as one more output row and clipping there was considered and not built: a feed-forward row cannot
reproduce the loop's gain reduction, and the amp is within 1.2 dB of the section output at the narrowest
Q, where both are 10 dB or more into the rail.

**Speed** (Apple M3, one core, a busy machine, x realtime, best of several passes; the current
release's figures are in the README):

| | v0.1.0 | This engine |
|---|---|---|
| card, mono, 48 kHz | 10.1 (8.9 under load) | 1050 |
| card, mono, 96 kHz | 4.9 (3.5) | 525 |
| plug-in, defaults, stereo, 1x / 2x | 4.3 / 2.1 | 820 / 270 |
| plug-in, defaults, mono, 1x / 2x | 9.5 / 4.4 | 1055 / 400 |
| everything in (filters, bands, dynamics), stereo, 1x / 2x | 4.2 / 2.2 | 300 / 130 |
| everything in, mono, 1x / 2x | 7.6 / 4.8 | 354 / 165 |
| everything in, every card control moving, stereo, 1x / 2x | 4.4 / 2.4 | 92 / 66 |
| everything in, every card control moving, mono, 1x / 2x | 8.5 / 4.7 | 99 / 73 |

With the card this fast the dynamics dominated: its coefficients (up to seven `exp()` calls per sample)
are now computed once per block, its two logarithms are one (the dB law is monotonic), and the gate,
whose range the plug-in keeps at 0 dB, is skipped while it is idle: bit-identical audio up to float
rounding.

## What is approximated

- **Op-amps**: open-loop gain 1500, memoryless (a 5534 with 22 pF has about that gain at 7 kHz; its
  DC figure of 100 dB would make a nonlinear solver crawl). It costs 0.04 dB at flat; the trimmer
  set-up absorbs it in the bands. The amp is an ideal gain-1500 source (v0.1.0 put 50 Ω behind it;
  the difference is 0.001 dB). Linear to ±14.85 V, then a tanh knee into ±16.5 V, applied once at
  each section's output (see "The plug-in's engine"). Slew rate and the real gain-bandwidth are not
  modelled.
- **J112s**: 50 Ω switches (their r_DS(on) is at most 50 Ω; they sit in series with 2.2–100 µF).
- **Trimmers**: set by the ECO's procedure on the schematic: RV1 1992 Ω, RV2 182.9 Ω, RV3 176.8 Ω,
  RV4 714 Ω. "Frequency pots at mid position" is taken as 7k5 of the 50k (15 %, a log law); a real
  card's set-up depends on its pots.
- **Left out**: the electrolytics' polarising resistors (R50, R51, R52, and in the filters R63, R67,
  R74), R54 10R, the motherboard's load (taken as R53's 10k alone), the 5534s' compensation
  capacitors (C26, C28, ... and C35, C38, C45: inside the op-amp model's memoryless gain), pot
  tolerances and end resistance.
- **The filter section** (see its note): its trimmers set by decision 1 of the filter note (RV6
  for K = 2, RV5 for 0 dB with the pot at mid position), C43 read as 6n8, the switch contacts
  0.1 Ω, and the 611's buffering between the filter and EQ loops ideal (a unity buffer).
- **Laws**: the gain, frequency and Q laws are computed at the set-up reference with the other
  bands at mid travel and the highest Q; the cross-coupling moves them a little elsewhere (see
  above).

## Engine changes

The circuit solver (`Source/dsp/circuit/`) is a modified-nodal solver adapted from the
analog-recreation project; the parameter-smoothed module base is `Source/dsp/CircuitModule.*`, and its
public interface is in `Source/dsp/Module.h`. This release uses it to build and factor the network
and to extract the state space; it no longer contains the nonlinear device solvers v0.1.0 used per
sample (the op-amp device with its linear core and the Newton device groups).

For decisions 1 and 2, `LinearNetwork` has pre-warped capacitors: `warpedCapacitor()` adds one and
`setCapacitorWarp (index, hz)` scales its trapezoidal companion conductance by x / tan x
(x = π hz T), so its admittance is the analog one at hz; 0 Hz is the plain rule. A change while
running keeps the capacitor's voltage and current (the history term is adjusted) and refactors like a
pot. Plain `capacitor()` is untouched. The pre-warping adds nothing per sample: it changes
conductances when a frequency or shape control moves, which refactors anyway.

Cost with v0.1.0's engine, mono, one core, Apple M3 under load (load average 9–13 on 8 cores): the EQ
netlist ran at 10–11x realtime at 48 kHz and the whole card at 8.5–9.5x, 4.6x at 96 kHz; the filters
cost about 20 %, and heavy clipping (all four bands at +18 dB on a full-scale chord) 11.4x. See
"Speed" above for this engine.

## The plug-in at 1x and 2x

Through the processor (`classic_channel_processor_test`: impulse through `processBlock` at 48 kHz, stereo,
JUCE's 2x polyphase IIR half-band filters, 2.7 s), dB:

| Setting | | 20 | 80 | 160 | 1k | 5k | 8k | 10k | 12k | 14k | 16k | 18k | 20k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| flat | 1x | −0.05 | −0.04 | −0.05 | −0.05 | −0.04 | −0.04 | −0.04 | −0.04 | −0.04 | −0.04 | −0.04 | −0.05 |
| | 2x | −0.05 | −0.04 | −0.05 | −0.05 | −0.04 | −0.05 | −0.06 | −0.06 | −0.07 | −0.07 | −0.08 | −0.10 |
| HPF 80 Hz in | 1x | −36.30 | −3.06 | −0.09 | −0.04 | −0.04 | −0.04 | −0.05 | −0.06 | −0.06 | −0.07 | −0.07 | −0.08 |
| | 2x | −36.30 | −3.06 | −0.09 | −0.04 | −0.03 | −0.05 | −0.06 | −0.07 | −0.09 | −0.10 | −0.11 | −0.12 |
| LPF 12 kHz in | 1x | −0.05 | −0.05 | −0.05 | −0.05 | −0.08 | −0.46 | −1.29 | −3.05 | −6.00 | −10.18 | −15.66 | −23.16 |
| | 2x | −0.05 | −0.05 | −0.05 | −0.05 | −0.12 | −0.70 | −1.63 | −3.07 | −4.89 | −6.95 | −9.11 | −11.31 |
| HF +18 bell 10 kHz | 1x | −0.05 | −0.03 | +0.01 | +1.68 | +12.37 | +16.97 | +18.07 | +17.07 | +14.89 | +12.31 | +9.49 | +6.36 |
| | 2x | −0.05 | −0.03 | +0.02 | +2.07 | +13.28 | +17.33 | +18.11 | +17.51 | +16.28 | +14.89 | +13.53 | +12.25 |

The schematic gives −0.10 dB at 20 kHz flat and −9.66 dB at 20 kHz for the LPF at 12 kHz
(`classic_channel_eq_ac filters`); both are closer at 2x, where 20 kHz is a fifth of the rate. Corners and
bell centres land on the knob at both factors; the rest is the trapezoidal rule's compression
near Nyquist described under decision 2.
