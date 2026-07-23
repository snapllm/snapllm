/// <reference types="vite/client" />

declare const __APP_VERSION__: string;

// Declare PNG image imports
declare module '*.png' {
  const value: string;
  export default value;
}

// Declare other image formats
declare module '*.jpg' {
  const value: string;
  export default value;
}

declare module '*.jpeg' {
  const value: string;
  export default value;
}

declare module '*.svg' {
  const value: string;
  export default value;
}

declare module '*.gif' {
  const value: string;
  export default value;
}

declare module '*.webp' {
  const value: string;
  export default value;
}
