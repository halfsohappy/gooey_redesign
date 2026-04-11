/* ── Demo mode init ── */

import { showToast } from "./toast.js";
import { activeViews, updatePanelLayout } from "./panel-resize.js";

if (window.GOOEY_DEMO) {
  // Force light theme regardless of localStorage or system preference
  try { localStorage.removeItem("gooey-theme"); } catch (e) {}
  document.documentElement.classList.remove("dark");

  // Open Notifications panel alongside Feed
  activeViews.notifications = true;
  updatePanelLayout();

  // Seed getting-started steps as notifications (added in reverse so step 1 shows at top)
  showToast("Step 4: Start a scene \u2014 group messages together and click Start to begin streaming.", "info");
  showToast("Step 3: Create messages \u2014 map sensor streams to OSC addresses in the Messages tab.", "info");
  showToast("Step 2: Query it \u2014 select your device tab, then click \u27f3 Query to load its config.", "info");
  showToast("Step 1: Add a device \u2014 click + in the header to enter your device\u2019s IP and port.", "info");
  showToast("Welcome to the annieData demo \u2014 OSC sending is disabled. Explore freely!", "info");
}
