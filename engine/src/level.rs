//! Input level normalisation.
//!
//! The denoising model was trained on speech at ordinary recording levels and
//! does noticeably worse on quiet material. The signal is brought to a
//! standard level before processing and the exact inverse gain is applied
//! afterwards, so the output level is unchanged.

/// Level the loudest recent passages are brought to (RMS, about -20 dBFS).
const TARGET: f32 = 0.1;
const MAX_GAIN: f32 = 16.0; // +24 dB
const MIN_GAIN: f32 = 0.25; // -12 dB
/// The level estimate follows loud passages at once and falls back slowly,
/// so the gain does not drift up during pauses between sentences.
const RELEASE_SECONDS: f32 = 20.0;

pub struct Leveler {
    ch: usize,
    hop: usize,
    level: f32,
    gain: f32,
    release: f32,
    /// Gains applied to past samples, for undoing them after the delay.
    applied: Vec<f32>,
    write: usize,
    delay: usize,
}

impl Leveler {
    pub fn new(ch: usize, hop: usize, delay: usize, sample_rate: f32) -> Self {
        let frames_per_second = sample_rate / hop as f32;
        Leveler {
            ch,
            hop,
            level: 0.0,
            gain: 1.0,
            release: (-1.0 / (RELEASE_SECONDS * frames_per_second)).exp(),
            applied: vec![1.0; delay + hop],
            write: 0,
            delay,
        }
    }

    /// Applies the gain to one frame (`ch * hop` samples) in place.
    pub fn apply(&mut self, frame: &mut [f32]) {
        let mut energy = 0.0f32;
        let mut peak = 0.0f32;
        for &x in frame.iter() {
            energy += x * x;
            peak = peak.max(x.abs());
        }
        let rms = (energy / frame.len() as f32).sqrt();
        self.level = rms.max(self.level * self.release);

        let mut wanted = if self.level > 1e-5 { TARGET / self.level } else { self.gain };
        wanted = wanted.clamp(MIN_GAIN, MAX_GAIN);
        // Move gently towards the wanted gain, but never let a frame clip.
        let mut next = self.gain + (wanted - self.gain) * if wanted < self.gain { 0.3 } else { 0.02 };
        if peak * next > 0.9 {
            next = (0.9 / peak).max(MIN_GAIN);
        }

        let len = self.applied.len();
        for i in 0..self.hop {
            let g = self.gain + (next - self.gain) * ((i + 1) as f32 / self.hop as f32);
            self.applied[(self.write + i) % len] = g;
            for c in 0..self.ch {
                frame[c * self.hop + i] *= g;
            }
        }
        self.gain = next;
    }

    /// Removes the gain again from a frame that has passed through the
    /// processing delay. Call once per `apply`, after it.
    pub fn undo(&mut self, frame: &mut [f32]) {
        let len = self.applied.len();
        for i in 0..self.hop {
            let g = self.applied[(self.write + i + len - self.delay) % len];
            for c in 0..self.ch {
                frame[c * self.hop + i] /= g;
            }
        }
        self.write = (self.write + self.hop) % len;
    }
}
