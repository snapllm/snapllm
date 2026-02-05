/**
 * @file snapllm_bindings.cpp
 * @brief Python bindings for SnapLLM C++ core using pybind11
 *
 * Exposes ModelManager and related classes to Python for use in FastAPI server
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include "snapllm/model_manager.h"

namespace py = pybind11;
using namespace snapllm;

/**
 * Thread-safe wrapper for ModelManager
 *
 * Python's GIL (Global Interpreter Lock) provides thread safety,
 * but we still use internal locking for consistency with C++ usage.
 */
class PyModelManager {
private:
    std::shared_ptr<ModelManager> manager_;

public:
    PyModelManager(const std::string& workspace_root) {
        manager_ = std::make_shared<ModelManager>(workspace_root);
    }

    /**
     * Load a model from GGUF file
     *
     * @param name Unique model identifier
     * @param path Absolute path to .gguf file
     * @param cache_only If true, only create cache without loading for inference
     * @param domain Domain type for cache optimization
     * @return true if successful
     */
    bool load_model(const std::string& name, const std::string& path, bool cache_only = false, DomainType domain = DomainType::General) {
        // Release GIL during C++ operation
        py::gil_scoped_release release;
        return manager_->load_model(name, path, cache_only, domain);
    }

    /**
     * Switch to a different loaded model
     *
     * @param name Model identifier to switch to
     * @return true if successful (model must be loaded first)
     */
    bool switch_model(const std::string& name) {
        py::gil_scoped_release release;
        return manager_->switch_model(name);
    }

    /**
     * Generate text from a prompt
     *
     * @param prompt Input text prompt
     * @param max_tokens Maximum number of tokens to generate
     * @return Generated text
     */
    std::string generate(const std::string& prompt, int max_tokens) {
        // Release GIL to allow other Python threads during generation
        py::gil_scoped_release release;
        return manager_->generate(prompt, max_tokens);
    }

    /**
     * Batch generation for multiple prompts
     *
     * Processes all prompts in parallel using llama.cpp's multi-sequence API
     *
     * @param prompts List of input prompts
     * @param max_tokens Maximum tokens per prompt
     * @return List of generated texts (same order as prompts)
     */
    std::vector<std::string> generate_batch(const std::vector<std::string>& prompts, int max_tokens) {
        py::gil_scoped_release release;
        return manager_->generate_batch(prompts, max_tokens);
    }

    /**
     * Get list of all loaded model names
     *
     * @return Vector of model identifiers
     */
    std::vector<std::string> get_loaded_models() const {
        return manager_->get_loaded_models();
    }

    /**
     * Get currently active model name
     *
     * @return Model name, or empty string if no model active
     */
    std::string get_current_model() const {
        return manager_->get_current_model();
    }

    /**
     * Enable/disable tensor validation (for debugging)
     *
     * @param enable true to enable validation
     */
    void enable_validation(bool enable) {
        manager_->enable_validation(enable);
    }

    /**
     * Run inference directly from F32 cache without GGUF
     *
     * @param model_name Model to use
     * @param prompt Input prompt
     * @param max_tokens Maximum tokens to generate
     * @return Generated text
     */
    std::string run_inference_from_cache(const std::string& model_name,
                                         const std::string& prompt,
                                         int max_tokens) {
        py::gil_scoped_release release;
        return manager_->run_inference_from_cache(model_name, prompt, max_tokens);
    }

    /**
     * Print cache statistics to stdout
     */
    void print_cache_stats() const {
        manager_->print_cache_stats();
    }

    /**
     * Clear all prompt and generation caches
     */
    void clear_cache() {
        manager_->clear_prompt_cache();
    }

    /**
     * Enable or disable prompt caching
     *
     * @param enabled true to enable caching, false to disable
     */
    void enable_cache(bool enabled) {
        manager_->enable_prompt_cache(enabled);
    }
};

/**
 * pybind11 module definition
 *
 * This creates the Python module that can be imported:
 * >>> from bindings import snapllm_bindings
 * >>> manager = snapllm_bindings.ModelManager("D:\\SnapLLM_Workspace")
 */
PYBIND11_MODULE(snapllm_bindings, m) {
    m.doc() = R"pbdoc(
        SnapLLM Python Bindings
        -----------------------

        Python interface to SnapLLM C++ core for multi-model LLM serving
        with vPID ultra-fast model switching.

        Note: These bindings expose LLM functionality only. Diffusion and
        multimodal (vision) features are not available via this module.

        Example:
            >>> import snapllm_bindings
            >>> manager = snapllm_bindings.ModelManager("D:\\SnapLLM_Workspace")
            >>>
            >>> # Load a model
            >>> manager.load_model("medicine", "D:/Models/medicine.gguf")
            True
            >>>
            >>> # Generate text
            >>> text = manager.generate("What is diabetes?", 50)
            >>> print(text)
            'Diabetes is a chronic condition...'
            >>>
            >>> # Switch models (ultra-fast!)
            >>> manager.load_model("legal", "D:/Models/legal.gguf")
            >>> manager.switch_model("legal")
            True
            >>>
            >>> # Batch generation
            >>> prompts = ["What is AI?", "Explain ML", "What is DL?"]
            >>> results = manager.generate_batch(prompts, 50)
    )pbdoc";

    // Expose DomainType enum FIRST (before ModelManager uses it)
    py::enum_<DomainType>(m, "DomainType", py::arithmetic(), R"pbdoc(
        Model domain type for cache optimization

        Different domains have different cache budgets and strategies:
        - Code: Large processing cache, extensive generation cache (3-50x speedup)
        - Chat: Balanced caches for conversational workloads
        - Reasoning: Large processing cache, minimal generation cache
        - Vision: Minimal caching (dynamic content)
        - General: Default balanced configuration
    )pbdoc")
        .value("Code", DomainType::Code, "Code generation and analysis")
        .value("Chat", DomainType::Chat, "Conversational AI")
        .value("Reasoning", DomainType::Reasoning, "Complex reasoning and analysis")
        .value("Vision", DomainType::Vision, "Vision and image understanding")
        .value("General", DomainType::General, "General purpose (default)")
        .export_values();

    // ModelManager class binding
    py::class_<PyModelManager>(m, "ModelManager", R"pbdoc(
        Multi-model LLM manager with vPID caching

        Manages multiple loaded models and provides ultra-fast switching
        between them using memory-mapped Q8_0 tensor caching.

        Args:
            workspace_root: Path to directory for storing model caches
    )pbdoc")
        .def(py::init<const std::string&>(),
             py::arg("workspace_root"),
             "Initialize ModelManager with workspace directory")

        .def("load_model",
             &PyModelManager::load_model,
             py::arg("name"),
             py::arg("path"),
             py::arg("cache_only") = false,
             py::arg("domain") = DomainType::General,
             R"pbdoc(
                Load a model from GGUF file

                On first load, creates a Q8_0 cache. Subsequent loads use the cache
                for fast startup via memory-mapped I/O.

                Args:
                    name: Unique identifier for this model
                    path: Absolute path to .gguf model file
                    cache_only: If True, only create cache without loading for inference
                    domain: Domain type for cache optimization (Code/Chat/Reasoning/Vision/General)

                Returns:
                    True if successful, False otherwise

                Example:
                    >>> manager.load_model("medicine", "D:/Models/medicine.gguf", False, DomainType.Code)
                    True
             )pbdoc")

        .def("switch_model",
             &PyModelManager::switch_model,
             py::arg("name"),
             R"pbdoc(
                Switch to a different loaded model

                This operation is ultra-fast (<1ms) thanks to vPID caching.
                The model must already be loaded via load_model().

                Args:
                    name: Model identifier to switch to

                Returns:
                    True if successful, False if model not loaded

                Example:
                    >>> manager.switch_model("legal")  # <1ms!
                    True
             )pbdoc")

        .def("generate",
             &PyModelManager::generate,
             py::arg("prompt"),
             py::arg("max_tokens"),
             R"pbdoc(
                Generate text from a prompt

                Uses the currently active model for generation.

                Args:
                    prompt: Input text prompt
                    max_tokens: Maximum number of tokens to generate

                Returns:
                    Generated text string

                Example:
                    >>> text = manager.generate("What is diabetes?", 50)
                    >>> print(text)
                    'Diabetes is a chronic condition...'
             )pbdoc")

        .def("generate_batch",
             &PyModelManager::generate_batch,
             py::arg("prompts"),
             py::arg("max_tokens"),
             R"pbdoc(
                Generate text for multiple prompts in parallel

                Uses llama.cpp's multi-sequence API to process all prompts
                simultaneously, achieving near-linear speedup.

                Args:
                    prompts: List of input text prompts
                    max_tokens: Maximum tokens to generate per prompt

                Returns:
                    List of generated texts (same order as input prompts)

                Example:
                    >>> prompts = ["What is AI?", "Explain ML", "What is DL?"]
                    >>> results = manager.generate_batch(prompts, 50)
                    >>> for result in results:
                    ...     print(result)
             )pbdoc")

        .def("get_loaded_models",
             &PyModelManager::get_loaded_models,
             "Get list of all loaded model names")

        .def("get_current_model",
             &PyModelManager::get_current_model,
             "Get name of currently active model")

        .def("enable_validation",
             &PyModelManager::enable_validation,
             py::arg("enable"),
             "Enable or disable tensor validation for debugging")

        .def("run_inference_from_cache",
             &PyModelManager::run_inference_from_cache,
             py::arg("model_name"),
             py::arg("prompt"),
             py::arg("max_tokens"),
             R"pbdoc(
                Run inference directly from F32 cache without GGUF

                This demonstrates Phase 2 capability: inference without
                the original GGUF file, using only the vPID cache.

                Args:
                    model_name: Model to use (must have cache created)
                    prompt: Input prompt
                    max_tokens: Maximum tokens to generate

                Returns:
                    Generated text
             )pbdoc")

        .def("print_cache_stats",
             &PyModelManager::print_cache_stats,
             "Print detailed cache statistics to stdout")

        .def("clear_cache",
             &PyModelManager::clear_cache,
             "Clear all prompt and generation caches for all models")

        .def("enable_cache",
             &PyModelManager::enable_cache,
             py::arg("enabled"),
             R"pbdoc(
                Enable or disable prompt caching

                Args:
                    enabled: True to enable caching, False to disable

                Example:
                    >>> manager.enable_cache(True)  # Enable caching
                    >>> manager.enable_cache(False)  # Disable caching
             )pbdoc");

    // Module version info
    m.attr("__version__") = "0.1.0";
    m.attr("__author__") = "SnapLLM Team";
}
