/* ── Guided tour — interactive walkthrough overlay ── */

import { $ } from "./state.js";

(function () {
  const TOUR_STEPS = [
    {
      sel: ".main-header",
      title: "Header Bar",
      body: "The header is your command centre. It holds device controls, quick-action buttons, connection status, and panel toggles."
    },
    {
      sel: "#btnAddDevice",
      title: "Add a Device",
      body: "Click the <strong>+</strong> button to register a new TheaterGWD device by entering its IP address and OSC port."
    },
    {
      sel: ".hdr-query",
      title: "Query",
      body: "After selecting a device tab, click <strong>Query</strong> to ask the device for its current messages, scenes, and settings."
    },
    {
      sel: ".hdr-port-box",
      title: "Listen Port",
      body: "This is the UDP port the software listens on for OSC replies from your devices. Change it and click <strong>Apply</strong>."
    },
    {
      sel: "#btnBlackoutAll",
      title: "Blackout",
      body: "Emergency stop — immediately halts all OSC output on every connected device. Use <strong>Restore</strong> to resume."
    },
    {
      sel: ".section-nav",
      title: "Main Tabs",
      body: "Switch between the main sections: <strong>Messages</strong>, <strong>Scenes</strong>, <strong>Ori</strong>, <strong>Shows</strong>, and <strong>Advanced</strong>."
    },
    {
      sel: '.nav-btn[data-section="messages"]',
      title: "Messages",
      body: "Define what the device sends — map a sensor reading to an OSC destination (IP, port, address) and set output range scaling."
    },
    {
      sel: '.nav-btn[data-section="scenes"]',
      title: "Scenes",
      body: "Group messages together with shared timing and addressing. Start, stop, or solo scenes to control what streams."
    },
    {
      sel: '.nav-btn[data-section="direct"]',
      title: "Direct",
      body: "The fastest path — pick a sensor, enter a target, and click Send. Gooey creates a message, scene, and starts streaming in one step."
    },
    {
      sel: '.nav-btn[data-section="shows"]',
      title: "Shows",
      body: "Save and load complete device configurations. Use shows to swap between performance setups instantly."
    },
    {
      sel: '.nav-btn[data-section="advanced"]',
      title: "Advanced",
      body: "Power-user tools: raw OSC sends, additional sensor callibration, and hidden tabs like Python scripting and control from mobile devices."
    },
    {
      sel: ".hdr-tools",
      title: "Panel Toggles",
      body: "Open or close the right-side panels: <strong>Feed</strong> (live traffic), <strong>Serial</strong> (USB monitor), <strong>Notifs</strong> (history), and <strong>Ref</strong> (command reference)."
    },
    {
      sel: "#btnFeedToggle",
      title: "Live Feed",
      body: "Toggle the live feed panel. It shows every OSC message sent and received in real time — great for debugging."
    },
    {
      sel: "#btnRefToggle",
      title: "Reference Panel",
      body: "Open the searchable reference for all OSC commands, sensor keywords, config keys, and address modes. You can restart this tour from there."
    }
  ];

  let _backdrop  = null;
  let _spotlight  = null;
  let _tooltip    = null;
  let _current    = -1;
  let _active     = false;

  function createOverlay() {
    if (_backdrop) return;
    _backdrop = document.createElement("div");
    _backdrop.className = "tour-backdrop";
    _backdrop.addEventListener("click", endTour);

    _spotlight = document.createElement("div");
    _spotlight.className = "tour-spotlight";

    _tooltip = document.createElement("div");
    _tooltip.className = "tour-tooltip";

    document.body.appendChild(_backdrop);
    document.body.appendChild(_spotlight);
    document.body.appendChild(_tooltip);
  }

  function removeOverlay() {
    if (_backdrop) { _backdrop.remove(); _backdrop = null; }
    if (_spotlight) { _spotlight.remove(); _spotlight = null; }
    if (_tooltip)  { _tooltip.remove();  _tooltip = null; }
  }

  function showStep(index) {
    if (index < 0 || index >= TOUR_STEPS.length) { endTour(); return; }
    _current = index;
    const step = TOUR_STEPS[index];
    const el = document.querySelector(step.sel);

    if (!el) {
      /* Skip missing elements */
      if (index < TOUR_STEPS.length - 1) showStep(index + 1);
      else endTour();
      return;
    }

    const pad = 6;
    const rect = el.getBoundingClientRect();

    /* Position spotlight */
    _spotlight.style.top    = (rect.top - pad) + "px";
    _spotlight.style.left   = (rect.left - pad) + "px";
    _spotlight.style.width  = (rect.width + pad * 2) + "px";
    _spotlight.style.height = (rect.height + pad * 2) + "px";

    /* Build dots */
    let dots = "";
    for (let d = 0; d < TOUR_STEPS.length; d++) {
      dots += '<span class="tour-dot' + (d === index ? " active" : "") + '"></span>';
    }

    /* Build tooltip content */
    _tooltip.innerHTML =
      '<div class="tour-tooltip-title">' + step.title + '</div>' +
      '<div class="tour-tooltip-body">' + step.body + '</div>' +
      '<div class="tour-tooltip-footer">' +
        '<div class="tour-progress">' + dots + '</div>' +
        '<div class="tour-tooltip-btns">' +
          (index > 0 ? '<button class="tour-btn" id="tourPrev">Back</button>' : '') +
          (index < TOUR_STEPS.length - 1
            ? '<button class="tour-btn tour-btn-primary" id="tourNext">Next</button>'
            : '<button class="tour-btn tour-btn-primary" id="tourDone">Done</button>') +
        '</div>' +
      '</div>';

    /* Wire button events */
    const btnPrev = _tooltip.querySelector("#tourPrev");
    const btnNext = _tooltip.querySelector("#tourNext");
    const btnDone = _tooltip.querySelector("#tourDone");
    if (btnPrev) btnPrev.addEventListener("click", function (e) { e.stopPropagation(); showStep(_current - 1); });
    if (btnNext) btnNext.addEventListener("click", function (e) { e.stopPropagation(); showStep(_current + 1); });
    if (btnDone) btnDone.addEventListener("click", function (e) { e.stopPropagation(); endTour(); });

    /* Position tooltip — prefer below the element, fall back to above */
    const tw = _tooltip.offsetWidth || 320;
    const th = _tooltip.offsetHeight || 200;
    const gap = 12;
    let tx, ty;

    /* Below */
    ty = rect.bottom + gap;
    if (ty + th > window.innerHeight) {
      /* Above */
      ty = rect.top - gap - th;
      if (ty < 0) ty = 10;
    }
    tx = rect.left + rect.width / 2 - tw / 2;
    if (tx < 10) tx = 10;
    if (tx + tw > window.innerWidth - 10) tx = window.innerWidth - tw - 10;

    _tooltip.style.top  = ty + "px";
    _tooltip.style.left = tx + "px";

    /* Scroll element into view if needed */
    if (rect.top < 0 || rect.bottom > window.innerHeight) {
      el.scrollIntoView({ behavior: "smooth", block: "center" });
      setTimeout(function () { showStep(index); }, 350);
    }
  }

  function startTour() {
    if (_active) return;
    _active = true;
    createOverlay();
    showStep(0);
  }

  function endTour() {
    _active = false;
    _current = -1;
    removeOverlay();
  }

  /* Keyboard navigation */
  document.addEventListener("keydown", function (e) {
    if (!_active) return;
    if (e.key === "Escape") { endTour(); }
    else if (e.key === "ArrowRight" || e.key === "ArrowDown") { e.preventDefault(); showStep(_current + 1); }
    else if (e.key === "ArrowLeft" || e.key === "ArrowUp")    { e.preventDefault(); showStep(_current - 1); }
  });

  /* Launch buttons */
  const btnRef = $("#btnStartTour");
  if (btnRef) btnRef.addEventListener("click", startTour);

  /* Guide button — open /docs/ in new tab (browser) or navigate in-app (Tauri) */
  const btnGuide = $("#btnGuide");
  if (btnGuide) btnGuide.addEventListener("click", function () {
    const url = location.origin + "/docs/";
    window.open(url, "_blank");
  });

  const btnOnboard = $("#btnOnboardTour");
  if (btnOnboard) btnOnboard.addEventListener("click", startTour);

  /* Expose for demo-mode auto-start */
  window._gooeyTour = { start: startTour, end: endTour };
}());
