//! Persistent configuration for repeatable GoogleTest checks.
//!
//! A manifest identifies a CMake test target rather than a full executable
//! path. This lets one command select a baseline and candidate build directory
//! while retaining the same test and default filter.

use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

const GTEST_ADAPTER: &str = "gtest";

/// TOML document consumed by `rustytwin init`, `doctor`, and `check --module`.
#[derive(Debug, Deserialize, Serialize)]
pub struct ModuleManifest {
    /// Adapter-specific test configuration.
    pub module: ModuleConfig,
    /// Default baseline build directory. Command-line input takes precedence.
    #[serde(default)]
    pub baseline: BuildConfig,
    /// Default candidate build directory. Command-line input takes precedence.
    #[serde(default)]
    pub candidate: BuildConfig,
}

/// Settings shared by both sides of a module check.
#[derive(Debug, Deserialize, Serialize)]
pub struct ModuleConfig {
    /// Adapter name. The current manifest adapter is `gtest`.
    pub adapter: String,
    /// CMake target and expected executable name below each build directory.
    pub test_target: String,
    /// Optional default GoogleTest filter. No filter runs the full binary once.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub filter: Option<String>,
    /// Optional per-test-process timeout in milliseconds.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub timeout_ms: Option<u64>,
}

/// Build-directory settings for one side of a comparison.
#[derive(Debug, Default, Deserialize, Serialize)]
pub struct BuildConfig {
    /// CMake build directory containing the configured test executable.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub build_dir: Option<PathBuf>,
}

impl ModuleManifest {
    /// Load and validate a manifest from TOML.
    pub fn load(path: &Path) -> Result<Self, String> {
        let contents = fs::read_to_string(path).map_err(|error| {
            format!("could not read module manifest {}: {error}", path.display())
        })?;
        let manifest: Self = toml::from_str(&contents).map_err(|error| {
            format!(
                "could not parse module manifest {}: {error}",
                path.display()
            )
        })?;
        manifest.validate()?;
        Ok(manifest)
    }

    /// Create a new GoogleTest manifest without overwriting an existing file.
    pub fn create(
        path: &Path,
        test_target: String,
        baseline_build: Option<PathBuf>,
        candidate_build: Option<PathBuf>,
        filter: Option<String>,
        timeout_ms: Option<u64>,
    ) -> Result<(), String> {
        if path.exists() {
            return Err(format!(
                "module manifest already exists: {} (use a different path or remove it first)",
                path.display()
            ));
        }
        let manifest = Self {
            module: ModuleConfig {
                adapter: GTEST_ADAPTER.to_owned(),
                test_target,
                filter,
                timeout_ms,
            },
            baseline: BuildConfig {
                build_dir: baseline_build,
            },
            candidate: BuildConfig {
                build_dir: candidate_build,
            },
        };
        manifest.validate()?;
        let contents = toml::to_string_pretty(&manifest)
            .map_err(|error| format!("could not serialize module manifest: {error}"))?;
        if let Some(parent) = path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
        {
            fs::create_dir_all(parent).map_err(|error| {
                format!(
                    "could not create module manifest directory {}: {error}",
                    parent.display()
                )
            })?;
        }
        fs::write(path, contents).map_err(|error| {
            format!(
                "could not write module manifest {}: {error}",
                path.display()
            )
        })
    }

    /// Validate adapter support and values that can be checked independently
    /// of a particular build directory.
    pub fn validate(&self) -> Result<(), String> {
        if self.module.adapter != GTEST_ADAPTER {
            return Err(format!(
                "unsupported module adapter {:?}; supported adapters: {GTEST_ADAPTER}",
                self.module.adapter
            ));
        }
        if self.module.test_target.trim().is_empty() {
            return Err("module test_target must not be empty".to_owned());
        }
        if Path::new(&self.module.test_target).is_absolute() {
            return Err(
                "module test_target must be a target name, not an absolute path".to_owned(),
            );
        }
        if let Some(filter) = &self.module.filter {
            if filter.trim().is_empty() {
                return Err("module filter must not be empty".to_owned());
            }
        }
        Ok(())
    }

    /// Resolve a build directory, preferring a command-line override over the
    /// manifest's default for the requested side.
    pub fn build_dir(
        &self,
        role: BuildRole,
        override_dir: Option<PathBuf>,
    ) -> Result<PathBuf, String> {
        override_dir
            .or_else(|| match role {
                BuildRole::Baseline => self.baseline.build_dir.clone(),
                BuildRole::Candidate => self.candidate.build_dir.clone(),
            })
            .ok_or_else(|| {
                format!(
                    "{} build directory is required; pass {} or set {}.build_dir in the manifest",
                    role.label(),
                    role.option_name(),
                    role.label()
                )
            })
    }

    /// Derive the test executable path expected by the current adapter.
    pub fn test_path(&self, build_dir: &Path) -> PathBuf {
        build_dir.join(&self.module.test_target)
    }
}

/// The baseline or candidate side of a differential check.
#[derive(Clone, Copy)]
pub enum BuildRole {
    Baseline,
    Candidate,
}

impl BuildRole {
    /// Lowercase label used in manifest sections and human-facing diagnostics.
    pub fn label(self) -> &'static str {
        match self {
            Self::Baseline => "baseline",
            Self::Candidate => "candidate",
        }
    }

    /// Command-line flag that overrides this side's configured build directory.
    pub fn option_name(self) -> &'static str {
        match self {
            Self::Baseline => "--baseline-build",
            Self::Candidate => "--candidate-build",
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parses_a_gtest_manifest() {
        let manifest: ModuleManifest = toml::from_str(
            r#"
                [module]
                adapter = "gtest"
                test_target = "test_raft_quorum"
                filter = "RaftQuorumTest.HelperPredicates"

                [baseline]
                build_dir = "build-baseline"

                [candidate]
                build_dir = "build-candidate"
            "#,
        )
        .unwrap();

        manifest.validate().unwrap();
        assert_eq!(
            manifest.build_dir(BuildRole::Baseline, None).unwrap(),
            PathBuf::from("build-baseline")
        );
        assert_eq!(
            manifest.test_path(Path::new("build-baseline")),
            PathBuf::from("build-baseline/test_raft_quorum")
        );
    }

    #[test]
    fn rejects_an_empty_test_target() {
        let manifest = ModuleManifest {
            module: ModuleConfig {
                adapter: GTEST_ADAPTER.to_owned(),
                test_target: " ".to_owned(),
                filter: None,
                timeout_ms: None,
            },
            baseline: BuildConfig::default(),
            candidate: BuildConfig::default(),
        };

        assert!(manifest.validate().unwrap_err().contains("test_target"));
    }
}
