//! RustyTwin compares the observable behavior of a baseline C++ harness and
//! a RustyCpp-migrated candidate harness.
//!
//! The crate is layered deliberately: [`module`] resolves repeatable
//! GoogleTest checks, [`runner`] executes each side, [`compare`] finds the
//! first observable difference, and [`replay`] retains failures for later
//! inspection.

pub mod compare;
pub mod module;
pub mod protocol;
pub mod replay;
pub mod report;
pub mod runner;

/// Version embedded in each replay artifact so its format can be interpreted
/// with the producing RustyTwin release.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
