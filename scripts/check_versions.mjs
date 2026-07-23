import fs from 'node:fs';

const cmake = fs.readFileSync('CMakeLists.txt', 'utf8');
const match = cmake.match(/project\(SnapLLM VERSION (\d+\.\d+\.\d+)/);
if (!match) {
  throw new Error('Could not read the canonical version from CMakeLists.txt');
}

const expected = match[1];
const packageJson = JSON.parse(fs.readFileSync('desktop-app/package.json', 'utf8'));
const packageLock = JSON.parse(fs.readFileSync('desktop-app/package-lock.json', 'utf8'));
const tauriConfig = JSON.parse(fs.readFileSync('desktop-app/src-tauri/tauri.conf.json', 'utf8'));
const cargo = fs.readFileSync('desktop-app/src-tauri/Cargo.toml', 'utf8');
const cargoVersion = cargo.match(/^\s*version\s*=\s*"([^"]+)"/m)?.[1];
const resource = fs.readFileSync('src/snapllm.rc', 'utf8');
const resourceVersion = resource.match(/VALUE "ProductVersion", "(\d+\.\d+\.\d+)\.0/)?.[1];

const versions = {
  'desktop-app/package.json': packageJson.version,
  'desktop-app/package-lock.json': packageLock.version,
  'desktop-app/package-lock.json root package': packageLock.packages?.['']?.version,
  'desktop-app/src-tauri/tauri.conf.json': tauriConfig.package.version,
  'desktop-app/src-tauri/Cargo.toml': cargoVersion,
  'src/snapllm.rc': resourceVersion,
};

for (const [file, actual] of Object.entries(versions)) {
  if (actual !== expected) {
    throw new Error(`${file} declares ${actual ?? 'no version'}; expected ${expected}`);
  }
}

const versionedText = {
  'build_desktop.bat': [
    `Builder v${expected}`,
    `SnapLLM_${expected}_x64-setup.exe`,
    `SnapLLM_${expected}_x64_en-US.msi`,
  ],
  'package_release.bat': [`set "VERSION=${expected}"`],
  'package_release.sh': [`VERSION="\${1:-${expected}}"`],
  'QUICKSTART.md': [`SnapLLM HTTP Server v${expected}`],
  'docs/API.ison': [`version ${expected}`, `version "${expected}"`],
};

for (const [file, requiredStrings] of Object.entries(versionedText)) {
  const text = fs.readFileSync(file, 'utf8');
  for (const required of requiredStrings) {
    if (!text.includes(required)) {
      throw new Error(`${file} is missing canonical version surface: ${required}`);
    }
  }
}

const quickstart = fs.readFileSync('QUICKSTART.md', 'utf8');
if (!quickstart.includes('Node.js 20.19+')) {
  throw new Error('QUICKSTART.md must match the desktop Node.js >=20.19 prerequisite');
}
if (!quickstart.includes('node --version   # Should show v20.19.x or higher')) {
  throw new Error('QUICKSTART.md Node version verification snippet is stale');
}
if (!quickstart.includes(`{"status": "ok", "version": "${expected}"}`)) {
  throw new Error('QUICKSTART.md health response must match the public server contract');
}

console.log(`version_consistency: ${expected}`);
