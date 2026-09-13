//! Unique identifier of a folder. Newtype so IDs cannot be confused with other strings.

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct FolderId(pub String);

impl FolderId {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for FolderId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}
