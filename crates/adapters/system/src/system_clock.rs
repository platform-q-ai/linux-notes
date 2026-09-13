//! [`SystemClock`]: wall-clock implementation of the `Clock` port for the
//! running app.

use rusty_notes_application::ports::Clock;
use rusty_notes_domain::Timestamp;

/// Wall-clock implementation of the [`Clock`] port for the running app.
///
/// The desktop shell injects this at the composition root; tests inject
/// `test_support::fakes::FixedClock` instead.
pub struct SystemClock;

impl SystemClock {
    /// Formats unix seconds as `YYYY-MM-DDTHH:MM:SSZ` (proleptic Gregorian, UTC).
    fn rfc3339_from_unix(secs: u64) -> String {
        let days = secs / 86_400;
        let rem = secs % 86_400;
        let (h, m, s) = (rem / 3600, (rem % 3600) / 60, rem % 60);
        // Civil-from-days algorithm (Howard Hinnant), valid for the full u64 range.
        let z = days as i64 + 719_468;
        let era = z.div_euclid(146_097);
        let doe = z.rem_euclid(146_097);
        let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
        let y = yoe + era * 400;
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        let mp = (5 * doy + 2) / 153;
        let d = doy - (153 * mp + 2) / 5 + 1;
        let mth = if mp < 10 { mp + 3 } else { mp - 9 };
        let y = if mth <= 2 { y + 1 } else { y };
        format!("{y:04}-{mth:02}-{d:02}T{h:02}:{m:02}:{s:02}Z")
    }
}

impl Clock for SystemClock {
    fn now(&self) -> Timestamp {
        // Seconds-precision UTC RFC-3339 without external time crates: SQLite and
        // the contract only need a stable, lexicographically ordered format.
        let secs = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default()
            .as_secs();
        Timestamp(Self::rfc3339_from_unix(secs))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rfc3339_format_is_pinned_and_lexicographically_ordered() {
        assert_eq!(SystemClock::rfc3339_from_unix(0), "1970-01-01T00:00:00Z");
        assert_eq!(
            SystemClock::rfc3339_from_unix(1_789_331_234),
            "2026-09-13T20:27:14Z"
        );
        // Lexicographic order equals chronological order (the property the
        // contract relies on for `updated_at` ordering).
        let earlier = SystemClock::rfc3339_from_unix(1_000_000_000);
        let later = SystemClock::rfc3339_from_unix(1_000_000_001);
        assert!(earlier < later);
    }

    #[test]
    fn now_returns_a_formatted_timestamp() {
        let stamp = SystemClock.now();
        // Seconds-precision RFC-3339 shape: `YYYY-MM-DDTHH:MM:SSZ` (20 chars).
        assert_eq!(stamp.0.len(), 20, "{}", stamp.0);
        assert_eq!(&stamp.0[4..5], "-");
        assert_eq!(&stamp.0[10..11], "T");
        assert!(stamp.0.ends_with('Z'));
    }
}
