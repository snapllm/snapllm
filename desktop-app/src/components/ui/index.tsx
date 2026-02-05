// ============================================================================
// SnapLLM Enterprise - Core UI Components
// ============================================================================

import React, { forwardRef, ReactNode, ButtonHTMLAttributes, InputHTMLAttributes } from 'react';
import { motion, AnimatePresence, HTMLMotionProps } from 'framer-motion';
import { clsx } from 'clsx';
import {
  X,
  Check,
  AlertCircle,
  Info,
  AlertTriangle,
  Loader2,
  ChevronDown,
  Search,
  Eye,
  EyeOff,
} from 'lucide-react';

// ----------------------------------------------------------------------------
// Button Component
// ----------------------------------------------------------------------------

export type ButtonVariant = 'primary' | 'secondary' | 'ghost' | 'danger' | 'success' | 'gradient';
export type ButtonSize = 'xs' | 'sm' | 'md' | 'lg' | 'xl';

interface ButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
  size?: ButtonSize;
  isLoading?: boolean;
  leftIcon?: ReactNode;
  rightIcon?: ReactNode;
  fullWidth?: boolean;
}

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(
  ({
    variant = 'primary',
    size = 'md',
    isLoading,
    leftIcon,
    rightIcon,
    fullWidth,
    className,
    children,
    disabled,
    ...props
  }, ref) => {
    const baseClasses = clsx(
      `btn btn-${variant} btn-${size}`,
      fullWidth && 'w-full',
      className
    );

    return (
      <button
        ref={ref}
        className={baseClasses}
        disabled={disabled || isLoading}
        {...props}
      >
        {isLoading ? (
          <Loader2 className="w-4 h-4 animate-spin" />
        ) : leftIcon ? (
          <span className="flex-shrink-0">{leftIcon}</span>
        ) : null}
        {children}
        {rightIcon && !isLoading && (
          <span className="flex-shrink-0">{rightIcon}</span>
        )}
      </button>
    );
  }
);

Button.displayName = 'Button';

// ----------------------------------------------------------------------------
// Icon Button Component
// ----------------------------------------------------------------------------

interface IconButtonProps extends ButtonHTMLAttributes<HTMLButtonElement> {
  variant?: ButtonVariant;
  size?: ButtonSize;
  icon: ReactNode;
  label: string;
}

export const IconButton = forwardRef<HTMLButtonElement, IconButtonProps>(
  ({ variant = 'ghost', size = 'md', icon, label, className, ...props }, ref) => {
    const sizeClasses = {
      xs: 'w-8 h-8',
      sm: 'w-9 h-9',
      md: 'w-11 h-11',
      lg: 'w-12 h-12',
      xl: 'w-14 h-14',
    };

    return (
      <button
        ref={ref}
        className={clsx(
          `btn btn-${variant} btn-icon`,
          sizeClasses[size],
          className
        )}
        aria-label={label}
        title={label}
        {...props}
      >
        {icon}
      </button>
    );
  }
);

IconButton.displayName = 'IconButton';

// ----------------------------------------------------------------------------
// Input Component
// ----------------------------------------------------------------------------

interface InputProps extends InputHTMLAttributes<HTMLInputElement> {
  label?: string;
  error?: string;
  hint?: string;
  leftIcon?: ReactNode;
  rightIcon?: ReactNode;
  isPassword?: boolean;
}

export const Input = forwardRef<HTMLInputElement, InputProps>(
  ({
    label,
    error,
    hint,
    leftIcon,
    rightIcon,
    isPassword,
    className,
    type = 'text',
    id,
    ...props
  }, ref) => {
    const [showPassword, setShowPassword] = React.useState(false);
    const inputId = id || label?.toLowerCase().replace(/\s/g, '-');

    return (
      <div className="w-full">
        {label && (
          <label htmlFor={inputId} className={clsx('label', props.required && 'label-required')}>
            {label}
          </label>
        )}
        <div className="relative">
          {leftIcon && (
            <div className="absolute left-3 top-1/2 -translate-y-1/2 text-surface-400">
              {leftIcon}
            </div>
          )}
          <input
            ref={ref}
            id={inputId}
            type={isPassword ? (showPassword ? 'text' : 'password') : type}
            className={clsx(
              'input',
              leftIcon && 'pl-10',
              (rightIcon || isPassword) && 'pr-10',
              error && 'input-error',
              className
            )}
            {...props}
          />
          {isPassword && (
            <button
              type="button"
              onClick={() => setShowPassword(!showPassword)}
              className="absolute right-3 top-1/2 -translate-y-1/2 text-surface-400 hover:text-surface-600"
            >
              {showPassword ? <EyeOff className="w-4 h-4" /> : <Eye className="w-4 h-4" />}
            </button>
          )}
          {rightIcon && !isPassword && (
            <div className="absolute right-3 top-1/2 -translate-y-1/2 text-surface-400">
              {rightIcon}
            </div>
          )}
        </div>
        {error && (
          <p className="mt-1.5 text-sm text-error-600 dark:text-error-400 flex items-center gap-1">
            <AlertCircle className="w-3.5 h-3.5" />
            {error}
          </p>
        )}
        {hint && !error && (
          <p className="mt-1.5 text-sm text-surface-500">{hint}</p>
        )}
      </div>
    );
  }
);

Input.displayName = 'Input';

// ----------------------------------------------------------------------------
// Search Input Component
// ----------------------------------------------------------------------------

interface SearchInputProps extends Omit<InputProps, 'leftIcon'> {
  onClear?: () => void;
}

export const SearchInput = forwardRef<HTMLInputElement, SearchInputProps>(
  ({ onClear, value, ...props }, ref) => {
    return (
      <Input
        ref={ref}
        leftIcon={<Search className="w-4 h-4" />}
        rightIcon={
          value && onClear ? (
            <button onClick={onClear} className="hover:text-surface-600">
              <X className="w-4 h-4" />
            </button>
          ) : undefined
        }
        value={value}
        {...props}
      />
    );
  }
);

SearchInput.displayName = 'SearchInput';

// ----------------------------------------------------------------------------
// Textarea Component
// ----------------------------------------------------------------------------

interface TextareaProps extends React.TextareaHTMLAttributes<HTMLTextAreaElement> {
  label?: string;
  error?: string;
  hint?: string;
}

export const Textarea = forwardRef<HTMLTextAreaElement, TextareaProps>(
  ({ label, error, hint, className, id, ...props }, ref) => {
    const textareaId = id || label?.toLowerCase().replace(/\s/g, '-');

    return (
      <div className="w-full">
        {label && (
          <label htmlFor={textareaId} className={clsx('label', props.required && 'label-required')}>
            {label}
          </label>
        )}
        <textarea
          ref={ref}
          id={textareaId}
          className={clsx('textarea', error && 'input-error', className)}
          {...props}
        />
        {error && (
          <p className="mt-1.5 text-sm text-error-600 dark:text-error-400 flex items-center gap-1">
            <AlertCircle className="w-3.5 h-3.5" />
            {error}
          </p>
        )}
        {hint && !error && (
          <p className="mt-1.5 text-sm text-surface-500">{hint}</p>
        )}
      </div>
    );
  }
);

Textarea.displayName = 'Textarea';

// ----------------------------------------------------------------------------
// Select Component
// ----------------------------------------------------------------------------

interface SelectOption {
  value: string;
  label: string;
  disabled?: boolean;
}

interface SelectProps extends React.SelectHTMLAttributes<HTMLSelectElement> {
  label?: string;
  error?: string;
  hint?: string;
  options: SelectOption[];
  placeholder?: string;
}

export const Select = forwardRef<HTMLSelectElement, SelectProps>(
  ({ label, error, hint, options, placeholder, className, id, ...props }, ref) => {
    const selectId = id || label?.toLowerCase().replace(/\s/g, '-');

    return (
      <div className="w-full">
        {label && (
          <label htmlFor={selectId} className={clsx('label', props.required && 'label-required')}>
            {label}
          </label>
        )}
        <select
          ref={ref}
          id={selectId}
          className={clsx('select', error && 'input-error', className)}
          {...props}
        >
          {placeholder && (
            <option value="" disabled>
              {placeholder}
            </option>
          )}
          {options.map((option) => (
            <option key={option.value} value={option.value} disabled={option.disabled}>
              {option.label}
            </option>
          ))}
        </select>
        {error && (
          <p className="mt-1.5 text-sm text-error-600 dark:text-error-400 flex items-center gap-1">
            <AlertCircle className="w-3.5 h-3.5" />
            {error}
          </p>
        )}
        {hint && !error && (
          <p className="mt-1.5 text-sm text-surface-500">{hint}</p>
        )}
      </div>
    );
  }
);

Select.displayName = 'Select';

// ----------------------------------------------------------------------------
// Badge Component
// ----------------------------------------------------------------------------

type BadgeVariant = 'default' | 'brand' | 'success' | 'warning' | 'error' | 'info';
type BadgeSize = 'sm' | 'md';

interface BadgeProps {
  variant?: BadgeVariant;
  size?: BadgeSize;
  children: ReactNode;
  dot?: boolean;
  className?: string;
}

export const Badge: React.FC<BadgeProps> = ({
  variant = 'default',
  size = 'md',
  children,
  dot,
  className
}) => {
  return (
    <span
      className={clsx(
        `badge badge-${variant}`,
        size === 'sm' && 'text-[10px] px-1.5 py-0.5',
        className
      )}
    >
      {dot && (
        <span className={clsx(
          'w-1.5 h-1.5 rounded-full',
          variant === 'success' && 'bg-success-500',
          variant === 'warning' && 'bg-warning-500',
          variant === 'error' && 'bg-error-500',
          variant === 'info' && 'bg-blue-500',
          variant === 'brand' && 'bg-brand-500',
          variant === 'default' && 'bg-surface-500',
        )} />
      )}
      {children}
    </span>
  );
};

// ----------------------------------------------------------------------------
// Card Component
// ----------------------------------------------------------------------------

interface CardProps extends HTMLMotionProps<'div'> {
  variant?: 'default' | 'hover' | 'interactive' | 'glass' | 'gradient';
  padding?: 'none' | 'sm' | 'md' | 'lg';
  children: ReactNode;
}

export const Card: React.FC<CardProps> = ({
  variant = 'default',
  padding = 'md',
  className,
  children,
  ...props
}) => {
  const paddingClasses = {
    none: '',
    sm: 'p-4',
    md: 'p-6',
    lg: 'p-8',
  };

  const variantClasses = {
    default: 'card',
    hover: 'card-hover',
    interactive: 'card-interactive',
    glass: 'card-glass',
    gradient: 'card-gradient',
  };

  return (
    <motion.div
      className={clsx(variantClasses[variant], paddingClasses[padding], className)}
      {...props}
    >
      {children}
    </motion.div>
  );
};

// ----------------------------------------------------------------------------
// Modal Component
// ----------------------------------------------------------------------------

interface ModalProps {
  isOpen: boolean;
  onClose: () => void;
  title?: string;
  description?: string;
  size?: 'sm' | 'md' | 'lg' | 'xl' | 'full';
  children: ReactNode;
  footer?: ReactNode;
}

export const Modal: React.FC<ModalProps> = ({
  isOpen,
  onClose,
  title,
  description,
  size = 'md',
  children,
  footer,
}) => {
  const sizeClasses = {
    sm: 'max-w-sm',
    md: 'max-w-lg',
    lg: 'max-w-2xl',
    xl: 'max-w-4xl',
    full: 'max-w-[90vw] h-[90vh]',
  };

  React.useEffect(() => {
    const handleEscape = (e: KeyboardEvent) => {
      if (e.key === 'Escape') onClose();
    };
    if (isOpen) {
      document.addEventListener('keydown', handleEscape);
      document.body.style.overflow = 'hidden';
    }
    return () => {
      document.removeEventListener('keydown', handleEscape);
      document.body.style.overflow = '';
    };
  }, [isOpen, onClose]);

  return (
    <AnimatePresence>
      {isOpen && (
        <>
          <motion.div
            className="modal-backdrop"
            initial={{ opacity: 0 }}
            animate={{ opacity: 1 }}
            exit={{ opacity: 0 }}
            onClick={onClose}
          />
          <div className="modal-container">
            <motion.div
              className={clsx('modal', sizeClasses[size])}
              initial={{ opacity: 0, scale: 0.95, y: 20 }}
              animate={{ opacity: 1, scale: 1, y: 0 }}
              exit={{ opacity: 0, scale: 0.95, y: 20 }}
              transition={{ duration: 0.2 }}
            >
              {(title || description) && (
                <div className="modal-header">
                  <div>
                    {title && (
                      <h2 className="text-lg font-semibold text-surface-900 dark:text-surface-100">
                        {title}
                      </h2>
                    )}
                    {description && (
                      <p className="text-sm text-surface-500 mt-1">{description}</p>
                    )}
                  </div>
                  <IconButton
                    icon={<X className="w-5 h-5" />}
                    label="Close"
                    onClick={onClose}
                    size="sm"
                  />
                </div>
              )}
              <div className="modal-body">{children}</div>
              {footer && <div className="modal-footer">{footer}</div>}
            </motion.div>
          </div>
        </>
      )}
    </AnimatePresence>
  );
};

// ----------------------------------------------------------------------------
// Alert Component
// ----------------------------------------------------------------------------

type AlertVariant = 'info' | 'success' | 'warning' | 'error';

interface AlertProps {
  variant?: AlertVariant;
  title?: string;
  children: ReactNode;
  onClose?: () => void;
  className?: string;
}

export const Alert: React.FC<AlertProps> = ({
  variant = 'info',
  title,
  children,
  onClose,
  className,
}) => {
  const icons = {
    info: <Info className="w-5 h-5" />,
    success: <Check className="w-5 h-5" />,
    warning: <AlertTriangle className="w-5 h-5" />,
    error: <AlertCircle className="w-5 h-5" />,
  };

  const colors = {
    info: 'bg-blue-50 dark:bg-blue-900/20 border-blue-200 dark:border-blue-800 text-blue-800 dark:text-blue-200',
    success: 'bg-success-50 dark:bg-success-900/20 border-success-200 dark:border-success-800 text-success-800 dark:text-success-200',
    warning: 'bg-warning-50 dark:bg-warning-900/20 border-warning-200 dark:border-warning-800 text-warning-800 dark:text-warning-200',
    error: 'bg-error-50 dark:bg-error-900/20 border-error-200 dark:border-error-800 text-error-800 dark:text-error-200',
  };

  return (
    <div className={clsx('rounded-lg border p-4', colors[variant], className)}>
      <div className="flex gap-3">
        <div className="flex-shrink-0">{icons[variant]}</div>
        <div className="flex-1">
          {title && <h4 className="font-medium mb-1">{title}</h4>}
          <div className="text-sm opacity-90">{children}</div>
        </div>
        {onClose && (
          <button onClick={onClose} className="flex-shrink-0 opacity-70 hover:opacity-100">
            <X className="w-4 h-4" />
          </button>
        )}
      </div>
    </div>
  );
};

// ----------------------------------------------------------------------------
// Progress Component
// ----------------------------------------------------------------------------

interface ProgressProps {
  value: number;
  max?: number;
  size?: 'sm' | 'md' | 'lg';
  showValue?: boolean;
  variant?: 'default' | 'success' | 'warning' | 'error';
  className?: string;
}

export const Progress: React.FC<ProgressProps> = ({
  value,
  max = 100,
  size = 'md',
  showValue,
  variant = 'default',
  className,
}) => {
  const percentage = Math.min(Math.max((value / max) * 100, 0), 100);

  const sizeClasses = {
    sm: 'h-1',
    md: 'h-2',
    lg: 'h-3',
  };

  const colorClasses = {
    default: 'bg-brand-500',
    success: 'bg-success-500',
    warning: 'bg-warning-500',
    error: 'bg-error-500',
  };

  return (
    <div className={clsx('w-full', className)}>
      <div className={clsx('progress', sizeClasses[size])}>
        <motion.div
          className={clsx('progress-bar', colorClasses[variant])}
          initial={{ width: 0 }}
          animate={{ width: `${percentage}%` }}
          transition={{ duration: 0.5, ease: 'easeOut' }}
        />
      </div>
      {showValue && (
        <div className="flex justify-between mt-1 text-xs text-surface-500">
          <span>{value}</span>
          <span>{max}</span>
        </div>
      )}
    </div>
  );
};

// ----------------------------------------------------------------------------
// Skeleton Component
// ----------------------------------------------------------------------------

interface SkeletonProps {
  width?: string | number;
  height?: string | number;
  circle?: boolean;
  className?: string;
}

export const Skeleton: React.FC<SkeletonProps> = ({
  width,
  height,
  circle,
  className,
}) => {
  return (
    <div
      className={clsx('skeleton', circle && 'rounded-full', className)}
      style={{ width, height }}
    />
  );
};

// ----------------------------------------------------------------------------
// Avatar Component
// ----------------------------------------------------------------------------

interface AvatarProps {
  src?: string;
  name?: string;
  size?: 'xs' | 'sm' | 'md' | 'lg' | 'xl';
  status?: 'online' | 'offline' | 'busy' | 'away';
  className?: string;
}

export const Avatar: React.FC<AvatarProps> = ({
  src,
  name,
  size = 'md',
  status,
  className,
}) => {
  const sizeClasses = {
    xs: 'w-6 h-6 text-xs',
    sm: 'w-8 h-8 text-sm',
    md: 'w-10 h-10 text-base',
    lg: 'w-12 h-12 text-lg',
    xl: 'w-16 h-16 text-xl',
  };

  const statusColors = {
    online: 'bg-success-500',
    offline: 'bg-surface-400',
    busy: 'bg-error-500',
    away: 'bg-warning-500',
  };

  const initials = name
    ?.split(' ')
    .map((n) => n[0])
    .join('')
    .toUpperCase()
    .slice(0, 2);

  return (
    <div className={clsx('relative inline-flex', className)}>
      {src ? (
        <img
          src={src}
          alt={name || 'Avatar'}
          className={clsx(
            'rounded-full object-cover',
            sizeClasses[size]
          )}
        />
      ) : (
        <div
          className={clsx(
            'rounded-full flex items-center justify-center font-medium',
            'bg-gradient-to-br from-brand-500 to-ai-purple text-white',
            sizeClasses[size]
          )}
        >
          {initials || '?'}
        </div>
      )}
      {status && (
        <span
          className={clsx(
            'absolute bottom-0 right-0 rounded-full ring-2 ring-white dark:ring-surface-900',
            size === 'xs' || size === 'sm' ? 'w-2 h-2' : 'w-3 h-3',
            statusColors[status]
          )}
        />
      )}
    </div>
  );
};

// ----------------------------------------------------------------------------
// Tabs Component
// ----------------------------------------------------------------------------

interface Tab {
  id: string;
  label: string;
  icon?: ReactNode;
  badge?: string | number;
}

interface TabsProps {
  tabs: Tab[];
  activeTab: string;
  onChange: (tabId: string) => void;
  variant?: 'pills' | 'underline' | 'enclosed';
  className?: string;
}

export const Tabs: React.FC<TabsProps> = ({
  tabs,
  activeTab,
  onChange,
  variant = 'pills',
  className,
}) => {
  if (variant === 'pills') {
    return (
      <div className={clsx('tabs', className)}>
        {tabs.map((tab) => (
          <button
            key={tab.id}
            onClick={() => onChange(tab.id)}
            className={clsx(activeTab === tab.id ? 'tab-active' : 'tab')}
          >
            {tab.icon}
            {tab.label}
            {tab.badge && (
              <span className="ml-1.5 badge-default text-2xs">{tab.badge}</span>
            )}
          </button>
        ))}
      </div>
    );
  }

  if (variant === 'underline') {
    return (
      <div className={clsx('flex border-b border-surface-200 dark:border-surface-800', className)}>
        {tabs.map((tab) => (
          <button
            key={tab.id}
            onClick={() => onChange(tab.id)}
            className={clsx(
              'px-4 py-2.5 text-sm font-medium border-b-2 -mb-px transition-colors',
              activeTab === tab.id
                ? 'border-brand-500 text-brand-600 dark:text-brand-400'
                : 'border-transparent text-surface-500 hover:text-surface-700 dark:hover:text-surface-300'
            )}
          >
            <span className="flex items-center gap-2">
              {tab.icon}
              {tab.label}
              {tab.badge && <Badge variant="brand">{tab.badge}</Badge>}
            </span>
          </button>
        ))}
      </div>
    );
  }

  return null;
};

// ----------------------------------------------------------------------------
// Toggle/Switch Component
// ----------------------------------------------------------------------------

interface ToggleProps {
  checked: boolean;
  onChange: (checked: boolean) => void;
  label?: string;
  description?: string;
  disabled?: boolean;
  size?: 'sm' | 'md' | 'lg';
}

export const Toggle: React.FC<ToggleProps> = ({
  checked,
  onChange,
  label,
  description,
  disabled,
  size = 'md',
}) => {
  const sizes = {
    sm: { track: 'w-8 h-4', thumb: 'w-3 h-3', translate: 'translate-x-4' },
    md: { track: 'w-11 h-6', thumb: 'w-5 h-5', translate: 'translate-x-5' },
    lg: { track: 'w-14 h-7', thumb: 'w-6 h-6', translate: 'translate-x-7' },
  };

  const handleClick = (e: React.MouseEvent) => {
    // Prevent double-toggle when Toggle is inside a label element
    e.preventDefault();
    e.stopPropagation();
    if (!disabled) {
      onChange(!checked);
    }
  };

  // If no label/description, just render the toggle button without wrapper
  if (!label && !description) {
    return (
      <button
        type="button"
        role="switch"
        aria-checked={checked}
        disabled={disabled}
        onClick={handleClick}
        className={clsx(
          'relative inline-flex flex-shrink-0 rounded-full transition-colors duration-200',
          sizes[size].track,
          checked ? 'bg-brand-600' : 'bg-surface-300 dark:bg-surface-700',
          disabled && 'opacity-50 cursor-not-allowed'
        )}
      >
        <span
          className={clsx(
            'inline-block rounded-full bg-white shadow transform transition-transform duration-200',
            sizes[size].thumb,
            'translate-x-0.5 translate-y-0.5',
            checked && sizes[size].translate
          )}
        />
      </button>
    );
  }

  // With label/description, render with div wrapper (not label to avoid conflicts)
  return (
    <div className={clsx('flex items-start gap-3', disabled && 'opacity-50 cursor-not-allowed')}>
      <button
        type="button"
        role="switch"
        aria-checked={checked}
        disabled={disabled}
        onClick={handleClick}
        className={clsx(
          'relative inline-flex flex-shrink-0 rounded-full transition-colors duration-200',
          sizes[size].track,
          checked ? 'bg-brand-600' : 'bg-surface-300 dark:bg-surface-700'
        )}
      >
        <span
          className={clsx(
            'inline-block rounded-full bg-white shadow transform transition-transform duration-200',
            sizes[size].thumb,
            'translate-x-0.5 translate-y-0.5',
            checked && sizes[size].translate
          )}
        />
      </button>
      <div className="flex-1">
        {label && (
          <span className="text-sm font-medium text-surface-900 dark:text-surface-100">
            {label}
          </span>
        )}
        {description && (
          <p className="text-sm text-surface-500 mt-0.5">{description}</p>
        )}
      </div>
    </div>
  );
};

// ----------------------------------------------------------------------------
// Slider Component
// ----------------------------------------------------------------------------

interface SliderProps {
  value: number;
  onChange: (value: number) => void;
  min?: number;
  max?: number;
  step?: number;
  label?: string;
  showValue?: boolean;
  formatValue?: (value: number) => string;
  className?: string;
}

export const Slider: React.FC<SliderProps> = ({
  value,
  onChange,
  min = 0,
  max = 100,
  step = 1,
  label,
  showValue = true,
  formatValue = (v) => String(v),
  className,
}) => {
  const percentage = ((value - min) / (max - min)) * 100;

  return (
    <div className={clsx('w-full', className)}>
      {(label || showValue) && (
        <div className="flex justify-between mb-2">
          {label && <span className="text-sm font-medium text-surface-700 dark:text-surface-300">{label}</span>}
          {showValue && <span className="text-sm text-surface-500">{formatValue(value)}</span>}
        </div>
      )}
      <input
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(e) => onChange(Number(e.target.value))}
        className="w-full h-2 bg-surface-200 dark:bg-surface-700 rounded-full appearance-none cursor-pointer
          [&::-webkit-slider-thumb]:appearance-none
          [&::-webkit-slider-thumb]:w-4
          [&::-webkit-slider-thumb]:h-4
          [&::-webkit-slider-thumb]:rounded-full
          [&::-webkit-slider-thumb]:bg-brand-600
          [&::-webkit-slider-thumb]:shadow-md
          [&::-webkit-slider-thumb]:cursor-pointer
          [&::-webkit-slider-thumb]:transition-transform
          [&::-webkit-slider-thumb]:hover:scale-110
        "
        style={{
          background: `linear-gradient(to right, rgb(14 165 233) ${percentage}%, rgb(228 228 231) ${percentage}%)`,
        }}
      />
    </div>
  );
};

// ----------------------------------------------------------------------------
// Tooltip Component
// ----------------------------------------------------------------------------

interface TooltipProps {
  content: ReactNode;
  children: ReactNode;
  position?: 'top' | 'bottom' | 'left' | 'right';
  delay?: number;
}

export const Tooltip: React.FC<TooltipProps> = ({
  content,
  children,
  position = 'top',
  delay = 200,
}) => {
  const [isVisible, setIsVisible] = React.useState(false);
  const timeoutRef = React.useRef<NodeJS.Timeout>();

  const show = () => {
    timeoutRef.current = setTimeout(() => setIsVisible(true), delay);
  };

  const hide = () => {
    clearTimeout(timeoutRef.current);
    setIsVisible(false);
  };

  const positions = {
    top: 'bottom-full left-1/2 -translate-x-1/2 mb-2',
    bottom: 'top-full left-1/2 -translate-x-1/2 mt-2',
    left: 'right-full top-1/2 -translate-y-1/2 mr-2',
    right: 'left-full top-1/2 -translate-y-1/2 ml-2',
  };

  return (
    <div className="relative inline-flex" onMouseEnter={show} onMouseLeave={hide}>
      {children}
      <AnimatePresence>
        {isVisible && (
          <motion.div
            initial={{ opacity: 0, scale: 0.95 }}
            animate={{ opacity: 1, scale: 1 }}
            exit={{ opacity: 0, scale: 0.95 }}
            transition={{ duration: 0.1 }}
            className={clsx(
              'absolute z-50 px-2 py-1 rounded-md whitespace-nowrap',
              'bg-surface-900 dark:bg-surface-100 text-white dark:text-surface-900',
              'text-xs shadow-lg',
              positions[position]
            )}
          >
            {content}
          </motion.div>
        )}
      </AnimatePresence>
    </div>
  );
};

// ----------------------------------------------------------------------------
// Empty State Component
// ----------------------------------------------------------------------------

interface EmptyStateProps {
  icon?: ReactNode;
  title: string;
  description?: string;
  action?: ReactNode;
  className?: string;
}

export const EmptyState: React.FC<EmptyStateProps> = ({
  icon,
  title,
  description,
  action,
  className,
}) => {
  return (
    <div className={clsx('empty-state', className)}>
      {icon && <div className="empty-state-icon">{icon}</div>}
      <h3 className="empty-state-title">{title}</h3>
      {description && <p className="empty-state-description">{description}</p>}
      {action && <div className="mt-6">{action}</div>}
    </div>
  );
};

// ----------------------------------------------------------------------------
// Loading Spinner Component
// ----------------------------------------------------------------------------

interface SpinnerProps {
  size?: 'sm' | 'md' | 'lg';
  className?: string;
}

export const Spinner: React.FC<SpinnerProps> = ({ size = 'md', className }) => {
  const sizes = {
    sm: 'w-4 h-4',
    md: 'w-6 h-6',
    lg: 'w-8 h-8',
  };

  return (
    <Loader2 className={clsx('animate-spin text-brand-500', sizes[size], className)} />
  );
};

// ----------------------------------------------------------------------------
// Thinking Dots Animation
// ----------------------------------------------------------------------------

export const ThinkingDots: React.FC<{ className?: string }> = ({ className }) => {
  return (
    <div className={clsx('thinking-dots', className)}>
      <span />
      <span />
      <span />
    </div>
  );
};

// ----------------------------------------------------------------------------
// Divider Component
// ----------------------------------------------------------------------------

interface DividerProps {
  orientation?: 'horizontal' | 'vertical';
  className?: string;
}

export const Divider: React.FC<DividerProps> = ({ orientation = 'horizontal', className }) => {
  return (
    <div className={clsx(orientation === 'horizontal' ? 'divider' : 'divider-vertical', className)} />
  );
};

// ----------------------------------------------------------------------------
// Keyboard Shortcut Display
// ----------------------------------------------------------------------------

interface KbdProps {
  children: ReactNode;
  className?: string;
}

export const Kbd: React.FC<KbdProps> = ({ children, className }) => {
  return (
    <kbd
      className={clsx(
        'inline-flex items-center justify-center px-1.5 py-0.5 rounded',
        'bg-surface-100 dark:bg-surface-800 border border-surface-300 dark:border-surface-600',
        'text-xs font-mono text-surface-600 dark:text-surface-400',
        'shadow-sm',
        className
      )}
    >
      {children}
    </kbd>
  );
};
