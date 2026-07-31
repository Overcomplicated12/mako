//! RustyTwin compares the observable behavior of a baseline C++ harness and
//! a RustyCpp-migrated candidate harness.

pub mod compare;
pub mod module;
pub mod protocol;
pub mod replay;
pub mod report;
pub mod runner;

pub const VERSION: &str = env!("CARGO_PKG_VERSION");
