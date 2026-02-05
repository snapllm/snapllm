#!/usr/bin/env python3
"""
SnapLLM Context Caching Example (vPID L2)

This example demonstrates the vPID L2 Context API:
- Ingest documents (O(n^2) one-time operation)
- Query with O(1) context lookup
- Manage context tiers (hot/warm/cold)

The key benefit is that large documents are processed once,
and subsequent queries use the cached KV state for instant response.

Prerequisites:
    pip install requests

Usage:
    1. Start the SnapLLM server with a model loaded
    2. Run this script: python context_caching.py
"""

import requests
import json
import time

BASE_URL = "http://localhost:6930"


def ingest_context(content: str, model_id: str, name: str, ttl_seconds: int = 86400):
    """
    Ingest a document and pre-compute its KV cache.

    This is an O(n^2) operation but only needs to happen once.
    After ingestion, queries against this context are O(1) + O(q^2) where q is query length.
    """
    response = requests.post(
        f"{BASE_URL}/api/v1/contexts/ingest",
        json={
            "content": content,
            "model_id": model_id,
            "name": name,
            "ttl_seconds": ttl_seconds
        }
    )
    return response.json()


def query_context(context_id: str, query: str, max_tokens: int = 256):
    """
    Query using a cached context.

    The context KV cache is loaded from disk (O(1)), then only the query
    needs to be processed (O(q^2)). This is much faster than processing
    the entire document + query each time.
    """
    response = requests.post(
        f"{BASE_URL}/api/v1/contexts/{context_id}/query",
        json={
            "query": query,
            "max_tokens": max_tokens
        }
    )
    return response.json()


def list_contexts(tier: str = None, model_id: str = None):
    """List all contexts, optionally filtered by tier or model."""
    params = {}
    if tier:
        params["tier"] = tier
    if model_id:
        params["model_id"] = model_id

    response = requests.get(f"{BASE_URL}/api/v1/contexts", params=params)
    return response.json()


def get_context_info(context_id: str):
    """Get detailed information about a specific context."""
    response = requests.get(f"{BASE_URL}/api/v1/contexts/{context_id}")
    return response.json()


def promote_context(context_id: str, tier: str = "hot"):
    """Promote a context to a higher tier (e.g., cold -> warm -> hot)."""
    response = requests.post(
        f"{BASE_URL}/api/v1/contexts/{context_id}/promote",
        json={"tier": tier}
    )
    return response.json()


def demote_context(context_id: str, tier: str = "cold"):
    """Demote a context to a lower tier to free memory."""
    response = requests.post(
        f"{BASE_URL}/api/v1/contexts/{context_id}/demote",
        json={"tier": tier}
    )
    return response.json()


def delete_context(context_id: str):
    """Delete a context and free all associated storage."""
    response = requests.delete(f"{BASE_URL}/api/v1/contexts/{context_id}")
    return response.json()


def get_context_stats():
    """Get statistics about context storage and cache performance."""
    response = requests.get(f"{BASE_URL}/api/v1/contexts/stats")
    return response.json()


# Sample document for the example
SAMPLE_DOCUMENT = """
SnapLLM Employee Handbook

Chapter 1: Company Overview
SnapLLM is a high-performance AI inference company focused on making LLMs
accessible and efficient. Our mission is to democratize AI by enabling
ultra-fast model switching and efficient multi-model deployment.

Chapter 2: Work Policies
- Work Hours: Core hours are 10am-4pm, with flexibility outside these hours.
- Remote Work: Employees may work remotely up to 3 days per week.
- Vacation: All employees receive 20 days of paid vacation per year.
- Sick Leave: Unlimited sick leave with manager approval.

Chapter 3: Benefits
- Health Insurance: Comprehensive medical, dental, and vision coverage.
- 401(k): Company matches up to 4% of salary.
- Equipment: $3000 budget for home office setup.
- Learning: $2000 annual budget for courses and conferences.

Chapter 4: Code of Conduct
- Treat all colleagues with respect and professionalism.
- Maintain confidentiality of company and client information.
- Follow security protocols for handling sensitive data.
- Report any concerns through the appropriate channels.

Chapter 5: Technical Guidelines
- All code must be reviewed before merging to main.
- Write tests for new features and bug fixes.
- Document API changes in the changelog.
- Use conventional commits for version control.
"""


def main():
    print("SnapLLM Context Caching Example (vPID L2)")
    print("=" * 60)

    # The model ID must match a loaded model
    MODEL_ID = "assistant"  # Update this to your loaded model

    # Step 1: Ingest the document
    print("\n1. Ingesting document (one-time O(n^2) operation)...")
    start = time.time()
    result = ingest_context(
        content=SAMPLE_DOCUMENT,
        model_id=MODEL_ID,
        name="employee-handbook",
        ttl_seconds=3600  # 1 hour TTL
    )
    ingest_time = time.time() - start
    print(f"   Ingestion result: {json.dumps(result, indent=2)}")
    print(f"   Ingestion time: {ingest_time:.2f}s")

    if result.get("status") != "success":
        print("   Error: Ingestion failed. Make sure a model is loaded.")
        return

    context_id = result.get("context_id")
    print(f"   Context ID: {context_id}")

    # Step 2: Query the cached context (fast!)
    print("\n2. Querying with cached context (O(1) lookup + O(q^2) for query)...")

    queries = [
        "What is the vacation policy?",
        "How much is the equipment budget?",
        "What are the core work hours?",
        "Tell me about the 401k matching."
    ]

    for query in queries:
        print(f"\n   Query: {query}")
        start = time.time()
        result = query_context(context_id, query, max_tokens=150)
        query_time = time.time() - start

        if result.get("status") == "success":
            print(f"   Response: {result.get('response', 'N/A')[:200]}...")
            print(f"   Cache hit: {result.get('cache_hit', False)}")
            print(f"   Latency: {query_time*1000:.1f}ms")
        else:
            print(f"   Error: {result}")

    # Step 3: Check context statistics
    print("\n3. Context statistics...")
    stats = get_context_stats()
    print(f"   Stats: {json.dumps(stats, indent=2)}")

    # Step 4: List all contexts
    print("\n4. Listing all contexts...")
    contexts = list_contexts()
    print(f"   Contexts: {json.dumps(contexts, indent=2)}")

    # Step 5: Demonstrate tier management
    print("\n5. Tier management demonstration...")

    # Demote to cold tier (frees memory, keeps on disk)
    print("   Demoting to cold tier...")
    result = demote_context(context_id, "cold")
    print(f"   Result: {json.dumps(result, indent=2)}")

    # Query still works (loads from disk)
    print("\n   Querying cold context (loads from disk)...")
    start = time.time()
    result = query_context(context_id, "What is the sick leave policy?")
    cold_query_time = time.time() - start
    print(f"   Cold query latency: {cold_query_time*1000:.1f}ms")

    # Promote back to hot tier
    print("\n   Promoting back to hot tier...")
    result = promote_context(context_id, "hot")
    print(f"   Result: {json.dumps(result, indent=2)}")

    # Step 6: Clean up
    print("\n6. Cleaning up...")
    result = delete_context(context_id)
    print(f"   Delete result: {json.dumps(result, indent=2)}")

    print("\nExample complete!")
    print("\nKey Takeaways:")
    print("  - Ingest once, query many times")
    print("  - Hot tier: Fastest, uses GPU/RAM")
    print("  - Warm tier: Medium speed, uses CPU RAM")
    print("  - Cold tier: Slowest, uses disk (but still faster than re-processing)")


if __name__ == "__main__":
    main()
