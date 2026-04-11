/* ── Section nav & collapsible cards ── */

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

/* ── Collapsible cards ── */
export function initCollapsibleCards() {
  $$(".section .card").forEach(function (card) {
    if (card.id === "oriDetailsCard") return;
    const headers = card.querySelectorAll("h2, .card-title-row, .tbl-toolbar");
    headers.forEach(function (hdr) {
      hdr.addEventListener("click", function (e) {
        if (e.target.closest("button, input, select, textarea, a, .col-picker-wrap")) return;
        card.classList.toggle("card-collapsed");
      });
    });
  });
}

initCollapsibleCards();
