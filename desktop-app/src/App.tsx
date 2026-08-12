// ============================================================================
// SnapLLM - Main Application Entry
// Premium Multi-Model AI Platform
// ============================================================================

import React from 'react';
import { BrowserRouter, Routes, Route } from 'react-router-dom';

// Layout
import { Layout } from './components/layout/Layout';

// Pages
import Dashboard from './pages/Dashboard';
import Models from './pages/Models';
import Chat from './pages/Chat';
import Images from './pages/Images';
import Vision from './pages/Vision';
import Compare from './pages/Compare';
import QuickSwitch from './pages/QuickSwitch';
import Contexts from './pages/Contexts';
import Playground from './pages/Playground';
import BatchProcessing from './pages/BatchProcessing';
import Metrics from './pages/Metrics';
import SettingsPage from './pages/Settings';
import AboutPage from './pages/About';
import Help from './pages/Help';
import ApiDocs from './pages/ApiDocs';

// Components
import ErrorBoundary from './components/ErrorBoundary';
import { ensureDaemonRunning } from './lib/daemon';

// ============================================================================
// Main App Component
// ============================================================================

function App() {
  React.useEffect(() => { void ensureDaemonRunning(); }, []);

  return (
    <ErrorBoundary>
        <BrowserRouter>
          <Routes>
            <Route element={<Layout />}>
              {/* Dashboard */}
              <Route path="/" element={<Dashboard />} />

              {/* GenAI Studio */}
              <Route path="/chat" element={<Chat />} />
              <Route path="/images" element={<Images />} />
              <Route path="/vision" element={<Vision />} />

              {/* Model Hub */}
              <Route path="/models" element={<Models />} />
              <Route path="/compare" element={<Compare />} />
              <Route path="/switch" element={<QuickSwitch />} />

              {/* Advanced */}
              <Route path="/contexts" element={<Contexts />} />
              {/* Developer */}
              <Route path="/playground" element={<Playground />} />
              <Route path="/batch" element={<BatchProcessing />} />
              <Route path="/metrics" element={<Metrics />} />

              {/* Settings & Help */}
              <Route path="/settings" element={<SettingsPage />} />
              <Route path="/about" element={<AboutPage />} />
              <Route path="/help" element={<Help />} />
              <Route path="/docs/api" element={<ApiDocs />} />
            </Route>
          </Routes>
        </BrowserRouter>
    </ErrorBoundary>
  );
}

export default App;
