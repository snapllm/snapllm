// ============================================================================
// RAG Placeholder (temporary - page needs fixing)
// ============================================================================

export default function RAGPlaceholder() {
  return (
    <div className="flex items-center justify-center h-full min-h-[60vh]">
      <div className="text-center p-8 max-w-md">
        <div className="w-16 h-16 mx-auto mb-4 rounded-full bg-amber-100 dark:bg-amber-900/30 flex items-center justify-center">
          <svg className="w-8 h-8 text-amber-600 dark:text-amber-400" fill="none" viewBox="0 0 24 24" stroke="currentColor">
            <path strokeLinecap="round" strokeLinejoin="round" strokeWidth={2} d="M12 9v2m0 4h.01m-6.938 4h13.856c1.54 0 2.502-1.667 1.732-3L13.732 4c-.77-1.333-2.694-1.333-3.464 0L3.34 16c-.77 1.333.192 3 1.732 3z" />
          </svg>
        </div>
        <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
          RAG Knowledge Base
        </h2>
        <p className="text-surface-600 dark:text-surface-400 mb-4">
          This page is temporarily unavailable while we fix some issues. Please check back soon.
        </p>
        <p className="text-sm text-surface-500">
          In the meantime, you can use the Chat, Compare, or other features.
        </p>
      </div>
    </div>
  );
}
