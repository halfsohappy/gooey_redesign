/* ── Hover tooltips: position fixed to escape overflow clipping ── */

import { $$ } from "./state.js";

$$(".tooltip-wrap").forEach(function (wrap) {
  wrap.addEventListener("mouseenter", function () {
    const body = wrap.querySelector(".tooltip-body");
    if (!body) return;
    const r = wrap.getBoundingClientRect();
    const h  = body.offsetHeight || 150;
    const vh = window.innerHeight || document.documentElement.clientHeight || 800;
    body.style.left = Math.max(8, r.right - 280) + "px";
    if (r.bottom + 6 + h > vh) {
      body.style.top  = "";
      body.style.bottom = (vh - r.top + 6) + "px";
    } else {
      body.style.bottom = "";
      body.style.top  = (r.bottom + 6) + "px";
    }
  });
});
