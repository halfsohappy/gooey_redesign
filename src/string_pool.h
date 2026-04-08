// =============================================================================
// string_pool.h — Fixed-size pool of named string values (str1, str2, ...)
// =============================================================================
//
// OscMessages that send a fixed string (rather than a float sensor value)
// hold a pointer into this pool.  The pool is a Meyer's singleton with a
// fixed-size array, so pointers remain stable for the device's lifetime.
//
// Names are 1-based: values[0] is "str1", values[1] is "str2", etc.
// =============================================================================

#ifndef STRING_POOL_H
#define STRING_POOL_H

#include <Arduino.h>

#define MAX_STRINGS 16

class StringPool {
public:
    String  values[MAX_STRINGS];
    uint8_t count = 0;

    // Register a string.  Returns its 0-based index.
    // If the string already exists, returns its existing index.
    // Returns -1 if the pool is full.
    int register_string(const String& s) {
        for (uint8_t i = 0; i < count; i++) {
            if (values[i] == s) return (int)i;
        }
        if (count >= MAX_STRINGS) return -1;
        int idx = count;
        values[count++] = s;
        return idx;
    }

    // "str1" for index 0, "str2" for index 1, ...
    static String name_for_index(int idx) {
        return "str" + String(idx + 1);
    }

    // Parse "str1" → 0, "str2" → 1, ...  Returns -1 if not a valid name.
    int index_from_name(const String& name) const {
        String l = name; l.trim(); l.toLowerCase();
        if (l.startsWith("str") && l.length() > 3) {
            int n = l.substring(3).toInt();
            if (n >= 1 && n <= (int)count) return n - 1;
        }
        return -1;
    }

    // Return pointer to the String value for "str1" etc., or nullptr.
    String* ptr_from_name(const String& name) {
        int idx = index_from_name(name);
        if (idx < 0) return nullptr;
        return &values[idx];
    }

    void clear() {
        for (uint8_t i = 0; i < MAX_STRINGS; i++) values[i] = "";
        count = 0;
    }
};

static inline StringPool& string_pool() {
    static StringPool sp;
    return sp;
}

#endif // STRING_POOL_H
