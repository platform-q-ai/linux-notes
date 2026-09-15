// Headless presentation critical scenarios (Qt Test when linked).
// CI: QT_QPA_PLATFORM=offscreen. NOT visual smoke.
//
// Until EditorViewModel + domain stabilize, this binary is a compile/link
// canary documenting required scenarios. Full QObject tests land after task 5.

#include <iostream>

// Required scenarios (see delivery-evidence/tests/TEST_PLAN.md):
// 1. note switch during debounce — no cross-note write
// 2. stale async completion ignored — save_generation_ gate
// 3. save error keeps dirty + error saveState
// 4. save/reopen covered by note_store contracts (fake+sqlite)
// 5. deletion ordering with pending save

int main() {
  std::cerr
      << "presentation critical scenarios: PLACEHOLDER headless canary\n"
      << "wire EditorViewModel Qt Test once gen-gate fixed and task5 claimed\n";
  // Exit 0 only for canary presence; do not claim feature coverage.
  return 0;
}
