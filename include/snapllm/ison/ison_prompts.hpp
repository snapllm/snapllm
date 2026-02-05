/**
 * @file ison_prompts.hpp
 * @brief ISON-based prompt templates for SnapLLM
 *
 * Provides structured prompt templates using ISON format for:
 * - Domain-specific models (medical, legal, coding)
 * - RAG context injection
 * - Multi-turn conversations
 * - Structured output requests
 */

#ifndef SNAPLLM_ISON_PROMPTS_HPP
#define SNAPLLM_ISON_PROMPTS_HPP

#include <string>
#include <vector>
#include <map>
#include <sstream>

namespace snapllm {
namespace ison_prompts {

/**
 * @brief Domain types for specialized prompts
 */
enum class Domain {
    General,
    Medical,
    Legal,
    Coding,
    Finance,
    Science
};

/**
 * @brief RAG chunk with metadata
 */
struct RAGChunk {
    std::string content;
    double score;
    std::string source;
    int chunk_id;
};

/**
 * @brief Conversation turn
 */
struct ConversationTurn {
    std::string role;  // "user" or "assistant"
    std::string content;
};

/**
 * @brief ISON Prompt Template Builder
 */
class ISONPromptBuilder {
public:
    /**
     * @brief Build a simple prompt with ISON context
     */
    static std::string build_simple_prompt(
        const std::string& query,
        Domain domain = Domain::General
    ) {
        std::ostringstream oss;

        oss << "# Context\n\n";
        oss << "object.request\n";
        oss << "domain query\n";
        oss << domain_to_string(domain) << " \"" << escape_string(query) << "\"\n\n";
        oss << "# Instructions\n";
        oss << get_domain_instructions(domain) << "\n\n";
        oss << "# Query\n";
        oss << query;

        return oss.str();
    }

    /**
     * @brief Build RAG-enhanced prompt with ISON context
     */
    static std::string build_rag_prompt(
        const std::string& query,
        const std::vector<RAGChunk>& chunks,
        Domain domain = Domain::General
    ) {
        std::ostringstream oss;

        // Query block
        oss << "# Query\n\n";
        oss << "object.query\n";
        oss << "text domain\n";
        oss << "\"" << escape_string(query) << "\" " << domain_to_string(domain) << "\n\n";

        // Context chunks as ISON table
        if (!chunks.empty()) {
            oss << "# Retrieved Context\n\n";
            oss << "table.context\n";
            oss << "rank score source content\n";

            for (size_t i = 0; i < chunks.size(); ++i) {
                const auto& chunk = chunks[i];
                oss << (i + 1) << " ";
                oss << std::fixed;
                oss.precision(4);
                oss << chunk.score << " ";
                oss << "\"" << escape_string(chunk.source) << "\" ";
                oss << "\"" << escape_string(chunk.content) << "\"\n";
            }
            oss << "\n";
        }

        // Instructions
        oss << "# Instructions\n";
        oss << "Answer the query using ONLY the context provided above.\n";
        oss << "If the context doesn't contain relevant information, say so.\n";
        oss << get_domain_instructions(domain) << "\n\n";

        // Final query
        oss << "# Answer the following:\n";
        oss << query;

        return oss.str();
    }

    /**
     * @brief Build multi-turn conversation prompt
     */
    static std::string build_conversation_prompt(
        const std::vector<ConversationTurn>& history,
        const std::string& current_query,
        Domain domain = Domain::General
    ) {
        std::ostringstream oss;

        // System context
        oss << "# System\n\n";
        oss << "object.system\n";
        oss << "role domain\n";
        oss << "assistant " << domain_to_string(domain) << "\n\n";

        // Conversation history as ISON table
        if (!history.empty()) {
            oss << "# Conversation History\n\n";
            oss << "table.messages\n";
            oss << "turn role content\n";

            for (size_t i = 0; i < history.size(); ++i) {
                const auto& turn = history[i];
                oss << (i + 1) << " ";
                oss << turn.role << " ";
                oss << "\"" << escape_string(turn.content) << "\"\n";
            }
            oss << "\n";
        }

        // Current query
        oss << "# Current Query\n";
        oss << current_query;

        return oss.str();
    }

    /**
     * @brief Build structured output request prompt
     */
    static std::string build_structured_output_prompt(
        const std::string& query,
        const std::vector<std::string>& output_fields,
        Domain domain = Domain::General
    ) {
        std::ostringstream oss;

        oss << "# Request\n\n";
        oss << "object.request\n";
        oss << "query domain\n";
        oss << "\"" << escape_string(query) << "\" " << domain_to_string(domain) << "\n\n";

        // Output schema
        oss << "# Expected Output Format (ISON)\n\n";
        oss << "object.response\n";
        for (const auto& field : output_fields) {
            oss << field << " ";
        }
        oss << "\n[Your structured response here]\n\n";

        oss << "# Instructions\n";
        oss << "Respond in the ISON format shown above.\n";
        oss << "Each field should contain the relevant information.\n";
        oss << get_domain_instructions(domain) << "\n\n";

        oss << "# Query\n";
        oss << query;

        return oss.str();
    }

    /**
     * @brief Build comparison prompt for multiple items
     */
    static std::string build_comparison_prompt(
        const std::string& query,
        const std::vector<std::pair<std::string, std::string>>& items,
        Domain domain = Domain::General
    ) {
        std::ostringstream oss;

        oss << "# Comparison Request\n\n";
        oss << "object.query\n";
        oss << "task domain\n";
        oss << "comparison " << domain_to_string(domain) << "\n\n";

        // Items to compare
        oss << "table.items\n";
        oss << "id name description\n";

        for (size_t i = 0; i < items.size(); ++i) {
            oss << (i + 1) << " ";
            oss << "\"" << escape_string(items[i].first) << "\" ";
            oss << "\"" << escape_string(items[i].second) << "\"\n";
        }
        oss << "\n";

        oss << "# Instructions\n";
        oss << "Compare the items above based on: " << query << "\n";
        oss << "Provide a structured comparison.\n\n";

        return oss.str();
    }

    /**
     * @brief Get domain-specific system prompt
     */
    static std::string get_domain_system_prompt(Domain domain) {
        switch (domain) {
            case Domain::Medical:
                return R"(You are a medical AI assistant. Provide accurate, evidence-based medical information.
Always recommend consulting healthcare professionals for medical decisions.
Be clear about limitations and uncertainties in medical knowledge.)";

            case Domain::Legal:
                return R"(You are a legal AI assistant. Provide informative legal guidance.
Always recommend consulting licensed attorneys for legal advice.
Note jurisdiction-specific variations when applicable.)";

            case Domain::Coding:
                return R"(You are a coding AI assistant. Provide clean, efficient, well-documented code.
Follow best practices and modern conventions.
Explain your code and design decisions.)";

            case Domain::Finance:
                return R"(You are a financial AI assistant. Provide educational financial information.
Always recommend consulting licensed financial advisors for investment decisions.
Note that past performance doesn't guarantee future results.)";

            case Domain::Science:
                return R"(You are a scientific AI assistant. Provide accurate, well-sourced scientific information.
Distinguish between established science and emerging research.
Acknowledge uncertainties and ongoing debates in the field.)";

            default:
                return R"(You are a helpful AI assistant. Provide accurate, helpful information.
Be clear, concise, and informative in your responses.)";
        }
    }

private:
    static std::string domain_to_string(Domain domain) {
        switch (domain) {
            case Domain::Medical: return "medical";
            case Domain::Legal: return "legal";
            case Domain::Coding: return "coding";
            case Domain::Finance: return "finance";
            case Domain::Science: return "science";
            default: return "general";
        }
    }

    static std::string get_domain_instructions(Domain domain) {
        switch (domain) {
            case Domain::Medical:
                return "Provide medically accurate information. Cite sources when possible.";
            case Domain::Legal:
                return "Provide legally accurate information. Note jurisdiction differences.";
            case Domain::Coding:
                return "Provide clean, working code with explanations.";
            case Domain::Finance:
                return "Provide financially sound information. Note risks.";
            case Domain::Science:
                return "Provide scientifically accurate information. Cite research.";
            default:
                return "Provide helpful, accurate information.";
        }
    }

    static std::string escape_string(const std::string& s) {
        std::string result;
        for (char c : s) {
            switch (c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }
};

/**
 * @brief Parse domain from model name
 */
inline Domain detect_domain_from_model(const std::string& model_name) {
    std::string lower = model_name;
    for (char& c : lower) c = std::tolower(c);

    if (lower.find("med") != std::string::npos ||
        lower.find("health") != std::string::npos ||
        lower.find("clinic") != std::string::npos) {
        return Domain::Medical;
    }
    if (lower.find("legal") != std::string::npos ||
        lower.find("law") != std::string::npos) {
        return Domain::Legal;
    }
    if (lower.find("code") != std::string::npos ||
        lower.find("coding") != std::string::npos ||
        lower.find("program") != std::string::npos) {
        return Domain::Coding;
    }
    if (lower.find("finance") != std::string::npos ||
        lower.find("trading") != std::string::npos) {
        return Domain::Finance;
    }
    if (lower.find("science") != std::string::npos ||
        lower.find("research") != std::string::npos) {
        return Domain::Science;
    }
    return Domain::General;
}

} // namespace ison_prompts
} // namespace snapllm

#endif // SNAPLLM_ISON_PROMPTS_HPP
