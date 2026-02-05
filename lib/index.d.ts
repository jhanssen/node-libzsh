/**
 * node-libzsh - Node.js native module exposing libzsh parser and ZLE
 */

// ============================================================================
// AST Types
// ============================================================================

export interface ZshAstNode {
  type: string;
}

export interface Program extends ZshAstNode {
  type: 'Program';
  body: List[];
  wordcode?: Uint32Array;
}

export interface List extends ZshAstNode {
  type: 'List';
  sublist: Sublist;
  async: boolean;
  next?: List;
}

export interface Sublist extends ZshAstNode {
  type: 'Sublist';
  conjunction: 'end' | 'and' | 'or';
  negate: boolean;
  coproc?: boolean;
  pipeline: Pipeline;
  next?: Sublist;
}

export interface Pipeline extends ZshAstNode {
  type: 'Pipeline';
  commands: PipelineCommand[];
}

export interface PipelineCommand extends ZshAstNode {
  type: 'PipelineCommand';
  position: 'first' | 'middle' | 'last' | 'only';
  command: Command;
}

export type Command =
  | SimpleCommand
  | Subshell
  | BraceGroup
  | ForLoop
  | WhileLoop
  | IfStatement
  | CaseStatement
  | ConditionalExpression
  | ArithmeticCommand
  | FunctionDef
  | TryBlock
  | Typeset
  | TimedCommand
  | JsCall
  | JsInline;

export interface SimpleCommand extends ZshAstNode {
  type: 'SimpleCommand';
  words: string[];
  assignments?: Assignment[];
  redirections?: Redirection[];
}

export interface Subshell extends ZshAstNode {
  type: 'Subshell';
  body: List;
  redirections?: Redirection[];
}

export interface BraceGroup extends ZshAstNode {
  type: 'BraceGroup';
  body: List;
  redirections?: Redirection[];
}

export interface ForLoop extends ZshAstNode {
  type: 'ForLoop';
  style: 'in' | 'cstyle';
  variable?: string;
  list?: string[];
  init?: string;
  condition?: string;
  update?: string;
  body: List;
  redirections?: Redirection[];
}

export interface WhileLoop extends ZshAstNode {
  type: 'WhileLoop';
  until: boolean;
  condition: List;
  body: List;
  redirections?: Redirection[];
}

export interface IfStatement extends ZshAstNode {
  type: 'IfStatement';
  clauses: IfClause[];
  redirections?: Redirection[];
}

export interface IfClause {
  type: 'if' | 'elif' | 'else';
  condition?: List;
  body: List;
}

export interface CaseStatement extends ZshAstNode {
  type: 'CaseStatement';
  word: string;
  cases: CaseItem[];
  redirections?: Redirection[];
}

export interface CaseItem {
  pattern: string;
  terminator: ';;' | ';&' | ';|';
  body?: List;
}

export interface ConditionalExpression extends ZshAstNode {
  type: 'ConditionalExpression';
  op: string;
  left?: string | ConditionalExpression;
  right?: string | ConditionalExpression;
  operand?: string | ConditionalExpression;
}

export interface ArithmeticCommand extends ZshAstNode {
  type: 'ArithmeticCommand';
  expression: string;
  redirections?: Redirection[];
}

export interface FunctionDef extends ZshAstNode {
  type: 'FunctionDef';
  names: string[];
  body: List;
  redirections?: Redirection[];
}

export interface TryBlock extends ZshAstNode {
  type: 'TryBlock';
  try: List;
  always: List;
  redirections?: Redirection[];
}

export interface Typeset extends ZshAstNode {
  type: 'Typeset';
  words: string[];
  assignments?: Assignment[];
  redirections?: Redirection[];
}

export interface TimedCommand extends ZshAstNode {
  type: 'TimedCommand';
  pipeline?: Pipeline;
}

export interface Redirection extends ZshAstNode {
  type: 'Redirection';
  op: string;
  fd: number;
  target: string;
  varId?: string;
  heredocTerminator?: string;
}

export interface Assignment extends ZshAstNode {
  type: 'Assignment';
  name: string;
  value: string | string[];
  append: boolean;
  isArray?: boolean;
}

// Hybrid shell JS nodes
export interface JsCall extends ZshAstNode {
  type: 'JsCall';
  name: string;
  args: string[];
  buffered: boolean;
}

export interface JsInline extends ZshAstNode {
  type: 'JsInline';
  code: string;
  buffered: boolean;
}

// ============================================================================
// Parse Types
// ============================================================================

export interface ParseOptions {
  includeWordcode?: boolean;
  includeLocations?: boolean;
}

export interface ParseError {
  message: string;
  line?: number;
  column?: number;
}

export interface ParseResult {
  ast: Program | null;
  wordcode?: Uint32Array;
  errors?: ParseError[];
}

export interface ValidateResult {
  valid: boolean;
  error?: ParseError;
}

// ============================================================================
// Preprocessing Types
// ============================================================================

export interface JsBlock {
  id: string;
  original: string;
  type: 'call' | 'inline';
  buffered: boolean;
  content: string;
  args?: string[];
}

export interface PreprocessResult {
  processed: string;
  jsBlocks: JsBlock[];
}

export interface PipelineInfo {
  pipeline: Pipeline;
  commandCount: number;
}

// ============================================================================
// ZLE Types
// ============================================================================

export interface ZLESessionOptions {
  keymap?: 'emacs' | 'viins' | 'vicmd' | string;
}

export interface WidgetResult {
  success: boolean;
  error?: string;
  returnValue: number;
}

export interface KeyFeedResult {
  success: boolean;
  error?: string;
  complete: boolean;
  line: string;
}

export interface ZLEState {
  line: string;
  cursor: number;
  length: number;
}

export interface ZLESession {
  // Line buffer operations
  getLine(): string;
  setLine(text: string): void;
  getCursor(): number;
  setCursor(pos: number): void;

  // Text manipulation
  insert(text: string): void;
  deleteForward(count?: number): void;
  deleteBackward(count?: number): void;

  // Keymap operations
  getKeymap(): string;
  setKeymap(name: string): void;

  // Widget operations
  executeWidget(name: string): WidgetResult;
  bindKey(keymap: string, sequence: string, widget: string): boolean;

  // Key input
  feedKeys(keys: string | Buffer): KeyFeedResult;

  // State
  getState(): ZLEState;
  destroy(): void;
}

export type WidgetFunction = () => number | void;

// ============================================================================
// Completion Types
// ============================================================================

export interface CompletionContext {
  prefix: string;
  suffix: string;
  words: string[];
  current: number;
  line: string;
  cursor: number;
  quote: string | null;
}

export interface CompletionMatch {
  value: string;
  display?: string;
  description?: string;
  group?: string;
  suffix?: string;
  removeSuffix?: string;
}

export interface CompletionResult {
  matches: CompletionMatch[];
  exclusive?: boolean;
}

export type CompleterFunction = (ctx: CompletionContext) => CompletionResult | Promise<CompletionResult>;

export interface CompleterOptions {
  pattern?: RegExp | string;
  position?: 'command' | 'argument' | 'any';
  priority?: number;
}

// ============================================================================
// Initialization Types
// ============================================================================

export interface InitOptions {
  enableZLE?: boolean;
  enableParser?: boolean;
}

// ============================================================================
// Module Exports
// ============================================================================

/**
 * Initialize libzsh subsystems.
 * Must be called before any other operations.
 */
export function initialize(options?: InitOptions): void;

/**
 * Shutdown libzsh subsystems.
 * Should be called on module cleanup.
 */
export function shutdown(): void;

/**
 * Check if libzsh is initialized.
 */
export function isInitialized(): boolean;

/**
 * Parse zsh shell code into an AST.
 */
export function parse(input: string, options?: ParseOptions): ParseResult;

/**
 * Validate zsh shell code syntax.
 */
export function validate(input: string): ValidateResult;

/**
 * Generate shell code from an AST.
 */
export function generate(ast: Program): string;

/**
 * Extract pipeline information from an AST.
 */
export function extractPipelines(ast: Program): PipelineInfo[];

/**
 * Preprocess input to extract @ syntax for hybrid shell.
 */
export function preprocess(input: string): PreprocessResult;

/**
 * Restore JS nodes in AST from preprocessing.
 */
export function restoreJs(ast: Program, jsBlocks: JsBlock[]): Program;

/**
 * Create a new ZLE editing session.
 */
export function createZLESession(options?: ZLESessionOptions): ZLESession;

/**
 * Register a custom ZLE widget.
 */
export function registerWidget(name: string, fn: WidgetFunction): void;

/**
 * Unregister a custom widget.
 */
export function unregisterWidget(name: string): boolean;

/**
 * Get list of custom widget names.
 */
export function getCustomWidgets(): string[];

/**
 * Register a completion provider.
 */
export function registerCompleter(
  name: string,
  fn: CompleterFunction,
  options?: CompleterOptions
): void;

/**
 * Unregister a completion provider.
 */
export function unregisterCompleter(name: string): boolean;

/**
 * Get current completion context from ZLE state.
 */
export function getCompletionContext(): CompletionContext | null;

/**
 * Add completion matches.
 */
export function addCompletions(matches: CompletionMatch[], group?: string): number;
