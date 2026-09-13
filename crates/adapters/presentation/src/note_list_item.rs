//! [`NoteListItem`]: one row in the middle-pane note list.

/// One row in the middle-pane note list.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NoteListItem {
    pub id: String,
    pub folder_id: String,
    pub title: String,
    pub preview: String,
    pub updated_at: String,
    pub selected: bool,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn list_item_is_plain_display_data() {
        let item = NoteListItem {
            id: "n1".into(),
            folder_id: "f1".into(),
            title: "shopping".into(),
            preview: "milk, eggs".into(),
            updated_at: "2026-09-13T12:00:00Z".into(),
            selected: true,
        };
        assert!(item.selected);
        assert_eq!(item.title, "shopping");
    }
}
