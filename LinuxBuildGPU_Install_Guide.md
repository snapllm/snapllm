 Required Build Tools for Linux

  For CPU-only build:

  # Ubuntu/Debian
  sudo apt update
  sudo apt install -y build-essential cmake git

  # Fedora
  sudo dnf install -y gcc-c++ cmake git

  # Arch
  sudo pacman -S base-devel cmake git

  For GPU build (CUDA acceleration):

  # Same as above, plus:

  # Install CUDA Toolkit (Ubuntu example)
  # Option 1: From NVIDIA repos
  wget
  https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2204/x86_64/cuda-keyring_1.1-1_all.deb    
  sudo dpkg -i cuda-keyring_1.1-1_all.deb
  sudo apt update
  sudo apt install -y cuda-toolkit-12-6

  # Option 2: Via package manager (may be older version)
  sudo apt install -y nvidia-cuda-toolkit

  Summary of Requirements

  | Build Type  | Requirements                                    |
  |-------------|-------------------------------------------------|
  | CPU         | GCC 11+, CMake 3.18+, Git                       |
  | GPU         | Above + CUDA Toolkit 12.x, NVIDIA GPU (CC 6.0+) |
  | API Server  | Python 3.10+                                    |
  | Desktop App | Node.js 18+                                     |

  Build Commands

  # Clone and setup
  git clone <repo-url>
  cd SnapLLM
  git submodule update --init --recursive

  # Build
  ./build.sh        # Auto-detect GPU/CPU
  ./build.sh cpu    # Force CPU-only (no CUDA needed)
  ./build.sh gpu    # Force GPU (requires CUDA)

  The ./build.sh cpu option allows building without CUDA if you don't have an NVIDIA GPU or don't want     
  GPU acceleration.
  