<p align="center">
  <img src="logo_files/FULL_TRIMMED_transparent.png" alt="SnapLLM Logo" width="400"/>
</p>

<h1 align="center">High-Performance Multi-Model LLM Inference Engine with Sub-Millisecond Model Switching, </br> Switch models in a snap! with Desktop UI, CLI & API</h1>

<p align="center">
  <strong>Arxiv Paper Link to be added</strong>
</p>

# Contributing to SnapLLM

<p align="center">
  <a href="https://www.linkedin.com/company/aroora-ai-labs">
    <img src="logo_files/AROORA_315x88.png" alt="AroorA AI Lab" width="180"/>
  </a>
</p>

Thank you for your interest in contributing to SnapLLM! This document provides guidelines and instructions for contributing.

## Table of Contents

- [Code of Conduct](#code-of-conduct)
- [Getting Started](#getting-started)
- [Development Setup](#development-setup)
- [Making Changes](#making-changes)
- [Submitting a Pull Request](#submitting-a-pull-request)
- [Code Style Guidelines](#code-style-guidelines)
- [Testing](#testing)
- [Documentation](#documentation)
- [Issue Reporting](#issue-reporting)

---

## Code of Conduct

By participating in this project, you agree to abide by our Code of Conduct:

- **Be respectful**: Treat everyone with respect. No harassment, discrimination, or offensive behavior.
- **Be constructive**: Provide helpful feedback. Focus on the code, not the person.
- **Be collaborative**: Work together to improve the project. Share knowledge freely.
- **Be patient**: Remember that contributors have varying skill levels and time constraints.

---

## Getting Started

### Prerequisites

Before contributing, ensure you have:

- **Windows**: Visual Studio 2022 with C++ workload (Desktop development with C++)
- **Linux**: GCC 11+ or Clang 14+
- **CUDA Toolkit**: 12.x (for GPU builds)
- **CMake**: 3.18+
- **Git**: Latest version
- **Node.js**: 18+ (for desktop app development)

### Fork and Clone

1. Fork the repository on GitHub
2. Clone your fork:
   ```bash
   git clone --recursive https://github.com/snapllm/snapllm.git
   cd snapllm
   ```
3. Add the upstream remote:
   ```bash
   git remote add upstream https://github.com/snapllm/snapllm.git
   ```

---

## Development Setup

### Building the Project

#### Windows (GPU)

```bash
# Ensure CUDA is installed and in PATH
build_gpu.bat
```

#### Windows (CPU only)

```bash
build_cpu.bat
```

#### Linux (GPU)

```bash
chmod +x build.sh
./build.sh --cuda
```

#### Linux (CPU only)

```bash
./build.sh
```

### Running the Server

```bash
# Windows
build_gpu\bin\snapllm.exe --server --port 6930

# Linux
./build_gpu/bin/snapllm --server --port 6930
```

### Desktop App Development

```bash
cd desktop-app
npm install
npm run dev
# Open http://localhost:9780
```

### Python Bindings (Optional)

The bindings are built via pybind11 and link against the SnapLLM core library.

```bash
# Linux/macOS example
cmake -S . -B build_cpu -DSNAPLLM_ENABLE_PYTHON_BINDINGS=ON -DSNAPLLM_CUDA=OFF
cmake --build build_cpu

# The module will be emitted to:
#   build_cpu/python/snapllm_bindings.*
python -c "import sys; sys.path.insert(0, 'build_cpu/python'); import snapllm_bindings"
```

---

## Making Changes

### Branch Naming

Use descriptive branch names:

- `feature/` - New features (e.g., `feature/websocket-streaming`)
- `fix/` - Bug fixes (e.g., `fix/memory-leak-model-switch`)
- `docs/` - Documentation changes (e.g., `docs/api-examples`)
- `refactor/` - Code refactoring (e.g., `refactor/vPID-architecture`)
- `test/` - Test additions (e.g., `test/context-api`)

### Commit Messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) specification:

```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only
- `style`: Code style (formatting, semicolons, etc.)
- `refactor`: Code refactoring
- `perf`: Performance improvement
- `test`: Adding tests
- `build`: Build system changes
- `ci`: CI/CD changes
- `chore`: Other changes

**Examples:**
```
feat(api): add streaming support for chat completions
fix(model): resolve memory leak during model switching
docs(readme): add installation instructions for Linux
perf(inference): optimize KV cache lookup by 40%
```

### Keep Changes Focused

- One feature/fix per pull request
- Keep PRs small and reviewable (ideally < 400 lines)
- Split large changes into multiple PRs

---

## Submitting a Pull Request

### Before Submitting

1. **Sync with upstream**:
   ```bash
   git fetch upstream
   git rebase upstream/main
   ```

2. **Run the build**:
   ```bash
   # Windows
   build_gpu.bat

   # Linux
   ./build.sh --cuda
   ```

3. **Test your changes**:
   ```bash
   # Run the server
   ./snapllm --server --port 6930

   # Test endpoints
   curl http://localhost:6930/health
   ```

4. **Update documentation** if needed

### PR Template

When creating a PR, include:

```markdown
## Summary
Brief description of the changes.

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
How was this tested?

## Checklist
- [ ] Code builds without errors
- [ ] Tests pass
- [ ] Documentation updated
- [ ] Commit messages follow conventions
```

### Review Process

1. A maintainer will review your PR
2. Address any requested changes
3. Once approved, a maintainer will merge your PR

---

## Code Style Guidelines

### C++ Code

```cpp
// Naming conventions
class ModelManager;           // PascalCase for classes
void load_model();           // snake_case for functions
int model_count_;            // snake_case with trailing underscore for members
const int MAX_MODELS = 100;  // SCREAMING_SNAKE_CASE for constants

// Interface prefix
class IModelLoader {         // 'I' prefix for interfaces
    virtual void load() = 0;
};

// Braces on same line
if (condition) {
    // code
} else {
    // code
}

// Use smart pointers
std::unique_ptr<Model> model = std::make_unique<Model>();
std::shared_ptr<Context> ctx = std::make_shared<Context>();

// Prefer const correctness
const std::string& get_name() const;
void process(const std::vector<int>& data);

// Use explicit for single-argument constructors
explicit ModelManager(const std::string& workspace);
```

### TypeScript/React Code

```typescript
// Use functional components
const ModelCard: React.FC<ModelCardProps> = ({ model, onSelect }) => {
  return (
    <div className="model-card">
      <h3>{model.name}</h3>
    </div>
  );
};

// Use hooks appropriately
const [models, setModels] = useState<Model[]>([]);
const fetchModels = useCallback(async () => {
  // ...
}, [dependency]);

// Type everything
interface ModelCardProps {
  model: Model;
  onSelect: (model: Model) => void;
}

// Use async/await
async function loadModels(): Promise<Model[]> {
  const response = await fetch('/api/v1/models');
  return response.json();
}
```

### Formatting

- **C++**: 4 spaces indentation
- **TypeScript**: 2 spaces indentation
- **Line length**: 100 characters max
- **Files**: UTF-8 encoding, LF line endings

---

## Testing

### Manual Testing

Before submitting, manually test:

1. **Server startup**:
   ```bash
   ./snapllm --server --port 6930
   ```

2. **Model loading**:
   ```bash
   curl -X POST http://localhost:6930/api/v1/models/load \
     -H "Content-Type: application/json" \
     -d '{"model_id":"test","file_path":"/path/to/model.gguf"}'
   ```

3. **Chat completion**:
   ```bash
   curl -X POST http://localhost:6930/v1/chat/completions \
     -H "Content-Type: application/json" \
     -d '{"model":"test","messages":[{"role":"user","content":"Hello"}]}'
   ```

4. **Model switching**:
   ```bash
   curl -X POST http://localhost:6930/api/v1/models/switch \
     -H "Content-Type: application/json" \
     -d '{"model_id":"other_model"}'
   ```

### Desktop App Testing

```bash
cd desktop-app
npm run dev
# Test all pages and features
```

---

## Documentation

### When to Update Docs

- Adding new API endpoints
- Changing existing behavior
- Adding new features
- Fixing bugs that affect user experience

### Documentation Files

| File | Purpose |
|------|---------|
| `README.md` | Project overview, installation, quick start |
| `CONTRIBUTING.md` | Contribution guidelines (this file) |
| `CLAUDE.md` | Development context and architecture |
| `docs/` | Additional documentation |

### API Documentation

When adding/modifying endpoints, update:

1. `README.md` API Reference section
2. `CLAUDE.md` API Endpoints section
3. Any relevant example files

---

## Issue Reporting

### Bug Reports

Include:

1. **Description**: Clear description of the bug
2. **Steps to Reproduce**: Numbered steps to reproduce
3. **Expected Behavior**: What should happen
4. **Actual Behavior**: What actually happens
5. **Environment**: OS, CUDA version, GPU, etc.
6. **Logs**: Relevant error messages or logs

### Feature Requests

Include:

1. **Problem**: What problem does this solve?
2. **Solution**: Proposed solution
3. **Alternatives**: Other approaches considered
4. **Use Cases**: Real-world scenarios

### Labels

- `bug` - Something isn't working
- `enhancement` - New feature or request
- `documentation` - Documentation improvements
- `good first issue` - Good for newcomers
- `help wanted` - Extra attention needed
- `performance` - Performance improvements
- `question` - Further information requested

---

## Recognition

Contributors will be recognized in:

- The project's README
- Release notes for significant contributions
- GitHub Contributors page

Thank you for contributing to SnapLLM!

---

## Questions?

- Open a GitHub Discussion
- Join our Discord (if available)
- Email the maintainers

---

<p align="center">
  <strong>Happy coding!</strong>
</p>

<p align="center">
  <em>Developed by</em><br/>
  <a href="https://aroora.ai">
    <img src="logo_files/AROORA_315x91.png" alt="AroorA AI Lab" width="180"/>
  </a>
</p>


