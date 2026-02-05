/**
 * @file ison_formatter.hpp
 * @brief ISON output formatter for SnapLLM
 *
 * Formats LLM responses in ISON format for structured output.
 * Integrates with SnapLLM's inference pipeline.
 */

#ifndef SNAPLLM_ISON_FORMATTER_HPP
#define SNAPLLM_ISON_FORMATTER_HPP

#include "ison_parser.hpp"
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <chrono>
#include <iomanip>

namespace snapllm {
namespace ison_fmt {

/**
 * @brief Output format options
 */
enum class OutputFormat {
    Plain,      // Raw text output
    ISON,       // Structured ISON format
    JSON,       // JSON (via ISON conversion)
    ISONL       // Line-oriented ISON for streaming
};

/**
 * @brief Inference metadata for ISON output
 */
struct InferenceMetadata {
    std::string model_name;
    std::string prompt;
    int tokens_generated = 0;
    double generation_time_ms = 0;
    double tokens_per_second = 0;
    std::string timestamp;

    InferenceMetadata() {
        // Set timestamp to current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        timestamp = oss.str();
    }
};

/**
 * @brief Format LLM response as ISON
 */
class ISONFormatter {
public:
    /**
     * @brief Format a simple response with metadata
     */
    static std::string format_response(
        const std::string& response,
        const InferenceMetadata& meta
    ) {
        std::ostringstream oss;

        // Object block for metadata
        oss << "object.inference\n";
        oss << "model prompt tokens time_ms tok_per_sec timestamp\n";
        oss << quote_if_needed(meta.model_name) << " ";
        oss << quote_if_needed(truncate_prompt(meta.prompt, 50)) << " ";
        oss << meta.tokens_generated << " ";
        oss << std::fixed << std::setprecision(2) << meta.generation_time_ms << " ";
        oss << std::fixed << std::setprecision(2) << meta.tokens_per_second << " ";
        oss << "\"" << meta.timestamp << "\"\n\n";

        // Object block for response
        oss << "object.response\n";
        oss << "content\n";
        oss << quote_if_needed(response) << "\n";

        return oss.str();
    }

    /**
     * @brief Format multiple responses as ISON table
     */
    static std::string format_batch_responses(
        const std::vector<std::pair<std::string, std::string>>& prompt_responses,
        const std::string& model_name
    ) {
        std::ostringstream oss;

        oss << "# SnapLLM Batch Results\n\n";
        oss << "object.metadata\n";
        oss << "model count\n";
        oss << quote_if_needed(model_name) << " " << prompt_responses.size() << "\n\n";

        oss << "table.results\n";
        oss << "id prompt response\n";

        int id = 1;
        for (const auto& [prompt, response] : prompt_responses) {
            oss << id++ << " ";
            oss << quote_if_needed(truncate_prompt(prompt, 30)) << " ";
            oss << quote_if_needed(response) << "\n";
        }

        return oss.str();
    }

    /**
     * @brief Format model comparison results
     */
    static std::string format_model_comparison(
        const std::string& prompt,
        const std::vector<std::tuple<std::string, std::string, double>>& model_results
    ) {
        std::ostringstream oss;

        oss << "# Multi-Model Comparison\n\n";

        oss << "object.query\n";
        oss << "prompt\n";
        oss << quote_if_needed(prompt) << "\n\n";

        oss << "table.responses\n";
        oss << "model response tok_per_sec\n";

        for (const auto& [model, response, speed] : model_results) {
            oss << quote_if_needed(model) << " ";
            oss << quote_if_needed(response) << " ";
            oss << std::fixed << std::setprecision(2) << speed << "\n";
        }

        return oss.str();
    }

    /**
     * @brief Format loaded models list
     */
    static std::string format_model_list(
        const std::vector<std::string>& models,
        const std::string& current_model
    ) {
        std::ostringstream oss;

        oss << "# SnapLLM Loaded Models\n\n";

        oss << "table.models\n";
        oss << "name active\n";

        for (const auto& model : models) {
            oss << quote_if_needed(model) << " ";
            oss << (model == current_model ? "true" : "false") << "\n";
        }

        return oss.str();
    }

    /**
     * @brief Format cache statistics
     */
    static std::string format_cache_stats(
        size_t total_allocs,
        size_t total_reads_mb,
        size_t total_writes_mb,
        size_t cache_hits,
        size_t cache_misses
    ) {
        std::ostringstream oss;

        oss << "object.cache_stats\n";
        oss << "allocations reads_mb writes_mb hits misses hit_rate\n";

        double hit_rate = (cache_hits + cache_misses > 0)
            ? (100.0 * cache_hits / (cache_hits + cache_misses))
            : 0.0;

        oss << total_allocs << " ";
        oss << total_reads_mb << " ";
        oss << total_writes_mb << " ";
        oss << cache_hits << " ";
        oss << cache_misses << " ";
        oss << std::fixed << std::setprecision(1) << hit_rate << "\n";

        return oss.str();
    }

    /**
     * @brief Create ISON context for RAG injection
     */
    static std::string create_rag_context(
        const std::string& query,
        const std::vector<std::pair<std::string, double>>& chunks_with_scores
    ) {
        std::ostringstream oss;

        oss << "# RAG Context\n\n";

        oss << "object.query\n";
        oss << "text\n";
        oss << quote_if_needed(query) << "\n\n";

        oss << "table.context\n";
        oss << "rank score content\n";

        int rank = 1;
        for (const auto& [content, score] : chunks_with_scores) {
            oss << rank++ << " ";
            oss << std::fixed << std::setprecision(4) << score << " ";
            oss << quote_if_needed(content) << "\n";
        }

        return oss.str();
    }

    /**
     * @brief Convert ISON to JSON
     */
    static std::string to_json(const std::string& ison_text) {
        try {
            auto doc = ::ison::parse(ison_text);
            return doc.to_json(2);
        } catch (const std::exception& e) {
            return "{\"error\": \"" + std::string(e.what()) + "\"}";
        }
    }

    /**
     * @brief Parse output format from string
     */
    static OutputFormat parse_format(const std::string& fmt) {
        if (fmt == "ison" || fmt == "ISON") return OutputFormat::ISON;
        if (fmt == "json" || fmt == "JSON") return OutputFormat::JSON;
        if (fmt == "isonl" || fmt == "ISONL") return OutputFormat::ISONL;
        return OutputFormat::Plain;
    }

private:
    static std::string quote_if_needed(const std::string& s) {
        if (s.empty()) return "\"\"";

        bool needs_quote = false;
        for (char c : s) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '"' || c == ':') {
                needs_quote = true;
                break;
            }
        }

        // Check for special tokens
        if (s == "true" || s == "false" || s == "null") {
            needs_quote = true;
        }

        if (!needs_quote) {
            // Check if it looks like a number
            bool is_numeric = true;
            for (char c : s) {
                if (!std::isdigit(c) && c != '.' && c != '-') {
                    is_numeric = false;
                    break;
                }
            }
            if (is_numeric && !s.empty()) needs_quote = true;
        }

        if (needs_quote) {
            std::string escaped;
            for (char c : s) {
                switch (c) {
                    case '\\': escaped += "\\\\"; break;
                    case '"':  escaped += "\\\""; break;
                    case '\n': escaped += "\\n"; break;
                    case '\t': escaped += "\\t"; break;
                    default:   escaped += c; break;
                }
            }
            return "\"" + escaped + "\"";
        }
        return s;
    }

    static std::string truncate_prompt(const std::string& prompt, size_t max_len) {
        if (prompt.length() <= max_len) return prompt;
        return prompt.substr(0, max_len - 3) + "...";
    }
};

} // namespace ison_fmt
} // namespace snapllm

#endif // SNAPLLM_ISON_FORMATTER_HPP
