// ============================================================================
// SnapLLM - Markdown Renderer Component
// Properly renders LLM responses with code blocks, lists, and formatting
// ============================================================================

import React, { memo } from 'react';
import ReactMarkdown from 'react-markdown';
import { Prism as SyntaxHighlighter } from 'react-syntax-highlighter';
import { oneDark, oneLight } from 'react-syntax-highlighter/dist/esm/styles/prism';
import remarkGfm from 'remark-gfm';
import { clsx } from 'clsx';
import { Copy, Check } from 'lucide-react';

interface MarkdownRendererProps {
  content: string;
  className?: string;
  isStreaming?: boolean;
}

// Code block component with copy button
const CodeBlock: React.FC<{
  language: string;
  value: string;
}> = ({ language, value }) => {
  const [copied, setCopied] = React.useState(false);
  const isDark = document.documentElement.classList.contains('dark');

  const handleCopy = async () => {
    await navigator.clipboard.writeText(value);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  return (
    <div className="relative group my-3 rounded-lg overflow-hidden">
      {/* Language badge and copy button */}
      <div className="flex items-center justify-between px-4 py-2 bg-surface-800 dark:bg-surface-900 text-xs">
        <span className="text-surface-400 font-mono uppercase">{language || 'code'}</span>
        <button
          onClick={handleCopy}
          className="flex items-center gap-1 px-2 py-1 rounded bg-surface-700 hover:bg-surface-600 text-surface-300 transition-colors"
        >
          {copied ? (
            <>
              <Check className="w-3 h-3 text-green-400" />
              <span>Copied!</span>
            </>
          ) : (
            <>
              <Copy className="w-3 h-3" />
              <span>Copy</span>
            </>
          )}
        </button>
      </div>
      <SyntaxHighlighter
        language={language || 'text'}
        style={isDark ? oneDark : oneLight}
        customStyle={{
          margin: 0,
          padding: '1rem',
          fontSize: '0.875rem',
          lineHeight: '1.5',
          borderRadius: 0,
        }}
        showLineNumbers={value.split('\n').length > 3}
      >
        {value}
      </SyntaxHighlighter>
    </div>
  );
};

// Inline code component
const InlineCode: React.FC<{ children: React.ReactNode }> = ({ children }) => (
  <code className="px-1.5 py-0.5 rounded bg-surface-200 dark:bg-surface-700 text-brand-600 dark:text-brand-400 text-sm font-mono">
    {children}
  </code>
);

export const MarkdownRenderer: React.FC<MarkdownRendererProps> = memo(({
  content,
  className,
  isStreaming = false,
}) => {
  return (
    <div className={clsx('markdown-content', className)}>
      <ReactMarkdown
        remarkPlugins={[remarkGfm]}
        components={{
          // Code blocks
          code({ node, inline, className, children, ...props }) {
            const match = /language-(\w+)/.exec(className || '');
            const language = match ? match[1] : '';
            const value = String(children).replace(/\n$/, '');

            if (!inline && (match || value.includes('\n'))) {
              return <CodeBlock language={language} value={value} />;
            }

            return <InlineCode {...props}>{children}</InlineCode>;
          },

          // Headings
          h1: ({ children }) => (
            <h1 className="text-2xl font-bold mt-6 mb-4 text-surface-900 dark:text-surface-100 border-b border-surface-200 dark:border-surface-700 pb-2">
              {children}
            </h1>
          ),
          h2: ({ children }) => (
            <h2 className="text-xl font-bold mt-5 mb-3 text-surface-900 dark:text-surface-100">
              {children}
            </h2>
          ),
          h3: ({ children }) => (
            <h3 className="text-lg font-semibold mt-4 mb-2 text-surface-900 dark:text-surface-100">
              {children}
            </h3>
          ),
          h4: ({ children }) => (
            <h4 className="text-base font-semibold mt-3 mb-2 text-surface-800 dark:text-surface-200">
              {children}
            </h4>
          ),

          // Paragraphs
          p: ({ children }) => (
            <p className="my-2 leading-relaxed text-surface-700 dark:text-surface-300">
              {children}
            </p>
          ),

          // Lists
          ul: ({ children }) => (
            <ul className="my-2 ml-4 space-y-1 list-disc list-outside text-surface-700 dark:text-surface-300">
              {children}
            </ul>
          ),
          ol: ({ children }) => (
            <ol className="my-2 ml-4 space-y-1 list-decimal list-outside text-surface-700 dark:text-surface-300">
              {children}
            </ol>
          ),
          li: ({ children }) => (
            <li className="leading-relaxed pl-1">
              {children}
            </li>
          ),

          // Blockquote
          blockquote: ({ children }) => (
            <blockquote className="my-3 pl-4 border-l-4 border-brand-500 bg-surface-50 dark:bg-surface-800/50 py-2 pr-4 italic text-surface-600 dark:text-surface-400">
              {children}
            </blockquote>
          ),

          // Links
          a: ({ href, children }) => (
            <a
              href={href}
              target="_blank"
              rel="noopener noreferrer"
              className="text-brand-600 dark:text-brand-400 hover:underline"
            >
              {children}
            </a>
          ),

          // Strong/Bold
          strong: ({ children }) => (
            <strong className="font-semibold text-surface-900 dark:text-surface-100">
              {children}
            </strong>
          ),

          // Emphasis/Italic
          em: ({ children }) => (
            <em className="italic">
              {children}
            </em>
          ),

          // Horizontal rule
          hr: () => (
            <hr className="my-4 border-surface-200 dark:border-surface-700" />
          ),

          // Tables
          table: ({ children }) => (
            <div className="my-3 overflow-x-auto">
              <table className="min-w-full border border-surface-200 dark:border-surface-700 rounded-lg overflow-hidden">
                {children}
              </table>
            </div>
          ),
          thead: ({ children }) => (
            <thead className="bg-surface-100 dark:bg-surface-800">
              {children}
            </thead>
          ),
          tbody: ({ children }) => (
            <tbody className="divide-y divide-surface-200 dark:divide-surface-700">
              {children}
            </tbody>
          ),
          tr: ({ children }) => (
            <tr className="hover:bg-surface-50 dark:hover:bg-surface-800/50">
              {children}
            </tr>
          ),
          th: ({ children }) => (
            <th className="px-4 py-2 text-left text-sm font-semibold text-surface-900 dark:text-surface-100">
              {children}
            </th>
          ),
          td: ({ children }) => (
            <td className="px-4 py-2 text-sm text-surface-700 dark:text-surface-300">
              {children}
            </td>
          ),
        }}
      >
        {content}
      </ReactMarkdown>
      {/* Streaming cursor */}
      {isStreaming && (
        <span className="inline-block w-2 h-4 ml-0.5 bg-brand-500 animate-pulse" />
      )}
    </div>
  );
});

MarkdownRenderer.displayName = 'MarkdownRenderer';

export default MarkdownRenderer;
