/* ── Section nav ── */

import { $, $$ } from "./state.js";

$$(".nav-btn[data-section]").forEach(function (btn) {
  btn.addEventListener("click", function () {
    $$(".nav-btn[data-section]").forEach(function (b) { b.classList.remove("active"); });
    $$(".section").forEach(function (s) { s.classList.remove("active"); });
    btn.classList.add("active");
    const sec = $("#sec-" + btn.dataset.section);
    if (sec) sec.classList.add("active");
  });
});
