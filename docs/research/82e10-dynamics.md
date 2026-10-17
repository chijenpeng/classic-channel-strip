# 82E10 dynamics: T82010.71 schematic model

The dynamics section is implemented from the corrected **T82010.71 issue-15** service drawing,
not from a fitted compressor curve. The two complex parts that SSL draws as IC blocks—the AD536J
RMS converter and DBX 2151 Blackmer VCA—use data-sheet macromodels. The components around those
blocks retain their drawing designators in `Source/dsp/SslE10Dynamics.cpp`.

## Primary sources

- SSL drawing **T82010.71, 611E/V Dynamics Section**, revision table through issue 15, in the
  [SSL 4000G Console Service Manual](https://funkwerkes.com/web/wp-content/techdocs/MixedProAudio/SSL-4000G-Console-Service-Manual-v1_1.pdf).
  This is the value authority. In particular, it has **R22 =
  10 kΩ**, R21 = 10 kΩ for AUTO attack, R19 = 300 Ω for FAST, C5 = 3.3 µF and C6 = 22 µF.
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

The attack switch is correctly labelled **AUTO / FAST**:

- AUTO selects R21 10 kΩ into C5 3.3 µF. A fixed fraction of a larger control step is reached
  sooner, which is the circuit's program-dependent behavior.
- FAST selects R19 300 Ω. R19 × C5 is 0.99 ms; about three time constants give the published
  3 ms settling figure.

C5, corrected R22 10 kΩ, C6 22 µF, R23 10 kΩ and the 250 kΩ logarithmic release pot form the
two-time-constant recovery. Control voltage decays in the capacitor network before entering the
exponential VCA; no gain-domain release smoother remains.

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
