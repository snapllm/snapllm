/**
 * Chain-of-Thought Utilities
 *
 * SnapLLM API preserves all chain-of-thought reasoning tags in responses.
 * This allows AI systems to access the full reasoning process.
 *
 * For display purposes, client applications can filter these tags using
 * the utilities below.
 *
 * Supported formats:
 * - <thinking>...</thinking> - SnapLLM extended thinking (Anthropic-style)
 * - <think>...</think> - Common reasoning models (DeepSeek, Qwen, etc.)
 * - <internal_thought>...</internal_thought> - Some models
 * - <reasoning>...</reasoning> - Alternative format
 * - <|channel|>...<|end|> - GPT-OSS style
 */

export interface ExtendedThinking {
  reasoning: string[];
  thinking: string | null;  // Extended thinking content (Anthropic style)
  cleanResponse: string;
}

/**
 * Extract chain-of-thought reasoning from model output
 * Returns both the reasoning and the clean response
 */
export function extractChainOfThought(text: string): ExtendedThinking {
  const reasoning: string[] = [];
  let thinking: string | null = null;
  let cleanText = text;

  // Extract <thinking>...</thinking> tags (SnapLLM extended thinking - Anthropic style)
  const thinkingRegex = /<thinking>([\s\S]*?)<\/thinking>/gi;
  let match;
  while ((match = thinkingRegex.exec(text)) !== null) {
    const content = match[1].trim();
    if (!thinking) thinking = content;
    reasoning.push(content);
  }
  cleanText = cleanText.replace(thinkingRegex, '').trim();

  // Extract <think>...</think> tags (common in reasoning models like DeepSeek, Qwen)
  const thinkRegex = /<think>([\s\S]*?)<\/think>/gi;
  while ((match = thinkRegex.exec(text)) !== null) {
    const content = match[1].trim();
    if (!thinking) thinking = content;
    reasoning.push(content);
  }
  cleanText = cleanText.replace(thinkRegex, '').trim();

  // Extract <internal_thought>...</internal_thought>
  const internalThoughtRegex = /<internal_thought>([\s\S]*?)<\/internal_thought>/gi;
  while ((match = internalThoughtRegex.exec(text)) !== null) {
    const content = match[1].trim();
    if (!thinking) thinking = content;
    reasoning.push(content);
  }
  cleanText = cleanText.replace(internalThoughtRegex, '').trim();

  // Extract <reasoning>...</reasoning>
  const reasoningTagRegex = /<reasoning>([\s\S]*?)<\/reasoning>/gi;
  while ((match = reasoningTagRegex.exec(text)) !== null) {
    const content = match[1].trim();
    if (!thinking) thinking = content;
    reasoning.push(content);
  }
  cleanText = cleanText.replace(reasoningTagRegex, '').trim();

  // Extract **Thinking:** or **Reasoning:** sections (markdown style)
  const markdownThinkingRegex = /\*\*(?:Thinking|Reasoning|Internal Thought):\*\*\s*([\s\S]*?)(?=\n\n|\*\*(?:Response|Answer|Output):\*\*|$)/gi;
  while ((match = markdownThinkingRegex.exec(text)) !== null) {
    const content = match[1].trim();
    if (!thinking) thinking = content;
    reasoning.push(content);
  }
  cleanText = cleanText.replace(markdownThinkingRegex, '').trim();

  // Extract <|channel|>analysis...<|end|> patterns (GPT-OSS style)
  const channelRegex = /<\|channel\|>(\w+)<\|message\|>([\s\S]*?)(?=<\|(?:end|start)\||$)/g;
  while ((match = channelRegex.exec(text)) !== null) {
    const channel = match[1];
    const content = match[2].trim();
    reasoning.push(`[${channel}] ${content}`);
  }
  cleanText = cleanText.replace(channelRegex, '').trim();

  // Clean up special tokens
  cleanText = cleanText
    .replace(/<\|(?:start|end|im_start|im_end|eot_id|start_header_id|end_header_id)\|>/g, '')
    .replace(/<\|user\|>|<\|assistant\|>|<\|system\|>/g, '')
    .replace(/<start_of_turn>|<end_of_turn>/g, '')
    .replace(/^\s*model\s*\n/i, '')  // Remove "model" prefix from Gemma
    .replace(/^\s*assistant\s*\n/i, '')  // Remove "assistant" prefix
    .trim();

  // Remove **Response:** or **Answer:** prefix if present
  cleanText = cleanText
    .replace(/^\*\*(?:Response|Answer|Output):\*\*\s*/i, '')
    .trim();

  return {
    reasoning,
    thinking,
    cleanResponse: cleanText,
  };
}

/**
 * Simple filter: Remove all chain-of-thought tags from text
 * Use this for clean display without reasoning
 */
export function filterChainOfThought(text: string): string {
  const { cleanResponse } = extractChainOfThought(text);
  return cleanResponse;
}

/**
 * Check if text contains chain-of-thought reasoning
 */
export function hasChainOfThought(text: string): boolean {
  const lowerText = text.toLowerCase();
  return (
    lowerText.includes('<thinking>') ||
    lowerText.includes('<think>') ||
    lowerText.includes('<internal_thought>') ||
    lowerText.includes('<reasoning>') ||
    lowerText.includes('<|channel|>') ||
    lowerText.includes('<|message|>') ||
    lowerText.includes('**thinking:**') ||
    lowerText.includes('**reasoning:**')
  );
}

/**
 * Format chain-of-thought reasoning for display
 * Returns markdown-formatted reasoning steps
 */
export function formatReasoning(reasoning: string[]): string {
  if (reasoning.length === 0) return '';

  return reasoning
    .map((step, idx) => `**Step ${idx + 1}:**\n${step}`)
    .join('\n\n');
}
