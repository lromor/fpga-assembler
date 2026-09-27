//! Discovery of the databases on disk, and the cache of the ones in use.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use fabric::{Database, PartInfo, Snapshot};
use serde::Serialize;

/// A family directory of a prjxray database, e.g. `artix7`, and the parts it
/// declares.
#[derive(Clone, Debug, Serialize)]
pub struct Family {
    pub name: String,
    pub parts: Vec<PartInfo>,
}

/// A database that has been opened, with its grid already read.
pub struct LoadedPart {
    pub family: String,
    pub part: String,
    pub database: Database,
    pub snapshot: Snapshot,
}

pub struct AppState {
    root: PathBuf,
    families: Vec<Family>,
    /// Opening a part costs seconds and hundreds of megabytes, so a part
    /// stays loaded once someone has looked at it.
    loaded: Mutex<HashMap<(String, String), Arc<LoadedPart>>>,
}

/// A family root is a directory that declares parts; anything else under the
/// database root (documentation, harness data) is skipped.
fn read_families(root: &Path) -> std::io::Result<Vec<Family>> {
    let mut families = Vec::new();
    for entry in std::fs::read_dir(root)? {
        let entry = entry?;
        if !entry.file_type()?.is_dir() {
            continue;
        }
        let path = entry.path();
        if !path.join("mapping").join("parts.yaml").is_file() {
            continue;
        }
        let Some(name) = entry.file_name().to_str().map(str::to_owned) else {
            continue;
        };
        match fabric::parts(&path) {
            Ok(parts) => families.push(Family { name, parts }),
            Err(error) => {
                tracing::warn!(family = %name, %error, "skipping a family whose mapping does not parse");
            }
        }
    }
    families.sort_by(|left, right| left.name.cmp(&right.name));
    Ok(families)
}

impl AppState {
    /// Scans `root` for family directories. Nothing is opened yet.
    pub fn new(root: PathBuf) -> std::io::Result<Self> {
        let families = read_families(&root)?;
        Ok(AppState {
            root,
            families,
            loaded: Mutex::new(HashMap::new()),
        })
    }

    pub fn families(&self) -> &[Family] {
        &self.families
    }

    /// True when the family declares the part, which is what keeps a request
    /// from reaching into an arbitrary directory.
    fn declares(&self, family: &str, part: &str) -> bool {
        self.families.iter().any(|candidate| {
            candidate.name == family && candidate.parts.iter().any(|known| known.name == part)
        })
    }

    /// Returns the loaded part, opening it if this is the first request for
    /// it. Blocks while the database is read, so callers run it on a
    /// blocking task.
    pub fn load(&self, family: &str, part: &str) -> Result<Arc<LoadedPart>, String> {
        if !self.declares(family, part) {
            return Err(format!("unknown part \"{part}\" in family \"{family}\""));
        }
        let key = (family.to_string(), part.to_string());
        if let Some(loaded) = self
            .loaded
            .lock()
            .expect("the cache lock is not poisoned")
            .get(&key)
        {
            return Ok(Arc::clone(loaded));
        }
        // Opened outside the lock: two requests for two different parts must
        // not queue behind each other for the seconds a load takes.
        let database =
            Database::open(&self.root.join(family), part).map_err(|error| error.to_string())?;
        let snapshot = database.snapshot();
        let loaded = Arc::new(LoadedPart {
            family: family.to_string(),
            part: part.to_string(),
            database,
            snapshot,
        });
        let mut cache = self.loaded.lock().expect("the cache lock is not poisoned");
        // Another request may have won the race; keep whichever landed first
        // so every caller shares one database.
        Ok(Arc::clone(cache.entry(key).or_insert(loaded)))
    }
}
