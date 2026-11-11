//! The PlumppuVox voice isolation engine, behind a small C API.
//!
//! Each frame passes through:
//!   1. level normalisation, so the model always sees speech at a sane level,
//!   2. DeepFilterNet3 at full strength,
//!   3. a contrast stage that deepens what the model judged not to be voice
//!      (breaths, clicks, noise between words) and eases off on speech,
//!   4. a dry/wet mix with the input, which is what the dial controls.
//!
//! Stages 2-4 reproduce the sound of the original one-knob tool, whose
//! settings between 0 and 100 were exact mixes of its fully processed output
//! with the untouched input.
//!
//! Everything that crosses the FFI boundary is wrapped in `catch_unwind`, so a
//! failure inside the model can never take the host down: the engine marks
//! itself as failed and the plugin falls back to passing audio through.

use std::panic::{catch_unwind, AssertUnwindSafe};
use std::slice;

use df::tract::{DfParams, DfTract, RuntimeParams};
use ndarray::{ArrayView2, ArrayViewMut2};

mod contrast;
mod level;

use contrast::Contrast;
use level::Leveler;

/// Delays the model input so it lines up with the model output.
struct DelayLine {
    ch: usize,
    hop: usize,
    buf: Vec<f32>, // per channel ring of `len` samples
    len: usize,
    write: usize,
}

impl DelayLine {
    fn new(ch: usize, hop: usize, delay: usize) -> Self {
        let len = delay + hop;
        DelayLine { ch, hop, buf: vec![0.0; ch * len], len, write: 0 }
    }

    /// Pushes one frame and writes the frame from `delay` samples ago to `out`.
    fn process(&mut self, frame: &[f32], out: &mut [f32]) {
        let (len, hop) = (self.len, self.hop);
        let delay = len - hop;
        for c in 0..self.ch {
            let ring = &mut self.buf[c * len..(c + 1) * len];
            for i in 0..hop {
                ring[(self.write + i) % len] = frame[c * hop + i];
            }
            for i in 0..hop {
                out[c * hop + i] = ring[(self.write + i + len - delay) % len];
            }
        }
        self.write = (self.write + hop) % len;
    }
}

pub struct PvEngine {
    model: DfTract,
    pristine: DfTract,
    leveler: Leveler,
    delay: DelayLine,
    contrast: Contrast,
    work: Vec<f32>,
    dry: Vec<f32>,
    dry_mix: f32,
    channels: usize,
    failed: bool,
}

fn model_latency(m: &DfTract) -> usize {
    m.fft_size - m.hop_size + m.lookahead * m.hop_size
}

impl PvEngine {
    fn fresh_state(&mut self) {
        let sample_rate = self.model.sr as f32;
        let (ch, hop, latency) = (self.channels, self.model.hop_size, model_latency(&self.model));
        self.leveler = Leveler::new(ch, hop, latency, sample_rate);
        self.delay = DelayLine::new(ch, hop, latency);
        self.contrast = Contrast::new(ch, hop, sample_rate);
    }
}

fn build(channels: usize) -> Option<PvEngine> {
    let params = DfParams::default();
    // DeepFilterNet estimates a local signal-to-noise ratio per frame and by
    // default takes shortcuts based on it: below -10 dB it mutes the frame
    // outright, above 30 dB it passes the frame through untouched, and above
    // 20 dB it skips its second (deep filtering) stage. All three are audible
    // (clipped word endings, noise riding along with loud speech), so the
    // thresholds are moved to the ends of the estimate's -15..35 dB range and
    // the model does its full work on every frame. The attenuation limit stays
    // off: the dry/wet mix is done here, after the contrast stage.
    let runtime = RuntimeParams::default_with_ch(channels)
        .with_thresholds(-15.0, 35.0, 35.0)
        .with_atten_lim(100.0);
    let model = DfTract::new(params, &runtime).ok()?;
    let (sample_rate, hop, latency) = (model.sr as f32, model.hop_size, model_latency(&model));
    Some(PvEngine {
        leveler: Leveler::new(channels, hop, latency, sample_rate),
        delay: DelayLine::new(channels, hop, latency),
        contrast: Contrast::new(channels, hop, sample_rate),
        work: vec![0.0; channels * hop],
        dry: vec![0.0; channels * hop],
        dry_mix: 0.0,
        pristine: model.clone(),
        model,
        channels,
        failed: false,
    })
}

/// Creates an engine for `channels` audio channels. Returns NULL on failure.
#[no_mangle]
pub extern "C" fn plumppuvox_create(channels: u32) -> *mut PvEngine {
    let channels = channels.max(1) as usize;
    match catch_unwind(|| build(channels)) {
        Ok(Some(engine)) => Box::into_raw(Box::new(engine)),
        _ => std::ptr::null_mut(),
    }
}

/// # Safety
/// `engine` must come from `plumppuvox_create` and must not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_free(engine: *mut PvEngine) {
    if !engine.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(engine))));
    }
}

/// Samples per channel consumed and produced by each `plumppuvox_process` call.
///
/// # Safety
/// `engine` must be a valid pointer from `plumppuvox_create`.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_hop_size(engine: *const PvEngine) -> u32 {
    (*engine).model.hop_size as u32
}

/// Sample rate the model runs at (48000 for DeepFilterNet3).
///
/// # Safety
/// `engine` must be a valid pointer from `plumppuvox_create`.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_sample_rate(engine: *const PvEngine) -> u32 {
    (*engine).model.sr as u32
}

/// Total delay in samples (model filter bank plus lookahead).
///
/// # Safety
/// `engine` must be a valid pointer from `plumppuvox_create`.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_latency(engine: *const PvEngine) -> u32 {
    model_latency(&(*engine).model) as u32
}

/// Clears all streaming state (used when playback restarts).
///
/// # Safety
/// `engine` must be a valid pointer from `plumppuvox_create`.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_reset(engine: *mut PvEngine) {
    let e = &mut *engine;
    let ok = catch_unwind(AssertUnwindSafe(|| {
        e.model = e.pristine.clone();
        e.fresh_state();
    }));
    e.failed = ok.is_err();
}

/// Sets how much of the untouched input is mixed into the output: 1 passes
/// the input through unchanged (delayed by the latency), 0 is the fully
/// processed voice.
///
/// # Safety
/// `engine` must be a valid pointer from `plumppuvox_create`.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_set_dry_mix(engine: *mut PvEngine, dry: f32) {
    (*engine).dry_mix = dry.clamp(0.0, 1.0);
}

/// Processes one frame. `input` and `output` hold `channels * hop_size`
/// samples, channel after channel. Returns 1 on success; on failure returns 0
/// and leaves `output` untouched.
///
/// # Safety
/// `engine` must be valid and both buffers must hold `channels * hop_size`
/// samples.
#[no_mangle]
pub unsafe extern "C" fn plumppuvox_process(
    engine: *mut PvEngine,
    input: *const f32,
    output: *mut f32,
) -> i32 {
    let e = &mut *engine;
    if e.failed {
        return 0;
    }
    let hop = e.model.hop_size;
    let n = e.channels * hop;
    let input = slice::from_raw_parts(input, n);
    let output = slice::from_raw_parts_mut(output, n);
    let shape = (e.channels, hop);
    let dry_mix = e.dry_mix;
    let (model, leveler, delay, contrast, work, dry) =
        (&mut e.model, &mut e.leveler, &mut e.delay, &mut e.contrast, &mut e.work, &mut e.dry);
    let result = catch_unwind(AssertUnwindSafe(|| {
        work.copy_from_slice(input);
        leveler.apply(work);
        delay.process(work, dry);
        {
            let noisy = ArrayView2::from_shape(shape, &work[..]).ok()?;
            let enhanced = ArrayViewMut2::from_shape(shape, &mut output[..]).ok()?;
            model.process(noisy, enhanced).ok()?;
        }
        contrast.process(dry, output);
        let wet_mix = 1.0 - dry_mix;
        for (o, d) in output.iter_mut().zip(dry.iter()) {
            *o = wet_mix * *o + dry_mix * *d;
        }
        leveler.undo(output);
        Some(())
    }));
    match result {
        Ok(Some(_)) => 1,
        _ => {
            e.failed = true;
            0
        }
    }
}
