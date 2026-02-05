import { Info, Sparkles } from 'lucide-react';
import { Card, Badge } from '../components/ui';

export default function AboutPage() {
  return (
    <div className="p-6 md:p-8 max-w-5xl mx-auto space-y-6">
      <div className="flex flex-col gap-3 md:flex-row md:items-center md:justify-between">
        <div className="flex items-start gap-3">
          <div className="w-11 h-11 rounded-xl bg-gradient-to-br from-brand-500 to-ai-purple flex items-center justify-center">
            <Info className="w-5 h-5 text-white" />
          </div>
          <div>
            <h1 className="text-3xl font-bold text-surface-900 dark:text-white">About SnapLLM</h1>
            <p className="text-surface-500">
              Platform overview, acknowledgments, and innovation highlights.
            </p>
          </div>
        </div>
        <div className="flex items-center gap-2">
          <Badge variant="default">v1.0.0</Badge>
        </div>
      </div>

      {/* About & Acknowledgments */}
      <Card className="space-y-5">
        <div className="space-y-4">
          <div>
            <p className="text-sm font-medium text-surface-700 dark:text-surface-300">Inference Engines</p>
            <div className="mt-2 space-y-3">
              <div className="flex items-start gap-3">
                <span className="text-sm text-surface-500">•</span>
                <div className="flex-1">
                  <p className="text-sm text-surface-600 dark:text-surface-400">
                    <a
                      href="https://github.com/ggerganov/llama.cpp"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="text-brand-600 hover:text-brand-700 font-medium"
                    >
                      llama.cpp
                    </a>
                    {' '}by Georgi Gerganov - Customized C++ inference engine (MIT License)
                  </p>
                </div>
              </div>
              <div className="flex items-start gap-3">
                <span className="text-sm text-surface-500">•</span>
                <div className="flex-1">
                  <p className="text-sm text-surface-600 dark:text-surface-400">
                    <a
                      href="https://github.com/leejet/stable-diffusion.cpp"
                      target="_blank"
                      rel="noopener noreferrer"
                      className="text-brand-600 hover:text-brand-700 font-medium"
                    >
                      stable-diffusion.cpp
                    </a>
                    {' '}by leejet - C++ diffusion engine for local image generation (MIT License)
                  </p>
                </div>
              </div>
            </div>
          </div>
          <div>
            <p className="text-sm font-medium text-surface-700 dark:text-surface-300">SnapLLM Innovation</p>
            <p className="text-sm text-surface-600 dark:text-surface-400 mt-1">
              While leveraging proven inference engines, SnapLLM adds tiered memory caching,
              model-agnostic tensor discovery, and multi-model serving with minimal RAM.
            </p>
          </div>
          <div className="grid grid-cols-1 md:grid-cols-3 gap-4">
            <div className="p-4 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800">
              <div className="flex items-center gap-2 text-sm font-semibold text-surface-900 dark:text-white">
                <Sparkles className="w-4 h-4 text-warning-500" />
                Multimodal Stack
              </div>
              <p className="text-sm text-surface-600 dark:text-surface-400 mt-2">
                Text, vision, and diffusion workflows orchestrated locally.
              </p>
            </div>
            <div className="p-4 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800">
              <div className="text-sm font-semibold text-surface-900 dark:text-white">vPID Switching</div>
              <p className="text-sm text-surface-600 dark:text-surface-400 mt-2">
                Sub-millisecond model switching powered by tiered memory caching.
              </p>
            </div>
            <div className="p-4 rounded-lg border border-surface-200 dark:border-surface-700 bg-surface-50 dark:bg-surface-800">
              <div className="text-sm font-semibold text-surface-900 dark:text-white">Local-Only</div>
              <p className="text-sm text-surface-600 dark:text-surface-400 mt-2">
                No external services, no auth, and no telemetry by default.
              </p>
            </div>
          </div>
          <div className="pt-2 border-t border-surface-200 dark:border-surface-700">
            <p className="text-xs text-surface-500">SnapLLM v1.0.0 - MIT License</p>
          </div>
          <div className="pt-3 mt-3 border-t border-surface-200 dark:border-surface-700 text-center">
            <p className="text-xs text-surface-500 mb-2">Developed by</p>
            <a href="https://aroora.ai" target="_blank" rel="noopener noreferrer">
              <img
                src="/logo_files/AROORA_315x91.png"
                alt="AroorA AI Lab"
                className="h-8 mx-auto opacity-80 hover:opacity-100 transition-opacity"
              />
            </a>
          </div>
        </div>
      </Card>
    </div>
  );
}
