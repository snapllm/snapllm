/**
 * @file isonantic.hpp
 * @brief ISONantic - Pydantic-like validation for ISON in C++
 *
 * Provides schema validation for ISON documents:
 * - Field type validation
 * - Required/optional fields
 * - Value constraints
 * - Reference validation
 * - Custom validators
 */

#ifndef SNAPLLM_ISONANTIC_HPP
#define SNAPLLM_ISONANTIC_HPP

#include "ison_parser.hpp"
#include <string>
#include <vector>
#include <functional>
#include <regex>
#include <limits>

namespace snapllm {
namespace isonantic {

/**
 * @brief Validation error details
 */
struct ValidationError {
    std::string field;
    std::string message;
    std::string value;

    ValidationError(const std::string& f, const std::string& m, const std::string& v = "")
        : field(f), message(m), value(v) {}
};

/**
 * @brief Validation result
 */
struct ValidationResult {
    bool valid;
    std::vector<ValidationError> errors;

    ValidationResult() : valid(true) {}

    void add_error(const std::string& field, const std::string& message, const std::string& value = "") {
        valid = false;
        errors.emplace_back(field, message, value);
    }

    std::string to_string() const {
        if (valid) return "Validation passed";
        std::string result = "Validation failed:\n";
        for (const auto& err : errors) {
            result += "  - " + err.field + ": " + err.message;
            if (!err.value.empty()) result += " (got: " + err.value + ")";
            result += "\n";
        }
        return result;
    }
};

/**
 * @brief Field type enumeration
 */
enum class FieldType {
    Any,
    String,
    Int,
    Float,
    Bool,
    Reference,
    Null
};

/**
 * @brief Field constraint
 */
struct FieldConstraint {
    std::string name;
    FieldType type = FieldType::Any;
    bool required = true;
    bool nullable = false;

    // String constraints
    size_t min_length = 0;
    size_t max_length = std::numeric_limits<size_t>::max();
    std::string pattern;  // Regex pattern

    // Numeric constraints
    double min_value = -std::numeric_limits<double>::infinity();
    double max_value = std::numeric_limits<double>::infinity();

    // Enum constraint
    std::vector<std::string> allowed_values;

    // Custom validator
    std::function<bool(const ison::Value&)> custom_validator;
    std::string custom_error;

    FieldConstraint(const std::string& n) : name(n) {}

    FieldConstraint& set_type(FieldType t) { type = t; return *this; }
    FieldConstraint& set_required(bool r) { required = r; return *this; }
    FieldConstraint& set_nullable(bool n) { nullable = n; return *this; }
    FieldConstraint& set_min_length(size_t l) { min_length = l; return *this; }
    FieldConstraint& set_max_length(size_t l) { max_length = l; return *this; }
    FieldConstraint& set_pattern(const std::string& p) { pattern = p; return *this; }
    FieldConstraint& set_min(double v) { min_value = v; return *this; }
    FieldConstraint& set_max(double v) { max_value = v; return *this; }
    FieldConstraint& set_allowed(const std::vector<std::string>& v) { allowed_values = v; return *this; }
    FieldConstraint& set_validator(std::function<bool(const ison::Value&)> v, const std::string& err = "Custom validation failed") {
        custom_validator = v;
        custom_error = err;
        return *this;
    }
};

/**
 * @brief Block schema definition
 */
class BlockSchema {
public:
    std::string kind;  // "object" or "table"
    std::string name;
    std::vector<FieldConstraint> fields;
    bool allow_extra_fields = false;

    BlockSchema(const std::string& k, const std::string& n)
        : kind(k), name(n) {}

    BlockSchema& add_field(const FieldConstraint& f) {
        fields.push_back(f);
        return *this;
    }

    BlockSchema& set_allow_extra(bool allow) {
        allow_extra_fields = allow;
        return *this;
    }

    /**
     * @brief Validate a block against this schema
     */
    ValidationResult validate(const ison::Block& block) const {
        ValidationResult result;

        // Check block kind
        if (block.kind != kind) {
            result.add_error("block.kind", "Expected '" + kind + "'", block.kind);
        }

        // Check block name
        if (block.name != name) {
            result.add_error("block.name", "Expected '" + name + "'", block.name);
        }

        // Validate each row
        for (size_t row_idx = 0; row_idx < block.rows.size(); ++row_idx) {
            const auto& row = block.rows[row_idx];
            std::string row_prefix = "row[" + std::to_string(row_idx) + "]";

            // Check required fields
            for (const auto& field : fields) {
                auto it = row.find(field.name);

                if (it == row.end()) {
                    if (field.required) {
                        result.add_error(row_prefix + "." + field.name, "Required field missing");
                    }
                    continue;
                }

                const ison::Value& value = it->second;

                // Check nullable
                if (value.is_null()) {
                    if (!field.nullable) {
                        result.add_error(row_prefix + "." + field.name, "Field cannot be null");
                    }
                    continue;
                }

                // Validate type
                validate_field_type(result, row_prefix + "." + field.name, value, field);

                // Validate constraints
                validate_field_constraints(result, row_prefix + "." + field.name, value, field);
            }

            // Check for extra fields
            if (!allow_extra_fields) {
                for (const auto& [key, _] : row) {
                    bool found = false;
                    for (const auto& f : fields) {
                        if (f.name == key) { found = true; break; }
                    }
                    if (!found) {
                        result.add_error(row_prefix + "." + key, "Unexpected field");
                    }
                }
            }
        }

        return result;
    }

private:
    void validate_field_type(ValidationResult& result, const std::string& path,
                            const ison::Value& value, const FieldConstraint& field) const {
        if (field.type == FieldType::Any) return;

        bool type_ok = false;
        std::string expected_type;

        switch (field.type) {
            case FieldType::String:
                type_ok = value.is_string();
                expected_type = "string";
                break;
            case FieldType::Int:
                type_ok = value.is_int();
                expected_type = "int";
                break;
            case FieldType::Float:
                type_ok = value.is_float() || value.is_int();
                expected_type = "float";
                break;
            case FieldType::Bool:
                type_ok = value.is_bool();
                expected_type = "bool";
                break;
            case FieldType::Reference:
                type_ok = value.is_reference();
                expected_type = "reference";
                break;
            default:
                type_ok = true;
        }

        if (!type_ok) {
            result.add_error(path, "Expected type '" + expected_type + "'");
        }
    }

    void validate_field_constraints(ValidationResult& result, const std::string& path,
                                   const ison::Value& value, const FieldConstraint& field) const {
        // String constraints
        if (value.is_string()) {
            const std::string& str = value.as_string();

            if (str.length() < field.min_length) {
                result.add_error(path, "String too short (min: " + std::to_string(field.min_length) + ")",
                               std::to_string(str.length()));
            }
            if (str.length() > field.max_length) {
                result.add_error(path, "String too long (max: " + std::to_string(field.max_length) + ")",
                               std::to_string(str.length()));
            }
            if (!field.pattern.empty()) {
                try {
                    std::regex re(field.pattern);
                    if (!std::regex_match(str, re)) {
                        result.add_error(path, "Does not match pattern '" + field.pattern + "'", str);
                    }
                } catch (...) {
                    result.add_error(path, "Invalid regex pattern: " + field.pattern);
                }
            }
            if (!field.allowed_values.empty()) {
                bool found = false;
                for (const auto& allowed : field.allowed_values) {
                    if (str == allowed) { found = true; break; }
                }
                if (!found) {
                    result.add_error(path, "Value not in allowed list", str);
                }
            }
        }

        // Numeric constraints
        if (value.is_int() || value.is_float()) {
            double num = value.is_int() ? static_cast<double>(value.as_int()) : value.as_float();

            if (num < field.min_value) {
                result.add_error(path, "Value too small (min: " + std::to_string(field.min_value) + ")",
                               std::to_string(num));
            }
            if (num > field.max_value) {
                result.add_error(path, "Value too large (max: " + std::to_string(field.max_value) + ")",
                               std::to_string(num));
            }
        }

        // Custom validator
        if (field.custom_validator) {
            if (!field.custom_validator(value)) {
                result.add_error(path, field.custom_error);
            }
        }
    }
};

/**
 * @brief Document schema - collection of block schemas
 */
class DocumentSchema {
public:
    std::vector<BlockSchema> blocks;
    bool strict = true;  // Reject unknown blocks

    DocumentSchema& add_block(const BlockSchema& b) {
        blocks.push_back(b);
        return *this;
    }

    DocumentSchema& set_strict(bool s) {
        strict = s;
        return *this;
    }

    /**
     * @brief Validate an ISON document
     */
    ValidationResult validate(const ison::Document& doc) const {
        ValidationResult result;

        // Validate each block
        for (const auto& block : doc.blocks) {
            bool found = false;
            for (const auto& schema : blocks) {
                if (schema.kind == block.kind && schema.name == block.name) {
                    auto block_result = schema.validate(block);
                    if (!block_result.valid) {
                        for (const auto& err : block_result.errors) {
                            result.add_error(block.kind + "." + block.name + "." + err.field,
                                           err.message, err.value);
                        }
                    }
                    found = true;
                    break;
                }
            }

            if (!found && strict) {
                result.add_error(block.kind + "." + block.name, "Unknown block type");
            }
        }

        // Check required blocks
        for (const auto& schema : blocks) {
            bool found = false;
            for (const auto& block : doc.blocks) {
                if (schema.kind == block.kind && schema.name == block.name) {
                    found = true;
                    break;
                }
            }
            // Note: We don't enforce required blocks by default
        }

        return result;
    }

    /**
     * @brief Validate ISON text directly
     */
    ValidationResult validate(const std::string& ison_text) const {
        try {
            auto doc = ison::parse(ison_text);
            return validate(doc);
        } catch (const ison::ISONError& e) {
            ValidationResult result;
            result.add_error("parse", e.what());
            return result;
        }
    }
};

/**
 * @brief Common schema builders
 */
namespace schemas {

/**
 * @brief Schema for inference response
 */
inline DocumentSchema inference_response() {
    DocumentSchema schema;

    BlockSchema inference("object", "inference");
    inference.add_field(FieldConstraint("model").set_type(FieldType::String).set_required(true));
    inference.add_field(FieldConstraint("prompt").set_type(FieldType::String).set_required(true));
    inference.add_field(FieldConstraint("tokens").set_type(FieldType::Int).set_min(0));
    inference.add_field(FieldConstraint("time_ms").set_type(FieldType::Float).set_min(0));
    inference.add_field(FieldConstraint("tok_per_sec").set_type(FieldType::Float).set_min(0));
    inference.add_field(FieldConstraint("timestamp").set_type(FieldType::String));
    schema.add_block(inference);

    BlockSchema response("object", "response");
    response.add_field(FieldConstraint("content").set_type(FieldType::String).set_required(true));
    schema.add_block(response);

    return schema;
}

/**
 * @brief Schema for RAG context
 */
inline DocumentSchema rag_context() {
    DocumentSchema schema;

    BlockSchema query("object", "query");
    query.add_field(FieldConstraint("text").set_type(FieldType::String).set_required(true));
    schema.add_block(query);

    BlockSchema context("table", "context");
    context.add_field(FieldConstraint("rank").set_type(FieldType::Int).set_min(1));
    context.add_field(FieldConstraint("score").set_type(FieldType::Float).set_min(0).set_max(1));
    context.add_field(FieldConstraint("content").set_type(FieldType::String).set_required(true));
    schema.add_block(context);

    return schema;
}

/**
 * @brief Schema for model list
 */
inline DocumentSchema model_list() {
    DocumentSchema schema;

    BlockSchema models("table", "models");
    models.add_field(FieldConstraint("name").set_type(FieldType::String).set_required(true));
    models.add_field(FieldConstraint("active").set_type(FieldType::Bool));
    schema.add_block(models);

    return schema;
}

} // namespace schemas

} // namespace isonantic
} // namespace snapllm

#endif // SNAPLLM_ISONANTIC_HPP
