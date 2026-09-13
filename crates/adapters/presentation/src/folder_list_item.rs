//! [`FolderListItem`]: one row in the left-pane folder list.

/// One row in the left-pane folder list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct FolderListItem {
    pub id: String,
    pub name: String,
    pub note_count: usize,
    pub selected: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn folder_item_carries_count_and_selection() {
        let item = FolderListItem {
            id: "f1".into(),
            name: "Inbox".into(),
            note_count: 3,
            selected: false,
        };
        assert_eq!(item.note_count, 3);
        assert!(!item.selected);
    }
}
