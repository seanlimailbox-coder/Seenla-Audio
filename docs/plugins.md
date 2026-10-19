# The Open collection

Four macOS audio effects for shaping dynamics, transients, and level. All retain their original **Open** product names.

Feature descriptions below reflect development as of October 18, 2026. Refer to the notes accompanying a downloadable release for its exact capabilities.

## Open Clipper

**Shape transients. Control peaks.**

Open Clipper provides clipping and peak control with input drive, an adjustable threshold and knee, wet/dry mix, and output gain. Oversampling and protection options offer control over the balance between character, peak behavior, processing cost, and latency.

The waveform and gain-reduction history help connect each adjustment to its effect on the signal. It is suited to transient shaping, adding density, and managing peaks before subsequent dynamics processing.

**Beta 1 version:** 2.4.23. The frozen-history inspection badge follows the cursor with a centered, single-row readout.

## Open Compressor

**Control dynamics with a clear view of the detector.**

Open Compressor combines threshold, ratio, knee, attack, hold, release, range, stereo linking, and parallel mix controls. Peak/RMS detection, detector filtering, external sidechain routing, audition, and optional lookahead provide additional control over what drives the compression.

Input/output history, detector traces, and gain-reduction metering make it easier to inspect the response. Auto Gain and Auto Release offer additional starting points for balancing and timing the result.

**Beta 1 version:** 2.10.15. Frozen inspection uses a moving card with centered input, output, gain-reduction, sidechain, and detector readings.

## Open Gate

**Shape space, separation, and sustain.**

Open Gate provides gating and downward expansion with threshold, hysteresis, ratio, range, attack, hold, and release controls. Sidechain filters, external key routing, and detector audition help isolate the events that should open or close the gate.

Lookahead and preopen controls support anticipatory operation. Flip mode reverses the level-dependent action for alternate dynamics treatments, while the history display and transfer overlay help show the result.

**Beta 1 version:** 1.8.37. Frozen inspection uses the shared moving card, with centered readouts and Shift-drag range selection for Learn Noise.

## Open Limiter

**Control final levels while watching peaks and loudness.**

Open Limiter is a lookahead limiter with gain, ceiling, attack, release, and stereo-link controls. Nine limiting styles provide different responses. Optional oversampling and true-peak protection add further control over output peaks.

Input/output history, gain reduction, and integrated, short-term, and momentary loudness views help assess the result. Style and quality settings can change the reported latency; consult the matching release notes for measured peak behavior and limitations.

**Beta 1 version:** 7.3.48. Preset and category menus use readable spacing and smooth opening/closing fades. The ceiling range extends down to −18 dB.

## Choosing a starting point

| Goal | Start with |
| --- | --- |
| Round off or reshape short peaks | Open Clipper |
| Adjust the movement and envelope of a performance | Open Compressor |
| Reduce spill or shorten sustain between events | Open Gate |
| Manage final output peaks and monitor loudness | Open Limiter |

See [installation and compatibility](installation.md) for host formats and package selection.
