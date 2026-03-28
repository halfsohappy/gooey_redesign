// =============================================================================
// osc_pattern.h — OSC 1.0 address pattern matching
// =============================================================================
//
// Implements the pattern-matching rules from the OSC 1.0 specification:
//   *        — matches zero or more characters
//   ?        — matches exactly one character
//   [abc]    — matches any single character in the set
//   [a-z]    — matches any single character in the range
//   [!abc]   — matches any single character NOT in the set
//   {foo,bar} — matches any of the comma-separated alternatives
//
// All comparisons are case-insensitive.
// Stack-only — no heap allocation.
// =============================================================================

#ifndef OSC_PATTERN_H
#define OSC_PATTERN_H

#include <ctype.h>

// ---------------------------------------------------------------------------
// osc_has_pattern — fast check for pattern metacharacters
// ---------------------------------------------------------------------------
static inline bool osc_has_pattern(const char* s) {
    for (; *s; ++s) {
        char c = *s;
        if (c == '*' || c == '?' || c == '[' || c == '{') return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// osc_pattern_match — recursive descent OSC pattern matcher
// ---------------------------------------------------------------------------
// Returns true if `pattern` matches the entirety of `text`.
// Case-insensitive.  No heap allocation.
// ---------------------------------------------------------------------------

static bool osc_pattern_match(const char* pattern, const char* text) {
    while (*pattern) {
        char pc = *pattern;

        // ── * — match zero or more characters ─────────────────────────────
        if (pc == '*') {
            ++pattern;
            // Optimisation: trailing * matches everything remaining.
            if (*pattern == '\0') return true;
            // Try matching the rest of the pattern at every position.
            for (const char* t = text; ; ++t) {
                if (osc_pattern_match(pattern, t)) return true;
                if (*t == '\0') return false;
            }
        }

        // ── ? — match exactly one character ───────────────────────────────
        if (pc == '?') {
            if (*text == '\0') return false;
            ++pattern;
            ++text;
            continue;
        }

        // ── [charset] — match one character from set/range ────────────────
        if (pc == '[') {
            if (*text == '\0') return false;
            ++pattern;  // skip '['
            bool negate = false;
            if (*pattern == '!') {
                negate = true;
                ++pattern;
            }
            bool matched = false;
            char tc = tolower((unsigned char)*text);
            // Scan until closing ']' or end of string.
            while (*pattern && *pattern != ']') {
                char lo = tolower((unsigned char)*pattern);
                // Check for range: a-z
                if (pattern[1] == '-' && pattern[2] && pattern[2] != ']') {
                    char hi = tolower((unsigned char)pattern[2]);
                    if (tc >= lo && tc <= hi) matched = true;
                    pattern += 3;  // skip "a-z"
                } else {
                    if (tc == lo) matched = true;
                    ++pattern;
                }
            }
            if (*pattern == ']') ++pattern;  // skip ']'
            if (negate) matched = !matched;
            if (!matched) return false;
            ++text;
            continue;
        }

        // ── {alt1,alt2,...} — match any alternative ───────────────────────
        if (pc == '{') {
            ++pattern;  // skip '{'
            // Try each comma-separated alternative.
            const char* alt_start = pattern;
            int depth = 1;
            while (depth > 0) {
                // Find the end of this alternative (next ',' at depth 1, or '}').
                const char* alt_end = alt_start;
                while (*alt_end && !(depth == 1 && (*alt_end == ',' || *alt_end == '}'))) {
                    if (*alt_end == '{') ++depth;
                    else if (*alt_end == '}') { --depth; if (depth == 0) break; }
                    ++alt_end;
                }
                // alt_start..alt_end is one alternative.  Try to match it
                // against text, then continue with the rest of the pattern
                // after the closing '}'.
                // Find the closing '}' for the rest-of-pattern.
                const char* after_brace = alt_end;
                if (*alt_end == ',') {
                    // Need to find the real closing '}'.
                    int d = 1;
                    after_brace = alt_end + 1;
                    while (*after_brace && d > 0) {
                        if (*after_brace == '{') ++d;
                        else if (*after_brace == '}') --d;
                        if (d > 0) ++after_brace;
                    }
                }
                // after_brace now points at the closing '}'.
                // Build a virtual pattern: alternative + rest_after_brace.
                // Instead of allocating, we match the alternative against text
                // character by character, then try matching the rest.
                size_t alt_len = alt_end - alt_start;
                // Try: does this alternative + rest-of-pattern match text?
                // We do this by recursive matching of the alternative prefix,
                // then the remainder after '}'.
                // Construct a stack buffer for alt + rest.
                const char* rest = (*after_brace == '}') ? after_brace + 1 : after_brace;
                size_t rest_len = strlen(rest);
                // Stack buffer — names are short (typically < 64 chars).
                char buf[128];
                if (alt_len + rest_len < sizeof(buf)) {
                    memcpy(buf, alt_start, alt_len);
                    memcpy(buf + alt_len, rest, rest_len + 1);  // include NUL
                    if (osc_pattern_match(buf, text)) return true;
                }

                // Move to next alternative.
                if (*alt_end == ',') {
                    alt_start = alt_end + 1;
                    // depth is still 1, continue loop.
                } else {
                    // Hit '}' — no more alternatives.
                    break;
                }
            }
            return false;  // no alternative matched
        }

        // ── Literal character ─────────────────────────────────────────────
        if (*text == '\0') return false;
        if (tolower((unsigned char)pc) != tolower((unsigned char)*text)) return false;
        ++pattern;
        ++text;
    }

    return *text == '\0';
}

#endif // OSC_PATTERN_H
