#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct yyjson_mut_doc;
struct yyjson_mut_val;

// Thin, move-only JSON object wrapper backed by yyjson's mutable API.
// All string data passed to set() is copied into the document pool.
class JsonBody {
public:
    JsonBody();
    ~JsonBody();
    JsonBody(const JsonBody& o);
    JsonBody& operator=(const JsonBody& o);
    JsonBody(JsonBody&&) noexcept;
    JsonBody& operator=(JsonBody&&) noexcept;

    // Parse a JSON string. Returns an empty body on failure.
    static JsonBody parse(const char* json);

    // Type checks for a top-level key.
    bool has  (const char* key) const;
    bool is_str  (const char* key) const;
    bool is_num  (const char* key) const;
    bool is_bool (const char* key) const;
    bool is_array(const char* key) const;
    bool is_obj  (const char* key) const;

    // Read with a default fallback.
    bool        value(const char* key, bool        def) const;
    int         value(const char* key, int         def) const;
    int64_t     value(const char* key, int64_t     def) const;
    std::string value(const char* key, const char* def) const;
    std::string value(const char* key, const std::string& def) const;

    // Read without a default (returns zero/empty on missing key).
    bool        get_bool(const char* key) const;
    int         get_int (const char* key) const;
    int64_t     get_i64 (const char* key) const;
    std::string get_str (const char* key) const;

    // Array helpers.
    std::vector<std::string> get_str_array(const char* key) const;

    // Return an owned copy of a sub-object (safe to use beyond this body's lifetime).
    JsonBody get_obj(const char* key) const;

    // Iterate an array-of-strings field.
    void each_str(const char* key,
                  const std::function<void(const char*)>& fn) const;

    // Iterate an array-of-objects field. The JsonBody passed to fn is a
    // non-owning view valid only for the duration of the callback.
    void each_obj(const char* key,
                  const std::function<void(const JsonBody&)>& fn) const;

    // Iterate an object-of-booleans or object-of-ints field.
    void each_bool(const char* key,
                   const std::function<void(const char* k, bool v)>& fn) const;
    void each_int (const char* key,
                   const std::function<void(const char* k, int  v)>& fn) const;

    // Iterate this body's own key-value pairs. The JsonBody passed to fn is a
    // non-owning view valid only for the duration of the callback.
    void each_kv(const std::function<void(const char* k, const JsonBody& v)>& fn) const;

    // Write a field (upserts: replaces existing key). Returns *this for chaining.
    JsonBody& set(const char* key, bool        v);
    JsonBody& set(const char* key, int         v);
    JsonBody& set(const char* key, int64_t     v);
    JsonBody& set(const char* key, double      v);
    JsonBody& set(const char* key, const char* v);
    JsonBody& set(const char* key, const std::string& v);

    // Copy a value from another body into this body under the given key.
    JsonBody& copy_field(const char* key, const JsonBody& src);

    // Move a child body in as a sub-object (copies its tree into this doc).
    JsonBody& set(const char* key, JsonBody&&  child);
    JsonBody& set(const char* key, const JsonBody& child);

    // Set a string-array field.
    JsonBody& set_str_array(const char* key, const std::vector<std::string>& v);

    // Create an array containing a single object, built by fn.
    JsonBody& set_array_with_object(const char* key,
                                    const std::function<void(JsonBody&)>& fn);

    // Serialize to a JSON string.
    std::string dump() const;           // compact
    std::string dump(int indent) const; // indent>0 -> 2-space pretty

private:
    // Private constructor for non-owning views used in iteration callbacks.
    JsonBody(yyjson_mut_doc* doc, yyjson_mut_val* root, bool owns) noexcept;

    yyjson_mut_doc* doc_  = nullptr;
    yyjson_mut_val* root_ = nullptr;
    bool            owns_ = true;
};
