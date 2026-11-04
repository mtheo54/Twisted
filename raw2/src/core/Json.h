#pragma once
// JSON minimal (lecture / écriture) pour les presets .raw et l'état du plugin. Pas de dépendance.
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace raw2::json {

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(int n) : type_(Type::Number), n_(n) {}
    Value(double n) : type_(Type::Number), n_(n) {}
    Value(float n) : type_(Type::Number), n_(n) {}
    Value(const char* s) : type_(Type::String), s_(s) {}
    Value(std::string s) : type_(Type::String), s_(std::move(s)) {}
    static Value array() { Value v; v.type_ = Type::Array; return v; }
    static Value object() { Value v; v.type_ = Type::Object; return v; }

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }

    double num(double def = 0.0) const { return type_ == Type::Number ? n_ : (type_ == Type::Bool ? (b_ ? 1.0 : 0.0) : def); }
    bool boolean(bool def = false) const { return type_ == Type::Bool ? b_ : (type_ == Type::Number ? n_ != 0.0 : def); }
    const std::string& str() const { static const std::string e; return type_ == Type::String ? s_ : e; }

    // tableau
    size_t size() const { return type_ == Type::Array ? arr_.size() : (type_ == Type::Object ? obj_.size() : 0); }
    const Value& operator[](size_t i) const;
    Value& push(Value v) { if (type_ != Type::Array) *this = array(); arr_.push_back(std::move(v)); return arr_.back(); }
    const std::vector<Value>& items() const { return arr_; }

    // objet (ordre d'insertion conservé)
    const Value& operator[](const char* key) const;
    const Value& get(const std::string& key) const { return (*this)[key.c_str()]; }
    bool has(const char* key) const;
    Value& set(const std::string& key, Value v);
    const std::vector<std::pair<std::string, Value>>& members() const { return obj_; }

    std::string dump(bool pretty = false) const;
    static bool parse(const std::string& text, Value& out);

private:
    void dumpTo(std::string& o, bool pretty, int indent) const;
    Type type_ = Type::Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;
};

std::string base64Encode(const unsigned char* data, size_t n);
bool base64Decode(const std::string& s, std::vector<unsigned char>& out);

} // namespace raw2::json
