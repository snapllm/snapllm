#!/usr/bin/env python3
"""
SnapLLM Multi-Model Switching Example

This example demonstrates SnapLLM's key feature: ultra-fast model switching.
With vPID architecture, switching between loaded models takes <1ms.

Use Cases:
- Multi-domain assistant (medical, legal, coding, etc.)
- A/B model comparison
- Ensemble methods
- Dynamic model selection based on query type

Prerequisites:
    pip install requests

Usage:
    1. Start the SnapLLM server: snapllm --server --port 6930
    2. Update MODEL_PATHS with your actual model paths
    3. Run this script: python multi_model_switching.py
"""

import requests
import json
import time
from typing import Dict, List, Tuple

BASE_URL = "http://localhost:6930"

# Update these paths to your actual model files
MODEL_PATHS = {
    "general": "/path/to/general-assistant.gguf",
    "medical": "/path/to/medical-llm.gguf",
    "coding": "/path/to/code-llm.gguf",
    "creative": "/path/to/creative-writer.gguf"
}


def load_model(model_id: str, file_path: str) -> dict:
    """Load a model into the server."""
    response = requests.post(
        f"{BASE_URL}/api/v1/models/load",
        json={"model_id": model_id, "file_path": file_path}
    )
    return response.json()


def switch_model(model_id: str) -> Tuple[dict, float]:
    """Switch to a model and measure the switch time."""
    start = time.perf_counter()
    response = requests.post(
        f"{BASE_URL}/api/v1/models/switch",
        json={"model_id": model_id}
    )
    switch_time = (time.perf_counter() - start) * 1000  # ms
    return response.json(), switch_time


def chat(model: str, messages: list, max_tokens: int = 256) -> dict:
    """Send a chat completion request."""
    response = requests.post(
        f"{BASE_URL}/v1/chat/completions",
        json={
            "model": model,
            "messages": messages,
            "max_tokens": max_tokens,
            "temperature": 0.7
        }
    )
    return response.json()


def get_response_text(response: dict) -> str:
    """Extract response text from chat completion response."""
    if "choices" in response and len(response["choices"]) > 0:
        return response["choices"][0]["message"]["content"]
    return str(response)


def benchmark_switching(model_ids: List[str], iterations: int = 10) -> Dict[str, float]:
    """
    Benchmark model switching times.

    This demonstrates the <1ms switching that makes SnapLLM unique.
    """
    print(f"\nBenchmarking model switching ({iterations} iterations)...")
    print("-" * 50)

    switch_times = {mid: [] for mid in model_ids}

    for i in range(iterations):
        for model_id in model_ids:
            _, switch_time = switch_model(model_id)
            switch_times[model_id].append(switch_time)

    # Calculate statistics
    print(f"{'Model':<15} {'Avg (ms)':<12} {'Min (ms)':<12} {'Max (ms)':<12}")
    print("-" * 50)

    for model_id in model_ids:
        times = switch_times[model_id]
        avg = sum(times) / len(times)
        min_time = min(times)
        max_time = max(times)
        print(f"{model_id:<15} {avg:<12.3f} {min_time:<12.3f} {max_time:<12.3f}")

    return {mid: sum(times)/len(times) for mid, times in switch_times.items()}


def multi_domain_assistant():
    """
    Example: Multi-domain assistant that routes queries to specialized models.

    This demonstrates how SnapLLM enables building assistants that can
    leverage multiple specialized models without the latency penalty
    of traditional model switching.
    """
    print("\n" + "=" * 60)
    print("Multi-Domain Assistant Demo")
    print("=" * 60)

    # Define domain-specific queries
    queries = [
        ("medical", "What are the common symptoms of type 2 diabetes?"),
        ("coding", "Write a Python function to find the longest common subsequence."),
        ("creative", "Write a haiku about artificial intelligence."),
        ("general", "What is the capital of Japan and its population?"),
        ("medical", "Explain how vaccines work in simple terms."),
        ("coding", "What's the time complexity of quicksort?"),
    ]

    for domain, query in queries:
        print(f"\n[{domain.upper()}] Query: {query[:50]}...")

        # Switch to the appropriate model (<1ms!)
        _, switch_time = switch_model(domain)
        print(f"  Switch time: {switch_time:.2f}ms")

        # Get response
        start = time.time()
        response = chat(
            model=domain,
            messages=[{"role": "user", "content": query}],
            max_tokens=150
        )
        total_time = time.time() - start

        text = get_response_text(response)
        print(f"  Response: {text[:200]}...")
        print(f"  Total time: {total_time*1000:.1f}ms")


def ab_comparison():
    """
    Example: A/B model comparison for the same query.

    Useful for:
    - Evaluating model quality
    - Testing fine-tuned vs base models
    - Comparing different model sizes
    """
    print("\n" + "=" * 60)
    print("A/B Model Comparison Demo")
    print("=" * 60)

    query = "Explain quantum entanglement to a 10-year-old."
    models = ["general", "creative"]

    print(f"\nQuery: {query}\n")

    for model_id in models:
        print(f"--- Model: {model_id} ---")

        # Switch to model
        _, switch_time = switch_model(model_id)

        # Get response
        start = time.time()
        response = chat(
            model=model_id,
            messages=[{"role": "user", "content": query}],
            max_tokens=200
        )
        total_time = time.time() - start

        text = get_response_text(response)
        print(f"Response ({total_time*1000:.0f}ms):\n{text}\n")


def rapid_ensemble():
    """
    Example: Rapid ensemble - get responses from multiple models quickly.

    Because switching is <1ms, we can query multiple models and combine
    their responses for better quality.
    """
    print("\n" + "=" * 60)
    print("Rapid Ensemble Demo")
    print("=" * 60)

    query = "What is machine learning in one sentence?"
    models = ["general", "coding", "creative"]

    print(f"\nQuery: {query}")
    print(f"Models: {', '.join(models)}\n")

    responses = []
    total_start = time.time()

    for model_id in models:
        switch_model(model_id)
        response = chat(
            model=model_id,
            messages=[{"role": "user", "content": query}],
            max_tokens=100
        )
        text = get_response_text(response)
        responses.append((model_id, text))
        print(f"[{model_id}]: {text[:150]}...")

    total_time = time.time() - total_start
    print(f"\nTotal time for {len(models)} models: {total_time*1000:.0f}ms")
    print(f"Average per model: {total_time*1000/len(models):.0f}ms")


def main():
    print("SnapLLM Multi-Model Switching Example")
    print("=" * 60)

    # Check server health
    try:
        response = requests.get(f"{BASE_URL}/health")
        if response.status_code != 200:
            raise Exception("Server not healthy")
    except Exception as e:
        print(f"Error: Cannot connect to server at {BASE_URL}")
        print("Start the server with: snapllm --server --port 6930")
        return

    print("Server is running!")

    # Load models (update paths first!)
    print("\nLoading models (this is a one-time operation)...")
    for model_id, path in MODEL_PATHS.items():
        print(f"  Loading {model_id}...", end=" ")
        result = load_model(model_id, path)
        if result.get("status") == "success":
            print(f"OK ({result.get('load_time_ms', 0):.0f}ms)")
        else:
            print(f"FAILED: {result}")
            print("\nUpdate MODEL_PATHS with your actual model paths!")
            return

    # List loaded models
    response = requests.get(f"{BASE_URL}/api/v1/models")
    models = response.json()
    print(f"\nLoaded {len(models.get('models', []))} models")

    # Run benchmarks
    model_ids = list(MODEL_PATHS.keys())
    benchmark_switching(model_ids)

    # Run demos
    multi_domain_assistant()
    ab_comparison()
    rapid_ensemble()

    print("\n" + "=" * 60)
    print("Example complete!")
    print("\nKey Takeaways:")
    print("  - Model switching takes <1ms with vPID architecture")
    print("  - Load models once, switch between them instantly")
    print("  - Enable new use cases: multi-domain, A/B testing, ensembles")
    print("=" * 60)


if __name__ == "__main__":
    main()
