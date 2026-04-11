# Tailwind + daisyUI Piece-by-Piece Migration Plan

## 1) Current Baseline (what already exists)

- Tailwind CSS + daisyUI are already installed and buildable via:
  - `npm run css:build`
  - `npm run css:watch`
- `app/templates/index.html` and `app/templates/docs.html` load both:
  - `app/static/css/output.css` (Tailwind/daisyUI output)
  - `app/static/css/style.css` (legacy custom stylesheet)
- `app/templates/remote.html` loads `output.css` but still uses a large inline `<style>` block.
- The main app UI still relies heavily on legacy semantic classes (`.main-header`, `.card`, `.nav-btn`, `.modal-*`, etc.) defined in `style.css`.

---

## 2) Migration Goals

1. Migrate styling incrementally without breaking existing UI behavior.
2. Replace legacy component styles with Tailwind utilities + daisyUI components.
3. Keep JavaScript behavior stable by preserving IDs and JS hook classes.
4. Reduce and eventually retire `app/static/css/style.css`.
5. Keep visual parity (light/dark themes, spacing, hierarchy, interaction states).

---

## 3) Migration Guardrails (must-follow)

- **Do not change JS selectors/IDs** used by modules unless done in a dedicated JS-safe step.
- **Migrate by UI slice**, not by file-wide rewrite.
- **One component family at a time** (buttons, cards, forms, modals, tables, nav, panels).
- **Dual-style period is expected**: old and new classes coexist temporarily.
- **Use feature-safe PR slices**: each slice should be testable and reversible.
- **No visual regressions** for:
  - device tab workflow
  - message/scene create-edit flows
  - modal interaction
  - feed/notif/reference panels
  - dark mode toggle behavior

---

## 4) Phase-by-Phase Rollout

## Phase 0 — Foundation and standards

### Scope
- Stabilize Tailwind/daisyUI setup as the source of truth for new styles.
- Define naming and usage conventions.

### Tasks
- Confirm `input.css` holds:
  - `@import "tailwindcss";`
  - `@plugin "daisyui";`
  - theme tokens (`gooey`, `gooey-dark`)
- Add migration conventions in docs:
  - when to use daisyUI classes (`btn`, `card`, `input`, `select`, `modal`, etc.)
  - when to use Tailwind utilities directly
  - how to keep JS hooks separate from visual classes
- Define token mapping from legacy CSS variables to daisyUI theme tokens.

### Exit criteria
- Team has a documented “how we style now” rule set.
- New UI work stops adding styles to `style.css`.

---

## Phase 1 — Primitive component layer (low risk, high reuse)

### Scope
- Build reusable primitive class patterns in markup using Tailwind/daisyUI.

### Tasks
- Migrate shared primitives used across pages:
  - buttons (`.btn*` variants)
  - text/number inputs
  - selects
  - badges/chips
  - cards
  - section titles
  - empty-state blocks
- For each primitive:
  - introduce new classes in one controlled location
  - keep old class as fallback during transition
- Start removing matching legacy primitive rules from `style.css` only after usage is replaced.

### Exit criteria
- Core primitives render from Tailwind/daisyUI definitions.
- No new primitive styling depends on legacy CSS.

---

## Phase 2 — Layout shell migration (header + split layout)

### Scope
- Migrate the structural shell of `index.html` without touching business logic.

### Tasks
- Convert:
  - main header rows and strips
  - device tab/action cluster
  - panel tool buttons
  - split left/right panel container
- Use responsive utilities for width/overflow behavior currently encoded in legacy CSS.
- Preserve all IDs, `data-*`, and event-targeted elements.

### Exit criteria
- App shell no longer depends on legacy layout selectors for core structure.
- Header interactions and panel toggles behave exactly as before.

---

## Phase 3 — Main page section-by-section migration (`index.html`)

### Order (piece-by-piece)
1. **Messages section**
2. **Scenes section**
3. **Ori section**
4. **Shows section**
5. **Advanced section**
6. **Script section**
7. **Right-side panels** (Feed, Notifications, Reference, Serial)

### Tasks per section
- Migrate forms, tables, action rows, helper text, chips, and in-section cards.
- Replace legacy styling classes with daisyUI/Tailwind equivalents.
- Remove only the now-unused legacy rules from `style.css` after each section is complete.

### Exit criteria per section
- Section renders correctly in light/dark.
- Keyboard focus and hover/active states remain clear.
- No JS behavior change.

---

## Phase 4 — Modal and overlay system

### Scope
- Migrate all modal-like elements to daisyUI-compatible modal patterns while preserving existing JS open/close behavior.

### Targets
- Device actions dropdown and overlays
- Device config modal
- Tare/calibration modal
- Confirmation modal
- Any other overlay panel in `index.html`

### Exit criteria
- All modal/overlay visuals sourced from Tailwind/daisyUI classes.
- Accessibility baseline preserved (`role`, `aria-*`, focus handling behavior as currently implemented).

---

## Phase 5 — Secondary templates

### 5A: `docs.html`
- Migrate docs header/sidebar/content/footer styling to Tailwind/daisyUI.
- Ensure TOC active/highlight behavior remains intact.

### 5B: `remote.html`
- Replace inline `<style>` system with Tailwind/daisyUI classes.
- Keep mobile/touch ergonomics unchanged (button size, spacing, scroll behavior).

### Exit criteria
- `docs.html` and `remote.html` are no longer dependent on legacy/global style blocks for primary layout and components.

---

## Phase 6 — Cleanup and deprecation

### Tasks
- Audit `style.css` for unused selectors after each completed phase.
- Remove dead CSS in controlled chunks.
- Keep only unavoidable custom CSS (if any), moved into:
  - `input.css` component layers, or
  - a much smaller focused legacy-compat file.
- Decide on bootstrap-icons strategy:
  - keep CDN as-is, or
  - replace with a consistent icon pipeline later.

### Exit criteria
- `style.css` reduced to minimal residual rules or fully retired.
- Tailwind/daisyUI is the primary styling system.

---

## 5) Suggested PR Breakdown (for safe delivery)

1. PR 1: Foundation docs + styling conventions
2. PR 2: Primitive components
3. PR 3: Header + shell layout
4. PR 4: Messages section
5. PR 5: Scenes + Ori sections
6. PR 6: Shows + Advanced + Script sections
7. PR 7: Modals/overlays
8. PR 8: Docs page migration
9. PR 9: Remote page migration
10. PR 10: CSS cleanup/final removal pass

Each PR should include:
- before/after screenshots (light + dark)
- selector removal list from `style.css`
- explicit regression checklist run

---

## 6) Regression Checklist (run every migration slice)

- Device add/edit/remove flows
- Query all devices + specific device behavior
- Message CRUD + bulk actions
- Scene CRUD + control actions
- Ori and shows actions
- Feed/notif/reference panel toggles
- Serial panel visibility and layout
- Modal open/close + confirm/cancel flows
- Dark mode toggle + persisted theme
- Responsive behavior in narrow and wide viewports

---

## 7) Definition of Done (final)

- Tailwind + daisyUI classes style all production templates (`index.html`, `docs.html`, `remote.html`).
- Legacy `style.css` is removed or reduced to a tiny compatibility subset with clear rationale.
- No behavioral regressions in JS-driven interactions.
- Light/dark themes remain fully functional and visually coherent.
- Migration docs are updated so future UI work continues in Tailwind/daisyUI only.

