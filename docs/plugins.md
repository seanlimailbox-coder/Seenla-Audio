# The Open collection

Four macOS audio effects for shaping dynamics, transients, and level. All retain their original **Open** product names.

Feature descriptions below reflect development as of October 17, 2026. Refer to the notes accompanying a downloadable release for its exact capabilities.

## Open Clipper

**Shape transients. Control peaks.**

Open Clipper provides clipping and peak control with input drive, an adjustable threshold and knee, wet/dry mix, and output gain. Oversampling and protection options offer control over the balance between character, peak behavior, processing cost, and latency.

The waveform and gain-reduction history help connect each adjustment to its effect on the signal. It is suited to transient shaping, adding density, and managing peaks before subsequent dynamics processing.

**Development version:** 2.4.15.

## Open Compressor

**Control dynamics with a clear view of the detector.**

Open Compressor combines threshold, ratio, knee, attack, hold, release, range, stereo linking, and parallel mix controls. Peak/RMS detection, detector filtering, external sidechain routing, audition, and optional lookahead provide additional control over what drives the compression.

Input/output history, detector traces, and gain-reduction metering make it easier to inspect the response. Auto Gain and Auto Release offer additional starting points for balancing and timing the result.

**Development version:** 2.10.8. This development version is newer than the most recent local distribution ZIP, 2.10.6; any first GitHub download must be prepared from the intended version.

## Open Gate

**Shape space, separation, and sustain.**

Open Gate provides gating and downward expansion with threshold, hysteresis, ratio, range, attack, hold, and release controls. Sidechain filters, external key routing, and detector audition help isolate the events that should open or close the gate.

Lookahead and preopen controls support anticipatory operation. Flip mode reverses the level-dependent action for alternate dynamics treatments, while the history display and transfer overlay help show the result.

**Development version:** 1.8.33.

## Open Limiter

**Control final levels while watching peaks and loudness.**

Open Limiter is a lookahead limiter with gain, ceiling, attack, release, and stereo-link controls. Transparent, Punch, Dynamic, and Aggressive styles provide different responses. Optional oversampling and true-peak protection add further control over output peaks.

Input/output history, gain reduction, and integrated, short-term, and momentary loudness views help assess the result. Style and quality settings can change the reported latency; consult the matching release notes for measured peak behavior and limitations.

**Development version:** 7.3.36.

## Choosing a starting point

| Goal | Start with |
| --- | --- |
| Round off or reshape short peaks | Open Clipper |
| Adjust the movement and envelope of a performance | Open Compressor |
| Reduce spill or shorten sustain between events | Open Gate |
| Manage final output peaks and monitor loudness | Open Limiter |

See [installation and compatibility](installation.md) for host formats and package selection.
