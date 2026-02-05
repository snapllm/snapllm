/**
 * @file ison.hpp
 * @brief Unified ISON header for SnapLLM
 *
 * ISON: Interchange Simple Object Notation
 * The LLM-native data format for token-efficient AI workflows
 *
 * This header includes all ISON components:
 * - ison_parser.hpp    - Core ISON parser
 * - ison_formatter.hpp - Output formatting
 * - ison_prompts.hpp   - Prompt templates
 * - isonantic.hpp      - Schema validation
 *
 * Usage:
 * @code
 * #include "snapllm/ison/ison.hpp"
 *
 * // Parse ISON
 * auto doc = ison::parse(ison_text);
 *
 * // Format output
 * auto output = snapllm::ison_fmt::ISONFormatter::format_response(response, meta);
 *
 * // Build prompts
 * auto prompt = snapllm::ison_prompts::ISONPromptBuilder::build_rag_prompt(query, chunks);
 *
 * // Validate
 * auto schema = snapllm::isonantic::schemas::inference_response();
 * auto result = schema.validate(doc);
 * @endcode
 */

#ifndef SNAPLLM_ISON_HPP
#define SNAPLLM_ISON_HPP

#include "ison_parser.hpp"
#include "ison_formatter.hpp"
#include "ison_prompts.hpp"
#include "isonantic.hpp"

namespace snapllm {

/**
 * @brief ISON convenience namespace combining all ISON functionality
 */
namespace ISON {

// Re-export core types from ison namespace
using Document = ::ison::Document;
using Block = ::ison::Block;
using Row = ::ison::Row;
using Value = ::ison::Value;
using Reference = ::ison::Reference;

// Re-export parse functions
using ::ison::parse;
using ::ison::loads;
using ::ison::load;
using ::ison::dumps;
using ::ison::dump;

// Re-export formatter
using Formatter = ison_fmt::ISONFormatter;
using OutputFormat = ison_fmt::OutputFormat;
using InferenceMetadata = ison_fmt::InferenceMetadata;

// Re-export prompt builder
using PromptBuilder = ison_prompts::ISONPromptBuilder;
using Domain = ison_prompts::Domain;
using RAGChunk = ison_prompts::RAGChunk;
using ConversationTurn = ison_prompts::ConversationTurn;

// Re-export validation
using BlockSchema = isonantic::BlockSchema;
using DocumentSchema = isonantic::DocumentSchema;
using FieldConstraint = isonantic::FieldConstraint;
using FieldType = isonantic::FieldType;
using ValidationResult = isonantic::ValidationResult;
using ValidationError = isonantic::ValidationError;

// Pre-built schemas
namespace Schemas {
    inline DocumentSchema InferenceResponse() { return isonantic::schemas::inference_response(); }
    inline DocumentSchema RAGContext() { return isonantic::schemas::rag_context(); }
    inline DocumentSchema ModelList() { return isonantic::schemas::model_list(); }
}

/**
 * @brief Quick parse and validate
 */
inline ValidationResult validate(const std::string& ison_text, const DocumentSchema& schema) {
    return schema.validate(ison_text);
}

/**
 * @brief Convert ISON to JSON
 */
inline std::string to_json(const std::string& ison_text) {
    return Formatter::to_json(ison_text);
}

/**
 * @brief Token count estimation for ISON vs JSON
 * Rough estimate: ISON is ~40% more token-efficient
 */
inline size_t estimate_token_savings(size_t json_tokens) {
    return static_cast<size_t>(json_tokens * 0.4);
}

} // namespace ISON

} // namespace snapllm

#endif // SNAPLLM_ISON_HPP
