// ============================================================================
// SnapLLM - Video Studio (Disabled)
// ============================================================================

import React from 'react';
import { Card } from '../components/ui';

export default function Video() {
  return (
    <div className="space-y-4">
      <Card className="p-8 text-center">
        <h2 className="text-xl font-semibold text-surface-900 dark:text-white mb-2">
          Video Generation Unavailable
        </h2>
        <p className="text-surface-500">
          This build is configured for LLM, Vision, and Diffusion image generation only.
        </p>
      </Card>
    </div>
  );
}