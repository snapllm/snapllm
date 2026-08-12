import React from 'react';
import { Prism as SyntaxHighlighter } from 'react-syntax-highlighter';
import { oneDark, oneLight } from 'react-syntax-highlighter/dist/esm/styles/prism';

export interface SyntaxCodeBlockProps {
  language: string;
  value: string;
  dark: boolean;
}

const SyntaxCodeBlock: React.FC<SyntaxCodeBlockProps> = ({ language, value, dark }) => (
  <SyntaxHighlighter
    language={language || 'text'}
    style={dark ? oneDark : oneLight}
    customStyle={{ margin: 0, padding: '1rem', fontSize: '0.875rem', lineHeight: '1.5', borderRadius: 0 }}
    showLineNumbers={value.split('\n').length > 3}
  >
    {value}
  </SyntaxHighlighter>
);

export default SyntaxCodeBlock;
