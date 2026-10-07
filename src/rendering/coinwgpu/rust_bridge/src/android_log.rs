//! Android diagnostics use logcat; desktop apps retain their own logger.
use std::ffi::CString;
use std::sync::Once;

struct AndroidLogger;
static LOGGER: AndroidLogger = AndroidLogger;
static INIT: Once = Once::new();

extern "C" {
    fn __android_log_write(priority: i32, tag: *const std::ffi::c_char,
                           text: *const std::ffi::c_char) -> i32;
}

impl log::Log for AndroidLogger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() <= log::max_level()
    }
    fn log(&self, record: &log::Record<'_>) {
        if !self.enabled(record.metadata()) { return; }
        let priority = match record.level() {
            log::Level::Error => 6, log::Level::Warn => 5,
            log::Level::Info => 4, log::Level::Debug => 3, log::Level::Trace => 2,
        };
        let message = format!("{}: {}", record.target(), record.args()).replace('\0', "?");
        if let Ok(message) = CString::new(message) {
            unsafe { __android_log_write(priority, b"CoinRenderWgpu\0".as_ptr().cast(), message.as_ptr()); }
        }
    }
    fn flush(&self) {}
}

pub fn init() {
    INIT.call_once(|| {
        // A host-owned logger takes precedence. Never replace its filter.
        if log::set_logger(&LOGGER).is_ok() {
            let level = match std::env::var("COIN_WGPU_LOG").as_deref() {
                Ok("debug") => log::LevelFilter::Debug,
                Ok("trace") => log::LevelFilter::Trace,
                Ok("info") => log::LevelFilter::Info,
                _ => log::LevelFilter::Warn,
            };
            log::set_max_level(level);
        }
    });
}
