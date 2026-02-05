import { useEffect, useState } from 'react';

export default function LoadingScreen() {
  const [progress, setProgress] = useState(0);

  useEffect(() => {
    const interval = setInterval(() => {
      setProgress((prev) => {
        if (prev >= 100) {
          clearInterval(interval);
          return 100;
        }
        return prev + 1;
      });
    }, 30);

    return () => clearInterval(interval);
  }, []);

  return (
    <div className="fixed inset-0 flex items-center justify-center bg-gradient-to-br from-slate-50 to-sky-50 dark:from-slate-900 dark:to-slate-800">
      <div className="text-center">
        <img
          src="/snapllm-full.png"
          alt="SnapLLM"
          className="w-96 h-auto mx-auto mb-8 animate-pulse"
        />

        <div className="w-96 mx-auto">
          <div className="relative pt-1">
            <div className="flex mb-2 items-center justify-between">
              <div>
                <span className="text-xs font-semibold inline-block py-1 px-2 uppercase rounded-full text-sky-600 bg-sky-100 dark:bg-sky-900/50 dark:text-sky-400">
                  Loading
                </span>
              </div>
              <div className="text-right">
                <span className="text-xs font-semibold inline-block text-sky-600 dark:text-sky-400">
                  {progress}%
                </span>
              </div>
            </div>
            <div className="overflow-hidden h-2 mb-4 text-xs flex rounded-full bg-gray-200 dark:bg-slate-700">
              <div
                style={{ width: `${progress}%` }}
                className="shadow-none flex flex-col text-center whitespace-nowrap text-white justify-center bg-sky-500 transition-all duration-300 rounded-full"
              ></div>
            </div>
          </div>

          <p className="text-gray-600 dark:text-gray-400 mt-4 text-sm">
            Initializing SnapLLM Desktop...
          </p>
        </div>
      </div>
    </div>
  );
}
