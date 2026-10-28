# 82E10 dynamics: T82010.71 schematic model

The dynamics section is implemented from the corrected **T82010.71 issue-15** service drawing,
not from a fitted compressor curve. The two complex parts that SSL draws as IC blocks—the AD536J
RMS converter and DBX 2151 Blackmer VCA—use data-sheet macromodels. The components around those
blocks retain their drawing designators in `Source/dsp/SslE10Dynamics.cpp`.

## Primary sources

- SSL drawing **T82010.71, 611E/V Dynamics Section**, revision table through issue 15, in the
  *SL4000G Console Service Manual*. This is the value authority. In particular, it has **R22 =
  10 kΩ**, R2 = 10 kΩ for SLOW attack (AUTO), R19 = 300 Ω in parallel with it for FAST, C5 =
  3.3 µF and C6 = 22 µF.
- The older two-page public scan is useful for tracing but is not the value authority:
  <https://retrorecordingsxr.com/wp-content/uploads/manuals/ssl/ssl_82E10.pdf>. It prints R22 as
  1 MΩ; that would produce a roughly 22-second release and is a known error.
- Analog Devices **AD536A/J true-RMS converter data sheet**: RMS transfer, 16.67 kΩ typical input
  resistance, 25 ms/µF averaging constant and auxiliary dB output:
  <https://www.analog.com/media/en/technical-documentation/data-sheets/AD536A.pdf>.
- THAT/dbx **2150-series VCA data sheet**: exponential gain law, 2151 distortion at 0 and ±15 dB,
  symmetry trim and the 20 kΩ current-input application circuit.
- SSL's **Channel Strip Guide** for the historical context and user-facing behavior:
  <https://www.solidstatelogic.com/channel-strip-guide>.
- SSL's 611 daughter-card alignment procedure for the 0 dBm, gain and VCA-symmetry trims.

## Audio path

The host sample is converted with the channel's common alignment, 0 dBFS = +18 dBu = 8.7015 V
peak. The modeled card path is:

1. C10 and R78/R79 drive the DBX 2151 current input.
2. R74/R75/R76 and the symmetry trim bias the gain cell. The command-bus scale printed on the
   drawing is 10 dB = 2.5 V.
3. The VCA macromodel applies the DBX exponential control law and the 2151 data-sheet distortion
   rise from 0.004% at 0 dB to 0.025% around ±15 dB.
4. T9 (NE5534), R81 20 kΩ and C12 100 pF form the transimpedance/output stage. It uses the same
   ±18 V rail and soft output-swing model as the EQ card.
5. Back-to-back C13/C14 (220 µF each), R83, R84 and R85 form the biased output coupling network.

Dynamics bypass removes this complete path, so bypass remains bit-exact.

## Detector and compressor control circuit

C1 10 µF, R2 6.2 kΩ and the AD536J's input resistance form the sidechain input high-pass. C2
3.3 nF sets the RMS averaging time using the AD536 data-sheet constant. VR1/R4/R6 and T2 set the
0 dBm reference; R7–R12 scale the dB output to the drawing's `LAW = 10 dB = 1 V` at TP1/TP2.

The two gangs of the 10 kΩ threshold pot provide threshold offset and automatic make-up. The two
10 kΩ ratio gangs form the feed-forward slope. D14/D1 and R88/R56/R55/R20 are evaluated as a
loaded Shockley-diode rectifier, so the soft knee comes from the drawn over-easy network rather
than a quadratic knee formula.

The attack switch is correctly labelled **AUTO / FAST**. It selects the resistance between the
sum node N and the timing node X:

- AUTO selects the SLOW resistor R2 10 kΩ alone. The old code called this R21; the value is the
  same. Measured: 10 dB of reduction in 11.0 ms (the old C5-only model gave 12.8 ms).
- FAST puts R19 300 Ω in parallel with R2: R2 ∥ R19 = 291 Ω. Measured: 10 dB in 0.35 ms (the old
  model gave 0.42 ms, with R19 alone).

**Release network (T82010·71, the drawing's topology).** The nodes are X, B, N20 and N:

- X is the control-voltage node (CV = V(X)), read through a high-impedance buffer.
- C5 3.3 µF runs from X to B. C6 22 µF runs from B to 0 V.
- R22 10 kΩ runs from B to N20 (the public scan at the primary-sources link prints 1 MΩ there, the
  known error). The RELEASE pot, a 250 kΩ logarithmic rheostat, runs from N20 to 0 V. R23 10 kΩ runs
  from N20 to N.
- The attack resistance runs from N to X.
- Charging: when the detector target exceeds V(N), D3 conducts and drives N to the target (ideal
  diode). Otherwise N floats, joined only by R23 and the attack resistance. R13 (1 MΩ) is ignored.

Each sample solves the nodal system for V(X), V(B), V(N20) and V(N) (a 4 × 4 dense solve, fixed
arrays, no allocation). C5 and C6 are backward-Euler companions. The states are V(X) and V(B), so a
change of release or attack keeps the stored charge.

*Check against the drawing.* The crop of T82010·71 used for this model shows the charging diode
(labelled D7 in the crop) on the X junction, not on N. A diode directly on X would bypass the attack
switch, so the model follows the drawing-based D3 on N. The full sheet should confirm the designator and
the node.

**Release law.** The knob's seconds are the 10 % recovery of the gain reduction after 2 s of a
1 kHz tone at about 10 dB of reduction, then a tone 60 dB under the threshold. For each pot
resistance on a 33-point grid (0 Ω, then 32 points 0.1 decade apart up to 250 kΩ), the time is
measured offline by the same solver (`SslE10Dynamics::compressorTenPercentSeconds`; `classic_channel_test
--release-table` prints the grid). The table is embedded in `SslE10Dynamics.cpp`. At run time the
seconds map to ohms by interpolation of ln(ohms) against ln(seconds).

Measured table: the achievable 10 % range is **0.140 s (about 790 Ω) to 12.6 s (250 kΩ)**. The
curve is not monotonic below 790 Ω. At 0 Ω the time is 0.144 s, slower than at 790 Ω, because C6
drains through R22 while the pot is near zero. The 0.1 s legend lies below the range; it sits at the
0.140 s end and is reported as such. The hand estimate (C5 discharging through R23 + R2 = 20 kΩ,
ln(10) × 3.3 µF × 20 kΩ = 0.152 s) is close; the simulated 0.144 s is shorter because part of the
charge reaches C6 through R22. The 0 Ω end is taken as a 1 Ω wiper floor, a chosen value. An offline
check with a 0.001 Ω floor moves the 0 Ω time by 0.006 %, so the floor does not affect the result.

Measured through `process()` (48 kHz, 2 s hold at the 10 dB tone): knob 0.1 → 0.140 s (range end,
was 1.007 s); 0.4 → 0.397 s (was 1.007 s); 1.0 → 1.003 s (was 1.007 s); 1.5 → 1.499 s (was 1.497 s);
4.0 → 3.989 s (was 3.981 s). The old model could not go below 1.007 s because the pot sat in series
with R22 + R23 on the C6 leg.

**Program dependence.** A 50 ms burst recovers faster than the sustained tone at the same knob:
at 4 s, 1.20 s against 3.99 s; at 1 s, 0.29 s against 1.00 s. C6 has charged little after a burst,
so the release has less to drain. The sustained reference is the reduction at the end of the 2 s
hold, and at long settings that reduction is still rising then: C6 charges through R22 and the pot,
and at 4 s the reduction settles only after about 8 s.

**Static reduction depends on the release knob.** At one level, the reduction the tone reaches
after the 2 s hold grows with the release setting: about 10.0 dB at 0.1 s, 10.25 dB at 1 s and
10.85 dB at 4 s. The cause is the detector ripple, not the timing solver. The model's AD536 averaging
(25 ms/µF with the drawing's 3.3 nF, about 82 µs) leaves a 2 kHz ripple on a 1 kHz tone: the target
swings from about 1.7 to 3.0 V. D3 charges N to the peaks, and the release network keeps that peak
bias. With the averaging made 1000 times longer, every setting holds the same 10.49 dB. This is left
as the topology gives it. Whether it is right is an open question for the owner (the value of C2,
or the diode's node).

## Expander/gate

The threshold and comparator path keeps the drawing's 25 kΩ threshold pot, R29/R30/R34 36 kΩ,
R31 510 kΩ, D8/D9 and the 1.5 V reference. Those parts produce the open/close hysteresis. In
EXPAND, the equal 36 kΩ arms give the 2:1 downward slope; in GATE, the range pot sets the maximum
command voltage.

C9 6.8 µF is the gate timing capacitor. TR4 selects the R44 1 Ω FAST or R45 100 Ω SLOW charge
path; transistor saturation resistance is included. R47 10 kΩ and the 250 kΩ logarithmic release
pot discharge C9. The linked release/range pots follow the motherboard ganging shown on the
drawing.

## Stereo link and metering

Each channel has its own AD536 state. SW2/D27 behavior is represented by diode-OR selection of
the control path asking for the greater attenuation, and that command drives both DBX VCA
macromodels. Compressor and gate gain-reduction meters read the same 250 mV/dB command voltages;
the hardware's 3/6/10/14/20 dB comparator ladder does not feed the audio circuit.

## Scope

This is a schematic-level model: every audio/control block shown on T82010.71 that changes the
sound or timing is represented, and the ICs use their published electrical transfers. It is not a
transistor-mask recreation of the AD536J or DBX 2151—the SSL drawing does not contain those IC
internals. Random tolerance, temperature drift, power-supply noise and self-noise are deliberately
not generated.
