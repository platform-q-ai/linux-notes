//! Unique identifier of a note. Newtype so IDs cannot be confused with other strings.

use std::fmt;

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub struct NoteId(pub String);

impl NoteId {
    pub fn as_str(&self) -> &str {
        &self.0
    }
}

impl fmt::Display for NoteId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.0)
    }
}
