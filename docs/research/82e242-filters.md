# 82E242 filter section (HPF / LPF): transcription and decisions

The four EQ bands of the SSL 4000 E "black knob" card 82E242 were already solved from the
schematic in the `analog-recreation` project (`Source/dsp/fx/SslEq.cpp` there; its research note
is ported here as [ssl-eq.md](ssl-eq.md)). That model left the card's high-pass and low-pass
filters out. This note transcribes the filter section from the drawing so the whole card can be
modelled, and records the two readings that needed a decision.

## Sources

- **SSL drawing T82242·70 "Equaliser Filters", issue 8a (June 1986)**: [archive.org's SSL
  4000/6000 individual-cards collection](https://archive.org/details/82-e-12-logic-card), file
  "82E242 Equalizer Filters", page 5. The same file's pages 3 and 4 are rev 2 / rev 3.
- **The same drawing, rev 4 (Jan 1986)**: [archive.org item ssl_ssl_82E242](https://archive.org/details/ssl_ssl_82E242),
  used to cross-check the component values.
- **SSL ECO "The 82E242 Rev. 4", 15 Nov 1985**: [archive.org item ssl_ssl_82E242_eco](https://archive.org/details/ssl_ssl_82E242_eco).
  It specifies the EQ bands only; it says nothing about the filters.
- **82E02 brown knob card, drawing T82002·71 rev 6 (March 1984)**: [KA Electronics' 82E02 PDF](http://www.ka-electronics.com/images/SSL/ssl_82E02.pdf),
  page 1; the earlier card has a related filter topology for comparison.

All ICs are NE5534N on ±18 V. Pin numbers are the card's edge connector.

## High-pass filter ("LF FILTER"), 18 dB/oct: T11, T12

| Part | Value / connection |
|---|---|
| Input | pin 42 → RV5 500R ("UNITY GAIN SET") → node X → R56 300R → ground; C34 220nF from X → node P11. RV5 is a rheostat: its wiper is tied to its R56 end (see the reading notes). C25, across C34, is marked N/C |
| First-order section | P11: R79 1M to ground; R55 1k8 → pin 7 → gang **c** of the 47k A/log triple pot (a rheostat: wiper tied to one end) → ground. T11 is a follower (+ at P11, − tied to its output; C35 22pF compensation) |
| Second-order section | T11 out → C36 220nF → node M → C37 220nF → node P12 (T12 +). M: R59 1k8 → pin 56 → gang **a** (rheostat) → the filter output (the junction after R64, i.e. the Sallen–Key feedback to the output). P12: R60 1k8 → pin 55 → gang **b** (rheostat) → ground |
| T12 gain | non-inverting. R58 7k5 from T12's output to its − input. From − : RV6 5k ("LF FILTER CORNER", a rheostat with the wiper tied to the − end) → R57 5k6 → ground. K = 1 + 7k5 / (5k6 + RV6): 2.34 (RV6 = 0) to 1.71 (RV6 = 5k). C38 10pF is T12's compensation capacitor (drawn on the op-amp, like C35 22p on T11 and C45 22p on T13), not across R58 |
| Output | T12 out → C39 220µ → bias node (R61 100k from T12's output, i.e. across C39; R63 1M to +18 V: polarises the electrolytics; the drawing marks it "+VE") → C40 220µ → node (R65 10k to ground) → R64 10R → pin 54 → switch S4 → pin 51 |
| In / out | S4 is the switch on the pot ("47K A/log TRIPLE + SWITCH"): it connects pin 51 either to the filter's output (R64) or straight to the input line from pin 42. R66 10k loads pin 51 to ground |

Reading notes. RV5's wiper (the "⊣" on its right side) runs right to a vertical line that joins
C25's and C34's left plates and drops to the junction of RV5's lower end and R56, with dots at
both joins: the wiper is tied to that end, so RV5 is a rheostat in series between pin 42 and the
node X that R56 and C34 share. (The first transcription had the wiper alone feeding C34, a
potentiometer; the divider's range is the same, 0.375 to 1.0, but its source resistance and the
trimmer's value for unity are not.) C38 sits under T12 between its + input side and the body,
the way C35 and C45 sit on T11 and T13 and the EQ's 22p / 10p sit on T1-T10: the 5534's
compensation, not a signal-path part; it is not modelled, as the EQ's are not. The pin 51 node
also has R66 10k to ground, which the first transcription left out.

The drawing shows R57 and RV6 as a loop ending in a short diagonal stroke after
"LF FILTER CORNER"; that stroke is the drawing's ground symbol (compare R56, R65, R79, and the
identical loop of R56 / R57 on the 82E02, where the arrowed resistor also ends in it). RV6's
wiper and the track end nearest R58 both join T12's − input, so RV6 is a rheostat in series with
R57 to ground. Read any other way the network is either floating (T12 a follower and the trimmer
inert) or gives K ≥ 3.8, which the Sallen–Key below cannot tolerate.

Analysis. With C36 = C37 = C and the two rheostat legs equal (R = 1k8 + gang), the second-order
section is an equal-component Sallen–Key high-pass with gain K: ω0 = 1 / (R C), Q = 1 / (3 − K).
The first-order section has the same ω0 (C34 220n into 1k8 + gang c). RV6 therefore sets the
filter's Q without moving its corner: 0.78 (RV6 = 5k) to 1.52 (RV6 = 0). K = 2 (RV6 = 1k9)
gives Q = 1: with the first-order section that is a 3rd-order Butterworth, maximally flat, −3 dB
at ω0. The input divider R56 / (R56 + RV5) (0.375 to 1.0) then takes the passband back to unity:
0.5 for K = 2, RV5 = 300 Ω. Its source resistance, R56 ‖ RV5 ≈ 150 Ω, sits in series with the
first-order section's 1k8 + gang c, so the passband level and that section's corner both move a
little with the knob (most at the top of the range, where the leg is only 1k8).

Range: R = 1k8 (gang at zero) → 402 Hz; R = 48k8 → 14.8 Hz.

Decision 1: **RV6 is set for a maximally flat response (K = 2, Q = 1) and RV5 for unity
passband gain.** No SSL set-up instruction for these trimmers was found; "LF FILTER CORNER" names
a corner-shape adjustment, and flat is the only setting with a definition. The exact values are
computed by the AC analysis tool, not taken from the figures above. Unity can only hold at one
setting (the divider's source resistance, above), so RV5 is trimmed for 0 dB at 1 kHz with the
pot at its "mid position", taken as 15 % of the 47k track (7k05) as the EQ's set-up takes 7k5 of
its 50k pots: an A/log pot at half rotation. That puts the HPF's corner at 82 Hz.

## Low-pass filter ("HF FILTER"), 12 dB/oct: T13

| Part | Value / connection |
|---|---|
| Input | pin 51 (R66 10k to ground) → C41 220µ → bias node (R67 1M to +18 V, R68 100k to ground) → C42 220µ → node (R70 10k to ground) → R69 680R → pin 44 |
| Filter | pin 44 → gang **a** of the 10k lin dual pot (rheostat) → pin 45 (node N) → gang **b** (rheostat) → pin 46 → R71 680R → T13 +. C44 3n3 from T13 + to ground. C43 from N to T13's output ("HF FILTER" line). T13 is a follower (C45 22pF) |
| Output | T13 out → C46 220µ → bias node (R72 100k from T13's output, i.e. across C46; R74 1M to +18 V) → C47 220µ → node (R76 10k to ground) → R75 10R → pin 47 → switch → output |
| In / out | the switch on the pot ("10K LIN DUAL + SWITCH"): the output from pin 47 or straight from pin 51 |

Analysis. Unity-gain Sallen–Key low-pass with R1 = 680 + gang a, R2 = gang b + 680:
ω0 = 1 / sqrt (R1 R2 C43 C44), Q = sqrt (R1 R2 C43 C44) / (C44 (R1 + R2)) = ½ sqrt (C43 / C44)
for equal legs.

Decision 2: **C43 is modelled as 6n8, although both scans of T82242·70 print "68n".**
- With 68n: Q = 2.27 (a +7.3 dB resonant peak just below the corner), and the corner (ω0) runs
  from 996 Hz (pot at 10k) to 15.6 kHz (pot at zero); −3 dB points 1.49 – 23.2 kHz.
- With 6n8: Q = 0.72 (Butterworth-like), corner (ω0) 3.15 kHz (pot at 10k) to 49 kHz (pot at zero);
  22 kHz at 850 Ω into the pot; −3 dB points 3.19 – 49.7 kHz (the AC analysis, below).
- The 82E02, the same topology with 470R legs, has C24 6n8 and C25 3n3 (drawn by the same
  draughtsperson a year earlier); the 242's rev 2 "FILTER VALUES CHANGED" fits the 470R → 680R
  legs and the HPF's redesign to 18 dB/oct.
The 6n8 reading is the model choice supported by that earlier card and the resulting response.
The 68n marking remains an unresolved ambiguity in the 82E242 drawing. The selected value is
one constant in the netlist (`kLpfC43`), and the AC tool can print both responses.

## What the AC analysis gives

`tools/ssl_eq_ac.cpp` (the `classic_channel_eq_ac` target) solves the filter netlist above at each frequency
with the op-amps as the circuit model has them (gain 1500 behind 50 Ω), the switch contacts at
0.1 Ω, and the EQ alone as before. The plug-in's model uses exactly these values
(`Source/dsp/SslE242.cpp`).

**Trimmers** (`classic_channel_eq_ac trims`, decision 1):

| Trimmer | Value | Set for | Result |
|---|---|---|---|
| RV6 (LF FILTER CORNER) | **1879.6 Ω** (1900 with an ideal op-amp) | T12's stage gain K = 2.0000 | the second-order section's Q: 0.998 at mid position, 0.987 at the top of the range, 1.022 at the bottom |
| RV5 (UNITY GAIN SET) | **288.15 Ω** (300 with an ideal op-amp and no source resistance) | 0 dB at 1 kHz, gangs at 7k05, LPF out | divider 0.5101, source resistance 147 Ω |

The passband then depends on the setting, as on a card trimmed this way: +0.11 dB with the gangs
at 47k (15 Hz), 0.00 dB at mid position, −0.23 dB at 200 Hz and −0.57 dB with the gangs at zero
(394 Hz), at 10 kHz. The response rises at most +0.02 dB (+0.06 dB at the top) above its
passband: maximally flat for practical purposes.

**Frequency laws** (`classic_channel_eq_ac laws`): the −3 dB point against the filter's own passband (the
HPF's at 10 kHz, the LPF's at 100 Hz), all gangs alike:

| HPF gang (Ω) | 0 | 250 | 500 | 1k | 1k5 | 2k | 3k | 4k | 5k | 6k | 7k5 | 10k | 12k5 | 15k | 20k | 25k | 30k | 35k | 40k | 47k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| −3 dB (Hz) | 394.3 | 348.0 | 310.8 | 256.1 | 217.8 | 189.4 | 150.4 | 124.7 | 106.5 | 92.92 | 78.04 | 61.61 | 50.92 | 43.40 | 33.52 | 27.33 | 23.08 | 19.99 | 17.63 | 15.15 |

| LPF gang (Ω) | 0 | 100 | 250 | 500 | 750 | 1k | 1k5 | 2k | 2k5 | 3k | 4k | 5k | 6k | 7k | 8k | 9k | 10k |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| −3 dB (Hz) | 49675 | 43626 | 36590 | 28839 | 23798 | 20256 | 15611 | 12698 | 10702 | 9248 | 7272 | 5992 | 5095 | 4431 | 3921 | 3516 | 3186.5 |

The module interpolates these on log (R + 1k8) and log (R + 680R) against log f; read that way the
−3 dB point lands within 0.16 % (HPF) and 0.001 % (LPF) of the knob wherever the pots reach it.
The analytic Sallen–Key figures agree: 402 / 14.8 Hz for the HPF's ω0 at the pot's ends (the
circuit's −3 dB points are 394 / 15.1 Hz: the first-order section's extra 147 Ω and the
finite op-amp gain), and 3146 Hz for the LPF's ω0 at 10k, its −3 dB point 1.3 % higher (Q 0.717).

**Responses** (`classic_channel_eq_ac filters`):

| HPF knob | gangs | −3 dB | passband (10 kHz) | slope fc/4 → fc/2 | T12 stage Q |
|---|---|---|---|---|---|
| 15 Hz | 47k (end) | 15.15 Hz | +0.11 dB | 18.21 dB/oct | 1.022 |
| 30 Hz | 22k59 | 30.00 Hz | +0.08 dB | 18.14 dB/oct | 1.008 |
| 80 Hz | 7k27 | 80.00 Hz | −0.01 dB | 18.09 dB/oct | 0.999 |
| 200 Hz | 1k80 | 200.0 Hz | −0.23 dB | 18.04 dB/oct | 0.993 |
| 400 Hz | 0 (end) | 394.3 Hz | −0.57 dB | 17.99 dB/oct | 0.987 |

| LPF knob | gangs | −3 dB | passband (100 Hz) | slope 2fc → 4fc | Q |
|---|---|---|---|---|---|
| 3 kHz | 10k (end) | 3186.5 Hz | −0.006 dB | 11.85 dB/oct | 0.717 |
| 6 kHz | 4k99 | 6000 Hz | −0.006 dB | 11.85 dB/oct | 0.717 |
| 12 kHz | 2k16 | 12000 Hz | −0.006 dB | 11.85 dB/oct | 0.717 |
| 20 kHz | 1k02 | 20000 Hz | −0.006 dB | 11.85 dB/oct | 0.717 |
| 22 kHz | 867 | 22000 Hz | −0.006 dB | 11.85 dB/oct | 0.717 |

Headroom: no op-amp of the filter section sees more than the input (+0.15 dB at most, T12 at the
15 Hz setting), so the filters clip where the EQ does, at +4.6 dBFS out.

The whole card flat (filters and EQ through the ideal buffer, so the responses multiply), dB:

| Hz | 20 | 40 | 80 | 160 | 1k | 5k | 10k | 12k | 16k | 20k |
|---|---|---|---|---|---|---|---|---|---|---|
| filters out | −0.04 | −0.04 | −0.04 | −0.04 | −0.04 | −0.04 | −0.06 | −0.07 | −0.08 | −0.10 |
| HPF 80 Hz + LPF 12 kHz in | −36.30 | −18.21 | −3.06 | −0.09 | −0.04 | −0.13 | −1.74 | −3.09 | −6.37 | −9.66 |

**C43: 6n8 against the drawing's 68n** (`classic_channel_eq_ac lpf68n`, decision 2):

| Gangs | C43 | −3 dB | ω0 (phase −90°) | Q | peak |
|---|---|---|---|---|---|
| 0 | 6n8 | 49.7 kHz | 49.0 kHz | 0.717 | none |
| 0 | 68n | 23.2 kHz | 15.5 kHz | 2.25 | +7.27 dB at 14.7 kHz |
| 850 Ω | 6n8 | 22.2 kHz | 22.0 kHz | 0.717 | none |
| 850 Ω | 68n | 10.4 kHz | 6.94 kHz | 2.25 | +7.27 dB at 6.56 kHz |
| 5k | 6n8 | 5.99 kHz | 5.92 kHz | 0.717 | none |
| 5k | 68n | 2.80 kHz | 1.87 kHz | 2.25 | +7.25 dB at 1.77 kHz |
| 10k | 6n8 | 3.19 kHz | 3.15 kHz | 0.717 | none |
| 10k | 68n | 1.49 kHz | 0.99 kHz | 2.25 | +7.20 dB at 949 Hz |

With 68n the LPF would reach only 1.5–23 kHz (−3 dB) and peak 7.3 dB below its corner (the gangs
at 5k: +2.5 dB at 1 kHz, +6.1 dB at 2 kHz, −4.8 dB at 3 kHz, where 6n8 gives −0.0, −0.04 and
−0.23 dB). The model keeps 6n8; `kLpfC43` in `Source/dsp/SslE242.cpp` holds it.

## Response from the selected circuit values

The response follows the component values and model choices documented above.

- **The LPF's −3 dB range is 3.19–49.7 kHz.** With R69 / R71 at 680R, the 10k dual, C44 at
  3n3 and C43 at 6n8, its lower point with the pot at 10k is 3186.5 Hz. The knob therefore
  starts at 3.2 kHz and stops at 22 kHz; the circuit's top is 49.7 kHz.
- **The HPF's range is 15.1–394 Hz.** The knob runs 16–390 Hz, inside the pot's ends.
- **The HPF's passband level moves with the knob**, from +0.11 dB (15 Hz) to −0.57 dB (394 Hz),
  0 dB at the trimmer's set-up point (82 Hz). That is RV5 / R56's source resistance (147 Ω) against
  the first section's 1k8 + gang c; a card trimmed at another setting would move the whole curve.
- **18 and 12 dB/oct.** One to two octaves past the corner the circuit gives 17.99–18.21 dB/oct
  (HPF) and 11.85 dB/oct (LPF), as the specification says.

## The plug-in's filter tests

`build/classic_channel_test` measures the time-domain model (impulse responses through the whole card, the
EQ flat and divided out) against the figures above: every HPF and LPF setting it tries puts its
−3 dB point within 0.05 % of the knob (or of the pot's end) at 48 and 96 kHz, the slopes are
17.99–18.14 dB/oct and 12.3–12.7 dB/oct, the passbands within 0.01 dB of the schematic's. The
LPF's slope is measured at a rate where 4 × the corner is under fs / 8 (96 kHz for 3 kHz up to
768 kHz for 20 kHz): at 48 or 96 kHz the trapezoidal rule compresses the frequency axis near
Nyquist (it puts −∞ dB there), which makes the digital slope steeper than the circuit's:
15.7 dB/oct between 6.4 and 12.7 kHz for the 3 kHz setting at 48 kHz, 15.2 dB/oct between 12 and
24 kHz for the 6 kHz setting at 96 kHz (12.65 dB/oct for 3 kHz at 96 kHz). The corner itself lands
on the knob at any rate, because each filter's capacitors are pre-warped at it.

## How the filters sit in the channel

On the 611E channel module the filter loop (pins 42 → 47) and the EQ loop (pins 48 → 43) are
separate; the module's switching normally puts the filters before the EQ (the "SPLIT" button
moves them before the dynamics instead). The plug-in runs HPF → LPF → EQ, with the motherboard's
buffering between the filter output and the EQ input taken as ideal (a unity buffer), and the
sections switched in and out with the card's own switches.

## Controls in the plug-in

| Control | Range | Maps to |
|---|---|---|
| HPF | Out / In | S4 |
| HPF frequency | 16 – 390 Hz, log (the −3 dB point of the flat set-up; inside the circuit's 15.1 – 394 Hz) | the three gangs of the 47k rheostat, through the circuit's own law; the filter's capacitors pre-warped at the knob |
| LPF | Out / In | the LPF's switch |
| LPF frequency | 3.2 kHz – 22 kHz, log (−3 dB point; the circuit reaches 3.19 – 49.7 kHz) | both gangs of the 10k rheostat, same treatment |
| EQ | Out / In | the EQ IN switch: the card's output or the line from pin 48 past it, a hard switch in the netlist |

The frequency knobs are logarithmic in Hz, not the pots' rotation (the console's are an anti-log
triple and a linear dual).
