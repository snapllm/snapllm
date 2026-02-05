# SnapLLM Desktop App

Modern desktop application for SnapLLM built with **Tauri + React + TypeScript**.

## Features

- Real-time server status monitoring
- Model management (load, switch, unload)
- Interactive chat interface with streaming
- Performance metrics dashboard
- Model comparison tools
- Beautiful, responsive UI with Tailwind CSS

## Prerequisites

- **Node.js** 18+ and npm
- **Rust** (for Tauri)
- **FastAPI Server** running on `http://localhost:8000`

## Quick Start

### 1. Start the FastAPI Server

First, start the backend API server:

```bash
cd ../api-server
python run.py --dev
```

The server will start at http://localhost:8000

### 2. Install Dependencies

```bash
npm install
```

### 3. Run in Development Mode

```bash
npm run tauri dev
```

The desktop app will launch and connect to the FastAPI server automatically.

## Project Structure

```
desktop-app/
├── src/
│   ├── lib/
│   │   └── api.ts              # FastAPI client
│   ├── hooks/
│   │   └── useApi.ts           # React Query hooks
│   ├── pages/
│   │   ├── Dashboard.tsx       # Main dashboard
│   │   ├── Models.tsx          # Model management
│   │   ├── Chat.tsx            # Chat interface
│   │   ├── Compare.tsx         # Model comparison
│   │   ├── Metrics.tsx         # Performance metrics
│   │   ├── Settings.tsx        # App settings
│   │   └── ApiDocs.tsx         # API documentation
│   ├── components/
│   │   ├── ErrorAlert.tsx      # Error display
│   │   ├── EmptyState.tsx      # Empty state UI
│   │   ├── ErrorBoundary.tsx   # Error boundary
│   │   └── LoadingScreen.tsx   # Loading screen
│   ├── App.tsx                 # Main app component
│   └── main.tsx                # Entry point
├── src-tauri/                  # Tauri backend
├── package.json
└── README.md
```

## Available Scripts

- `npm run dev` - Run Vite dev server (web only)
- `npm run tauri dev` - Run Tauri app in development mode
- `npm run build` - Build for production
- `npm run tauri build` - Build Tauri app for production

## API Integration

The app connects to the FastAPI server via:

### Configuration

Set the API URL via environment variable (optional):

```bash
# .env.local
VITE_API_URL=http://localhost:8000
```

Defaults to `http://localhost:8000` if not specified.

### API Client

The TypeScript API client is located at [src/lib/api.ts](src/lib/api.ts) and provides:

- Health check
- Model management (list, load, switch)
- Text generation (single, batch, streaming)
- WebSocket streaming support
- Error handling utilities

### React Hooks

Custom hooks in [src/hooks/useApi.ts](src/hooks/useApi.ts):

```typescript
import { useHealth, useModels, useLoadModel, useSwitchModel, useGenerateText } from './hooks/useApi';

// In your component:
const { data: health } = useHealth();
const { data: models } = useModels();
const loadModel = useLoadModel();
const switchModel = useSwitchModel();
const generate = useGenerateText();

// Usage:
loadModel.mutate({
  name: 'medicine',
  path: 'D:/Models/medicine-llm.Q8_0.gguf'
});

generate.mutate({
  prompt: 'What is diabetes?',
  max_tokens: 50
});
```

## Features Status

| Feature | Status |
|---------|--------|
| Dashboard | ✅ Complete |
| Model Management UI | 🔨 In Progress |
| Chat Interface | 🔨 In Progress |
| WebSocket Streaming | ✅ Complete |
| Model Comparison | 📋 Planned |
| Performance Metrics | 📋 Planned |
| Settings | 📋 Planned |

## Development

### Hot Reload

The app supports hot reload in development mode:
- React components auto-reload on save
- Tailwind CSS updates instantly
- API client changes require restart

### Debugging

Open DevTools in the Tauri app:
- **Right-click** → **Inspect Element**
- Or press **F12**

### API Testing

Test the FastAPI server directly:
- Swagger UI: http://localhost:8000/docs
- ReDoc: http://localhost:8000/redoc

## Building for Production

### Build Web Assets

```bash
npm run build
```

### Build Desktop App

```bash
npm run tauri build
```

The distributable will be in `src-tauri/target/release/bundle/`

## Troubleshooting

### Cannot Connect to API Server

**Error**: `ECONNREFUSED` or "Cannot connect to API server"

**Solution**:
1. Check if FastAPI server is running: `curl http://localhost:8000/health`
2. Start the server: `cd ../api-server && python run.py --dev`
3. Verify CORS is configured for desktop app in `api-server/app/core/config.py`

### WebSocket Connection Failed

**Solution**:
1. Verify FastAPI server supports WebSockets
2. Check WebSocket URL: should be `ws://localhost:8000/api/v1/generate/stream`

### Model Loading Timeout

**Solution**:
- Increase timeout in `src/lib/api.ts` (default: 10 minutes)
- Large models may take longer to load

## Architecture

```
Desktop App (Tauri + React)
        ↓ HTTP/WebSocket
FastAPI Server (Python)
        ↓ pybind11
C++ ModelManager
        ↓ llama.cpp
Inference Engine
```

## Links

- [FastAPI Server Documentation](../api-server/README.md)
- [Main Project README](../README.md)
- [Architecture Guide](../ARCHITECTURE.md)
- [Deployment Guide](../DEPLOYMENT_GUIDE.md)
