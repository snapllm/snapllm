#!/usr/bin/env python3
"""
SnapLLM Basic Usage Example

This example demonstrates basic operations with SnapLLM:
- Loading models
- Chat completion
- Model switching
- Unloading models

Prerequisites:
    pip install requests

Usage:
    1. Start the SnapLLM server: snapllm --server --port 6930
    2. Run this script: python basic_usage.py
"""

import requests
import json

BASE_URL = "http://localhost:6930"


def check_health():
    """Check if the server is running."""
    try:
        response = requests.get(f"{BASE_URL}/health")
        return response.status_code == 200
    except requests.ConnectionError:
        return False


def load_model(model_id: str, file_path: str, model_type: str = "auto"):
    """Load a model into the server."""
    response = requests.post(
        f"{BASE_URL}/api/v1/models/load",
        json={
            "model_id": model_id,
            "file_path": file_path,
            "model_type": model_type
        }
    )
    return response.json()


def list_models():
    """List all loaded models."""
    response = requests.get(f"{BASE_URL}/api/v1/models")
    return response.json()


def switch_model(model_id: str):
    """Switch to a different loaded model."""
    response = requests.post(
        f"{BASE_URL}/api/v1/models/switch",
        json={"model_id": model_id}
    )
    return response.json()


def chat(model: str, messages: list, max_tokens: int = 256):
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


def unload_model(model_id: str):
    """Unload a model from the server."""
    response = requests.post(
        f"{BASE_URL}/api/v1/models/unload",
        json={"model_id": model_id}
    )
    return response.json()


def main():
    print("SnapLLM Basic Usage Example")
    print("=" * 50)

    # Check server health
    if not check_health():
        print("Error: SnapLLM server is not running!")
        print("Start it with: snapllm --server --port 6930")
        return

    print("Server is healthy!")

    # Example model paths - update these to your actual model paths
    MODEL_PATH_1 = "/path/to/your/first-model.gguf"
    MODEL_PATH_2 = "/path/to/your/second-model.gguf"

    # Load first model
    print("\nLoading first model...")
    result = load_model("assistant", MODEL_PATH_1)
    print(f"Result: {json.dumps(result, indent=2)}")

    # Chat with first model
    print("\nChatting with assistant model...")
    response = chat(
        model="assistant",
        messages=[
            {"role": "system", "content": "You are a helpful assistant."},
            {"role": "user", "content": "What is the capital of France?"}
        ]
    )

    if "choices" in response:
        print(f"Response: {response['choices'][0]['message']['content']}")
    else:
        print(f"Response: {json.dumps(response, indent=2)}")

    # Load second model
    print("\nLoading second model...")
    result = load_model("coder", MODEL_PATH_2)
    print(f"Result: {json.dumps(result, indent=2)}")

    # List all models
    print("\nListing all models...")
    models = list_models()
    print(f"Loaded models: {json.dumps(models, indent=2)}")

    # Switch to coder model (< 1ms!)
    print("\nSwitching to coder model...")
    result = switch_model("coder")
    print(f"Result: {json.dumps(result, indent=2)}")

    # Chat with coder model
    print("\nChatting with coder model...")
    response = chat(
        model="coder",
        messages=[
            {"role": "user", "content": "Write a Python function to calculate fibonacci numbers."}
        ],
        max_tokens=512
    )

    if "choices" in response:
        print(f"Response: {response['choices'][0]['message']['content']}")
    else:
        print(f"Response: {json.dumps(response, indent=2)}")

    # Unload models when done
    print("\nUnloading models...")
    unload_model("assistant")
    unload_model("coder")
    print("Done!")


if __name__ == "__main__":
    main()
