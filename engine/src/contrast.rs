//! Contrast stage: deepens what the model has already decided is not voice.
//!
//! DeepFilterNet3 turns breaths, clicks and other non-speech sounds down, but
//! often only by 6 to 15 dB, and it nibbles a decibel or two off speech that
//! carries a little noise. The sound this plugin is tuned to (the renders of
//! the original one-knob tool) has more contrast: those sounds are all but
//! removed and speech is left a little fuller.
//!
//! Every frame the stage measures how much the model turned the 250 Hz-5 kHz
//! band down, compared with the same input before the model, and maps that
//! through a curve:
//!
//! * mild attenuation (down to `KNEE_DB`) is eased back by `EASE`, so speech
//!   keeps its body;
//! * beyond the knee every further dB the model removes becomes `SLOPE` dB,
//!   up to `DEPTH_DB` extra.
//!
//! The curve and timings were fitted against those renders. On material held
//! out from the fitting, sounds the target removes end up about 3.5 dB louder
//! than in the target (median), where the model alone leaves them about
//! 9.5 dB louder. `DEPTH_DB` keeps long noisy stretches from being pushed
//! further down than the target does.
//!
//! The model also turns down the decaying end of a word or a sentence, and
//! deepening that made speech trail off. So, as cppod's breath fix does,
//! the stage only goes for what is clearly not voice: nothing is deepened
//! for 40 ms after speech, an airy sound (a breath) is deepened after that,
//! and anything else only once the speech has been gone for 250 ms. On a
//! test video this kept the worst 10 % of sentence endings within 3.3 dB of
//! the original (4.8 dB without it), while still taking out most breaths.

const KNEE_DB: f32 = -6.0;
const EASE: f32 = 0.3;
const SLOPE: f32 = 3.0;
const DEPTH_DB: f32 = 20.0;
const ATTACK_SECONDS: f32 = 0.010;
/// After speech the model hears clearly, deepening waits HOLD_SECONDS and
/// then fades in over FADE_SECONDS, so the decaying end of a word or a
/// sentence (which the model also turns down) keeps its natural fade.
const HOLD_SECONDS: f32 = 0.25;
/// Right after speech nothing is deepened for GUARD_SECONDS (a word-final
/// s or t); after that, an airy sound (a breath: more energy at 1.5-7 kHz
/// than at 200-1200 Hz, as cppod's breath fix judges it) is deepened even
/// within the hold, while a voiced tail waits for the hold to pass.
const GUARD_SECONDS: f32 = 0.04;
const AIRY_FROM_DB: f32 = -3.0;
const AIRY_TO_DB: f32 = 1.0;
const FADE_SECONDS: f32 = 0.15;
const RELEASE_SECONDS: f32 = 0.040;
const BAND_LOW_HZ: f32 = 250.0;
const BAND_HIGH_HZ: f32 = 5000.0;

/// Extra gain in dB for a smoothed model attenuation of `model_db` (<= 0).
fn curve(model_db: f32) -> f32 {
    ease(model_db) + deepen(model_db)
}

/// The easing part of the curve: mild attenuation is given back.
fn ease(model_db: f32) -> f32 {
    -EASE * model_db.max(KNEE_DB)
}

/// The deepening part of the curve (<= 0), beyond the knee.
fn deepen(model_db: f32) -> f32 {
    if model_db >= KNEE_DB {
        0.0
    } else {
        (SLOPE * (model_db - KNEE_DB)).max(-DEPTH_DB)
    }
}

/// Second-order section (RBJ cookbook, Butterworth Q).
#[derive(Clone)]
struct Biquad {
    b: [f32; 3],
    a: [f32; 2],
    z: [f32; 2],
}

impl Biquad {
    fn new(sample_rate: f32, freq: f32, highpass: bool) -> Self {
        let w = 2.0 * std::f32::consts::PI * freq / sample_rate;
        let (sin, cos) = w.sin_cos();
        let alpha = sin / std::f32::consts::SQRT_2;
        let a0 = 1.0 + alpha;
        let b = if highpass {
            [(1.0 + cos) / 2.0, -(1.0 + cos), (1.0 + cos) / 2.0]
        } else {
            [(1.0 - cos) / 2.0, 1.0 - cos, (1.0 - cos) / 2.0]
        };
        Biquad {
            b: [b[0] / a0, b[1] / a0, b[2] / a0],
            a: [-2.0 * cos / a0, (1.0 - alpha) / a0],
            z: [0.0; 2],
        }
    }

    fn run(&mut self, x: f32) -> f32 {
        let y = self.b[0] * x + self.z[0];
        self.z[0] = self.b[1] * x - self.a[0] * y + self.z[1];
        self.z[1] = self.b[2] * x - self.a[1] * y;
        y
    }
}

#[derive(Clone)]
struct Band {
    high: Biquad,
    low: Biquad,
}

impl Band {
    fn new(sample_rate: f32) -> Self {
        Self::between(sample_rate, BAND_LOW_HZ, BAND_HIGH_HZ)
    }

    fn between(sample_rate: f32, low_hz: f32, high_hz: f32) -> Self {
        Band {
            high: Biquad::new(sample_rate, low_hz, true),
            low: Biquad::new(sample_rate, high_hz, false),
        }
    }

    /// Energy of `frame` inside the band.
    fn energy(&mut self, frame: &[f32]) -> f32 {
        frame
            .iter()
            .map(|&x| {
                let y = self.low.run(self.high.run(x));
                y * y
            })
            .sum()
    }
}

pub struct Contrast {
    ch: usize,
    hop: usize,
    before: Vec<Band>,
    after: Vec<Band>,
    attack: f32,
    release: f32,
    model_db: f32,
    gain: f32,
    hold_frames: f32,
    fade_frames: f32,
    guard_frames: f32,
    since_voice: f32,
    voice_band: Vec<Band>,
    air_band: Vec<Band>,
}

impl Contrast {
    pub fn new(ch: usize, hop: usize, sample_rate: f32) -> Self {
        let frames_per_second = sample_rate / hop as f32;
        Contrast {
            ch,
            hop,
            before: vec![Band::new(sample_rate); ch],
            after: vec![Band::new(sample_rate); ch],
            attack: (-1.0 / (ATTACK_SECONDS * frames_per_second)).exp(),
            release: (-1.0 / (RELEASE_SECONDS * frames_per_second)).exp(),
            model_db: 0.0,
            gain: 1.0,
            hold_frames: HOLD_SECONDS * frames_per_second,
            fade_frames: (FADE_SECONDS * frames_per_second).max(1.0),
            guard_frames: GUARD_SECONDS * frames_per_second,
            since_voice: 0.0,
            voice_band: vec![Band::between(sample_rate, 200.0, 1200.0); ch],
            air_band: vec![Band::between(sample_rate, 1500.0, 7000.0); ch],
        }
    }

    /// Processes one frame of model output in place. `input` is the model's
    /// input delayed to line up with `output`; both hold `ch * hop` samples,
    /// channel after channel. All channels get the same gain.
    pub fn process(&mut self, input: &[f32], output: &mut [f32]) {
        let hop = self.hop;
        let mut e_in = 1e-12f32;
        let mut e_out = 1e-12f32;
        let mut e_voice = 1e-12f32;
        let mut e_air = 1e-12f32;
        for c in 0..self.ch {
            let frame = &input[c * hop..(c + 1) * hop];
            e_in += self.before[c].energy(frame);
            e_out += self.after[c].energy(&output[c * hop..(c + 1) * hop]);
            e_voice += self.voice_band[c].energy(frame);
            e_air += self.air_band[c].energy(frame);
        }
        let airiness = ((10.0 * (e_air / e_voice).log10() - AIRY_FROM_DB) / (AIRY_TO_DB - AIRY_FROM_DB)).clamp(0.0, 1.0);
        let model_db = (10.0 * (e_out / e_in).log10()).min(0.0);
        let k = if model_db < self.model_db { self.attack } else { self.release };
        self.model_db = k * self.model_db + (1.0 - k) * model_db;

        self.since_voice = if self.model_db >= KNEE_DB { 0.0 } else { self.since_voice + 1.0 };
        let after_hold = ((self.since_voice - self.hold_frames) / self.fade_frames).clamp(0.0, 1.0);
        let past_guard = if self.since_voice > self.guard_frames { 1.0 } else { 0.0 };
        let deepening = after_hold.max(airiness * past_guard);
        let target = 10f32.powf((ease(self.model_db) + deepening * deepen(self.model_db)) / 20.0);
        for i in 0..hop {
            let g = self.gain + (target - self.gain) * ((i + 1) as f32 / hop as f32);
            for c in 0..self.ch {
                output[c * hop + i] *= g;
            }
        }
        self.gain = target;
    }
}

#[cfg(test)]
mod tests {
    use super::curve;

    #[test]
    fn curve_shape() {
        assert_eq!(curve(0.0), 0.0);
        assert!((curve(-3.0) - 0.9).abs() < 1e-6);
        assert!((curve(-6.0) - 1.8).abs() < 1e-6);
        assert!((curve(-10.0) - (1.8 - 12.0)).abs() < 1e-5);
        assert!((curve(-60.0) - (1.8 - 20.0)).abs() < 1e-5);
    }
}
