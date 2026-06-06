#include "json_body.h"
#include "yyjson.h"
#include <cstdlib>
#include <cstring>

// --- Internal helpers --------------------------------------------------------

static yyjson_mut_val* field(yyjson_mut_val* obj, const char* key) {
    if (!obj || !yyjson_mut_is_obj(obj)) return nullptr;
    return yyjson_mut_obj_get(obj, key);
}

// Upsert: remove existing key (if any) then add. Keys are always copied.
static void upsert(yyjson_mut_doc* doc, yyjson_mut_val* obj,
                   const char* key, yyjson_mut_val* val) {
    yyjson_mut_obj_remove_str(obj, key);
    yyjson_mut_obj_add(obj, yyjson_mut_strcpy(doc, key), val);
}

// --- Lifetime ----------------------------------------------------------------

JsonBody::JsonBody(const JsonBody& o) {
    doc_  = yyjson_mut_doc_new(nullptr);
    root_ = yyjson_mut_obj(doc_);
    yyjson_mut_doc_set_root(doc_, root_);
    if (o.root_) {
        if (yyjson_mut_val* copied = yyjson_mut_val_mut_copy(doc_, o.root_)) {
            yyjson_mut_doc_set_root(doc_, copied); root_ = copied;
        }
    }
}

JsonBody& JsonBody::operator=(const JsonBody& o) {
    if (this != &o) {
        JsonBody tmp(o);
        *this = std::move(tmp);
    }
    return *this;
}

JsonBody::JsonBody() {
    doc_  = yyjson_mut_doc_new(nullptr);
    root_ = yyjson_mut_obj(doc_);
    yyjson_mut_doc_set_root(doc_, root_);
    owns_ = true;
}

JsonBody::~JsonBody() {
    if (owns_ && doc_) yyjson_mut_doc_free(doc_);
}

JsonBody::JsonBody(JsonBody&& o) noexcept
    : doc_(o.doc_), root_(o.root_), owns_(o.owns_) {
    o.doc_  = nullptr;
    o.root_ = nullptr;
    o.owns_ = false;
}

JsonBody& JsonBody::operator=(JsonBody&& o) noexcept {
    if (this != &o) {
        if (owns_ && doc_) yyjson_mut_doc_free(doc_);
        doc_  = o.doc_;  root_  = o.root_;  owns_  = o.owns_;
        o.doc_ = nullptr; o.root_ = nullptr; o.owns_ = false;
    }
    return *this;
}

JsonBody::JsonBody(yyjson_mut_doc* doc, yyjson_mut_val* root, const bool owns) noexcept
    : doc_(doc), root_(root), owns_(owns) {}

// --- Parsing -----------------------------------------------------------------

JsonBody JsonBody::parse(const char* json) {
    JsonBody out;
    if (!json || !*json) return out;

    yyjson_doc* idoc = yyjson_read(json, strlen(json), 0);
    if (!idoc) return out;

    yyjson_mut_doc* mdoc = yyjson_doc_mut_copy(idoc, nullptr);
    yyjson_doc_free(idoc);
    if (!mdoc) return out;

    yyjson_mut_val* root = yyjson_mut_doc_get_root(mdoc);
    if (!root || !yyjson_mut_is_obj(root)) {
        yyjson_mut_doc_free(mdoc);
        return out;
    }

    yyjson_mut_doc_free(out.doc_);
    out.doc_  = mdoc;
    out.root_ = root;
    return out;
}

// --- Type checks -------------------------------------------------------------

bool JsonBody::has  (const char* key) const { return field(root_, key) != nullptr; }

bool JsonBody::is_str  (const char* key) const {
    auto* v = field(root_, key); return v && yyjson_mut_is_str(v);
}
bool JsonBody::is_num  (const char* key) const {
    auto* v = field(root_, key); return v && yyjson_mut_is_num(v);
}
bool JsonBody::is_bool (const char* key) const {
    auto* v = field(root_, key); return v && yyjson_mut_is_bool(v);
}
bool JsonBody::is_array(const char* key) const {
    auto* v = field(root_, key); return v && yyjson_mut_is_arr(v);
}
bool JsonBody::is_obj  (const char* key) const {
    auto* v = field(root_, key); return v && yyjson_mut_is_obj(v);
}

// --- value() with defaults ---------------------------------------------------

bool JsonBody::value(const char* key, const bool def) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_bool(v)) ? yyjson_mut_get_bool(v) : def;
}
int JsonBody::value(const char* key, const int def) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_num(v)) ? yyjson_mut_get_int(v) : def;
}
int64_t JsonBody::value(const char* key, const int64_t def) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_num(v)) ? yyjson_mut_get_sint(v) : def;
}
std::string JsonBody::value(const char* key, const char* def) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_str(v)) ? std::string(yyjson_mut_get_str(v))
                                       : std::string(def ? def : "");
}
std::string JsonBody::value(const char* key, const std::string& def) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_str(v)) ? std::string(yyjson_mut_get_str(v)) : def;
}

// --- get_*() without default -------------------------------------------------

bool JsonBody::get_bool(const char* key) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_bool(v)) ? yyjson_mut_get_bool(v) : false;
}
int JsonBody::get_int(const char* key) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_num(v)) ? yyjson_mut_get_int(v) : 0;
}
int64_t JsonBody::get_i64(const char* key) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_num(v)) ? yyjson_mut_get_sint(v) : 0LL;
}
std::string JsonBody::get_str(const char* key) const {
    auto* v = field(root_, key);
    return (v && yyjson_mut_is_str(v)) ? std::string(yyjson_mut_get_str(v))
                                       : std::string{};
}

std::vector<std::string> JsonBody::get_str_array(const char* key) const {
    std::vector<std::string> result;
    auto* arr = field(root_, key);
    if (!arr || !yyjson_mut_is_arr(arr)) return result;
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(arr);
    yyjson_mut_val* item;
    while ((item = yyjson_mut_arr_iter_next(&it)) != nullptr)
        if (yyjson_mut_is_str(item))
            result.emplace_back(yyjson_mut_get_str(item));
    return result;
}

JsonBody JsonBody::get_obj(const char* key) const {
    auto* v = field(root_, key);
    if (!v || !yyjson_mut_is_obj(v)) return JsonBody{};
    JsonBody out;
    if (yyjson_mut_val* copied = yyjson_mut_val_mut_copy(out.doc_, v)) {
        yyjson_mut_doc_set_root(out.doc_, copied);
        out.root_ = copied;
    }
    return out;
}

// --- Iteration ---------------------------------------------------------------

void JsonBody::each_str(const char* key,
                        const std::function<void(const char*)>& fn) const {
    auto* arr = field(root_, key);
    if (!arr || !yyjson_mut_is_arr(arr)) return;
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(arr);
    yyjson_mut_val* item;
    while ((item = yyjson_mut_arr_iter_next(&it)) != nullptr)
        if (yyjson_mut_is_str(item)) fn(yyjson_mut_get_str(item));
}

void JsonBody::each_obj(const char* key,
                        const std::function<void(const JsonBody&)>& fn) const {
    auto* arr = field(root_, key);
    if (!arr || !yyjson_mut_is_arr(arr)) return;
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(arr);
    yyjson_mut_val* item;
    while ((item = yyjson_mut_arr_iter_next(&it)) != nullptr) {
        if (yyjson_mut_is_obj(item)) {
            JsonBody view{doc_, item, false};
            fn(view);
        }
    }
}

void JsonBody::each_bool(const char* key,
                         const std::function<void(const char* k, bool v)>& fn) const {
    auto* obj = field(root_, key);
    if (!obj || !yyjson_mut_is_obj(obj)) return;
    yyjson_mut_obj_iter it = yyjson_mut_obj_iter_with(obj);
    yyjson_mut_val* k;
    while ((k = yyjson_mut_obj_iter_next(&it)) != nullptr) {
        yyjson_mut_val* v = yyjson_mut_obj_iter_get_val(k);
        if (yyjson_mut_is_bool(v))
            fn(yyjson_mut_get_str(k), yyjson_mut_get_bool(v));
    }
}

void JsonBody::each_int(const char* key,
                        const std::function<void(const char* k, int v)>& fn) const {
    auto* obj = field(root_, key);
    if (!obj || !yyjson_mut_is_obj(obj)) return;
    yyjson_mut_obj_iter it = yyjson_mut_obj_iter_with(obj);
    yyjson_mut_val* k;
    while ((k = yyjson_mut_obj_iter_next(&it)) != nullptr) {
        yyjson_mut_val* v = yyjson_mut_obj_iter_get_val(k);
        if (yyjson_mut_is_int(v))
            fn(yyjson_mut_get_str(k), yyjson_mut_get_int(v));
    }
}

void JsonBody::each_kv(
        const std::function<void(const char* k, const JsonBody& v)>& fn) const {
    if (!root_ || !yyjson_mut_is_obj(root_)) return;
    yyjson_mut_obj_iter it = yyjson_mut_obj_iter_with(root_);
    yyjson_mut_val* k;
    while ((k = yyjson_mut_obj_iter_next(&it)) != nullptr) {
        yyjson_mut_val* v = yyjson_mut_obj_iter_get_val(k);
        JsonBody view{doc_, v, false};
        fn(yyjson_mut_get_str(k), view);
    }
}

// --- Setters -----------------------------------------------------------------

JsonBody& JsonBody::set(const char* key, const bool v) {
    upsert(doc_, root_, key, yyjson_mut_bool(doc_, v)); return *this;
}
JsonBody& JsonBody::set(const char* key, const int v) {
    upsert(doc_, root_, key, yyjson_mut_int(doc_, v)); return *this;
}
JsonBody& JsonBody::set(const char* key, const int64_t v) {
    upsert(doc_, root_, key, yyjson_mut_sint(doc_, v)); return *this;
}
JsonBody& JsonBody::set(const char* key, const double v) {
    upsert(doc_, root_, key, yyjson_mut_double(doc_, v)); return *this;
}
JsonBody& JsonBody::set(const char* key, const char* v) {
    upsert(doc_, root_, key, yyjson_mut_strcpy(doc_, v ? v : "")); return *this;
}
JsonBody& JsonBody::set(const char* key, const std::string& v) {
    return set(key, v.c_str());
}

JsonBody& JsonBody::copy_field(const char* key, const JsonBody& src) {
    auto* v = field(src.root_, key);
    if (!v) return *this;
    if (yyjson_mut_val* copied = yyjson_mut_val_mut_copy(doc_, v))
        upsert(doc_, root_, key, copied);
    return *this;
}

JsonBody& JsonBody::set(const char* key, JsonBody&& child) {
    return set(key, child);
}

JsonBody& JsonBody::set(const char* key, const JsonBody& child) {
    if (child.root_) {
        if (yyjson_mut_val* copied = yyjson_mut_val_mut_copy(doc_, child.root_))
            upsert(doc_, root_, key, copied);
    }
    return *this;
}

JsonBody& JsonBody::set_str_array(const char* key,
                                  const std::vector<std::string>& v) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc_);
    for (const auto& s : v)
        yyjson_mut_arr_add_strcpy(doc_, arr, s.c_str());
    upsert(doc_, root_, key, arr);
    return *this;
}

JsonBody& JsonBody::set_array_with_object(
        const char* key, const std::function<void(JsonBody&)>& fn) {
    yyjson_mut_val* arr = yyjson_mut_arr(doc_);
    yyjson_mut_val* obj = yyjson_mut_arr_add_obj(doc_, arr);
    JsonBody view{doc_, obj, false};
    fn(view);
    upsert(doc_, root_, key, arr);
    return *this;
}

// --- Serialization -----------------------------------------------------------

std::string JsonBody::dump() const {
    size_t len = 0;
    char* s = yyjson_mut_write(doc_, 0, &len);
    if (!s) return "{}";
    std::string result(s, len);
    free(s);
    return result;
}

std::string JsonBody::dump(const int indent) const {
    size_t len = 0;
    yyjson_write_flag flags = (indent > 0) ? YYJSON_WRITE_PRETTY_TWO_SPACES : 0;
    char* s = yyjson_mut_write(doc_, flags, &len);
    if (!s) return "{}";
    std::string result(s, len);
    free(s);
    return result;
}
