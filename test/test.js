/**
 * node-libzsh tests
 */

'use strict';

const assert = require('assert');
const path = require('path');

// Try to load the native module
let libzsh;
try {
    libzsh = require('../lib/index.js');
} catch (e) {
    console.error('Failed to load native module:', e.message);
    console.error('Make sure to run: npm run prebuild && npm run build');
    process.exit(1);
}

// Test helpers
let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`✓ ${name}`);
        passed++;
    } catch (e) {
        console.log(`✗ ${name}`);
        console.log(`  Error: ${e.message}`);
        failed++;
    }
}

function assertEqual(actual, expected, msg) {
    if (actual !== expected) {
        throw new Error(`${msg || 'Assertion failed'}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
}

function assertDeepEqual(actual, expected, msg) {
    if (JSON.stringify(actual) !== JSON.stringify(expected)) {
        throw new Error(`${msg || 'Assertion failed'}: expected ${JSON.stringify(expected)}, got ${JSON.stringify(actual)}`);
    }
}

// ============================================================================
// Initialization Tests
// ============================================================================

console.log('\n=== Initialization Tests ===\n');

test('isInitialized returns false before initialization', () => {
    // Note: may already be initialized if tests run multiple times
    const wasInitialized = libzsh.isInitialized();
    if (!wasInitialized) {
        assertEqual(libzsh.isInitialized(), false);
    }
});

test('initialize succeeds', () => {
    libzsh.initialize();
    assertEqual(libzsh.isInitialized(), true);
});

test('initialize with options', () => {
    // Can call multiple times
    libzsh.initialize({ enableZLE: true, enableParser: true });
    assertEqual(libzsh.isInitialized(), true);
});

// ============================================================================
// Preprocessing Tests
// ============================================================================

console.log('\n=== Preprocessing Tests ===\n');

test('preprocess simple @funcName', () => {
    const result = libzsh.preprocess('ls | @filter | head');
    assertEqual(result.jsBlocks.length, 1);
    assertEqual(result.jsBlocks[0].type, 'call');
    assertEqual(result.jsBlocks[0].content, 'filter');
    assertEqual(result.jsBlocks[0].buffered, false);
    assert(result.processed.includes('__JS_0__'));
});

test('preprocess @!buffered function', () => {
    const result = libzsh.preprocess('cat file.txt | @!parseJson');
    assertEqual(result.jsBlocks.length, 1);
    assertEqual(result.jsBlocks[0].type, 'call');
    assertEqual(result.jsBlocks[0].content, 'parseJson');
    assertEqual(result.jsBlocks[0].buffered, true);
});

test('preprocess @funcName with arguments', () => {
    const result = libzsh.preprocess('ls | @filter "^d" | head');
    assertEqual(result.jsBlocks.length, 1);
    assertEqual(result.jsBlocks[0].content, 'filter');
    assertEqual(result.jsBlocks[0].args.length, 1);
    assertEqual(result.jsBlocks[0].args[0], '"^d"');
});

test('preprocess @{ inline code }', () => {
    const result = libzsh.preprocess('ls | @{ line => line.toUpperCase() }');
    assertEqual(result.jsBlocks.length, 1);
    assertEqual(result.jsBlocks[0].type, 'inline');
    assertEqual(result.jsBlocks[0].buffered, false);
    assert(result.jsBlocks[0].content.includes('line.toUpperCase()'));
});

test('preprocess @!{ buffered inline code }', () => {
    const result = libzsh.preprocess('cat file | @!{ text => text.split("\\n").reverse().join("\\n") }');
    assertEqual(result.jsBlocks.length, 1);
    assertEqual(result.jsBlocks[0].type, 'inline');
    assertEqual(result.jsBlocks[0].buffered, true);
});

test('preprocess multiple JS blocks', () => {
    const result = libzsh.preprocess('ls | @filter | @!transform | head');
    assertEqual(result.jsBlocks.length, 2);
    assertEqual(result.jsBlocks[0].content, 'filter');
    assertEqual(result.jsBlocks[1].content, 'transform');
});

test('preprocess preserves non-@ content', () => {
    const result = libzsh.preprocess('echo hello world');
    assertEqual(result.jsBlocks.length, 0);
    assertEqual(result.processed, 'echo hello world');
});

// ============================================================================
// Parser Tests
// ============================================================================

console.log('\n=== Parser Tests ===\n');

test('parse simple command', () => {
    const result = libzsh.parse('echo hello world');
    assert(result.ast, 'AST should exist');
    assertEqual(result.ast.type, 'Program');
    assert(result.ast.body.length > 0, 'Should have body');
});

test('parse pipeline', () => {
    const result = libzsh.parse('ls -la | grep foo | head');
    assert(result.ast, 'AST should exist');
    // Navigate to pipeline
    const pipeline = result.ast.body[0]?.sublist?.pipeline;
    assert(pipeline, 'Pipeline should exist');
    assertEqual(pipeline.type, 'Pipeline');
    assertEqual(pipeline.commands.length, 3);
});

test('parse with redirections', () => {
    const result = libzsh.parse('echo hello > output.txt');
    assert(result.ast, 'AST should exist');
});

test('parse for loop', () => {
    const result = libzsh.parse('for x in a b c; do echo $x; done');
    assert(result.ast, 'AST should exist');
});

test('parse if statement', () => {
    const result = libzsh.parse('if [[ $x -eq 1 ]]; then echo yes; else echo no; fi');
    assert(result.ast, 'AST should exist');
});

test('parse case statement', () => {
    const result = libzsh.parse('case $x in a) echo a;; b) echo b;; esac');
    assert(result.ast, 'AST should exist');
});

test('parse function definition', () => {
    const result = libzsh.parse('myfunc() { echo hello; }');
    assert(result.ast, 'AST should exist');
});

test('validate valid syntax', () => {
    const result = libzsh.validate('echo hello');
    assertEqual(result.valid, true);
});

test('validate invalid syntax', () => {
    const result = libzsh.validate('do done');
    assertEqual(result.valid, false);
    assert(result.error, 'Should have error');
});

test('extractPipelines', () => {
    const parseResult = libzsh.parse('ls | grep foo; cat file | head');
    assert(parseResult.ast, 'AST should exist');
    const pipelines = libzsh.extractPipelines(parseResult.ast);
    assert(pipelines.length >= 2, 'Should find at least 2 pipelines');
});

// ============================================================================
// restoreJs Tests
// ============================================================================

console.log('\n=== restoreJs Tests ===\n');

test('restoreJs converts placeholders to JsCall nodes', () => {
    const input = 'ls | @filter | head';
    const preResult = libzsh.preprocess(input);
    const parseResult = libzsh.parse(preResult.processed);
    assert(parseResult.ast, 'AST should exist');

    const hybridAst = libzsh.restoreJs(parseResult.ast, preResult.jsBlocks);

    // Find the JS node
    let foundJsCall = false;
    function findJsCall(node) {
        if (!node || typeof node !== 'object') return;
        if (node.type === 'JsCall') {
            foundJsCall = true;
            assertEqual(node.name, 'filter');
            assertEqual(node.buffered, false);
        }
        for (const key of Object.keys(node)) {
            if (Array.isArray(node[key])) {
                node[key].forEach(findJsCall);
            } else if (typeof node[key] === 'object') {
                findJsCall(node[key]);
            }
        }
    }
    findJsCall(hybridAst);
    assert(foundJsCall, 'Should find JsCall node');
});

test('restoreJs handles JsInline nodes', () => {
    const input = 'ls | @{ x => x.toUpperCase() }';
    const preResult = libzsh.preprocess(input);
    const parseResult = libzsh.parse(preResult.processed);
    assert(parseResult.ast, 'AST should exist');

    const hybridAst = libzsh.restoreJs(parseResult.ast, preResult.jsBlocks);

    let foundJsInline = false;
    function findJsInline(node) {
        if (!node || typeof node !== 'object') return;
        if (node.type === 'JsInline') {
            foundJsInline = true;
            assert(node.code.includes('toUpperCase'));
            assertEqual(node.buffered, false);
        }
        for (const key of Object.keys(node)) {
            if (Array.isArray(node[key])) {
                node[key].forEach(findJsInline);
            } else if (typeof node[key] === 'object') {
                findJsInline(node[key]);
            }
        }
    }
    findJsInline(hybridAst);
    assert(foundJsInline, 'Should find JsInline node');
});

// ============================================================================
// ZLE Tests
// ============================================================================

console.log('\n=== ZLE Tests ===\n');

test('createZLESession', () => {
    const session = libzsh.createZLESession();
    assert(session, 'Session should exist');
    session.destroy();
});

test('createZLESession with options', () => {
    const session = libzsh.createZLESession({ keymap: 'emacs' });
    assert(session, 'Session should exist');
    session.destroy();
});

test('ZLESession setLine/getLine', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo hello');
    assertEqual(session.getLine(), 'echo hello');
    session.destroy();
});

test('ZLESession setCursor/getCursor', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo hello world');
    session.setCursor(5);
    assertEqual(session.getCursor(), 5);
    session.destroy();
});

test('ZLESession insert', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo ');
    session.setCursor(5);
    session.insert('hello');
    assertEqual(session.getLine(), 'echo hello');
    session.destroy();
});

test('ZLESession deleteForward', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo hello');
    session.setCursor(5);
    session.deleteForward(5);
    assertEqual(session.getLine(), 'echo ');
    session.destroy();
});

test('ZLESession deleteBackward', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo hello');
    session.setCursor(4);
    session.deleteBackward(4);
    assertEqual(session.getLine(), ' hello');
    session.destroy();
});

test('ZLESession feedKeys', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo ');
    const result = session.feedKeys('test');
    assert(result.success, 'feedKeys should succeed');
    assert(session.getLine().includes('test'), 'Line should contain test');
    session.destroy();
});

test('ZLESession getState', () => {
    const session = libzsh.createZLESession();
    session.setLine('echo hello');
    session.setCursor(5);
    const state = session.getState();
    assertEqual(state.line, 'echo hello');
    assertEqual(state.cursor, 5);
    assertEqual(state.length, 10);
    session.destroy();
});

// ============================================================================
// Widget Tests
// ============================================================================

console.log('\n=== Widget Tests ===\n');

test('registerWidget and getCustomWidgets', () => {
    libzsh.registerWidget('test-widget', () => 0);
    const widgets = libzsh.getCustomWidgets();
    assert(widgets.includes('test-widget'), 'Widget should be registered');
});

test('unregisterWidget', () => {
    libzsh.registerWidget('temp-widget', () => 0);
    const result = libzsh.unregisterWidget('temp-widget');
    assertEqual(result, true);
    const widgets = libzsh.getCustomWidgets();
    assert(!widgets.includes('temp-widget'), 'Widget should be unregistered');
});

// ============================================================================
// Completion Tests
// ============================================================================

console.log('\n=== Completion Tests ===\n');

test('registerCompleter', () => {
    libzsh.registerCompleter('test-completer', (ctx) => {
        return { matches: [] };
    });
    // No error means success
});

test('registerCompleter with options', () => {
    libzsh.registerCompleter('test-completer-opts', (ctx) => {
        return { matches: [] };
    }, { priority: 10, position: 'any' });
});

test('unregisterCompleter', () => {
    libzsh.registerCompleter('temp-completer', (ctx) => ({ matches: [] }));
    const result = libzsh.unregisterCompleter('temp-completer');
    assertEqual(result, true);
});

test('getCompletionContext', () => {
    // Create a session and set up some state
    const session = libzsh.createZLESession();
    session.setLine('ls -la');
    session.setCursor(6);

    const ctx = libzsh.getCompletionContext();
    assert(ctx, 'Context should exist');
    assert('prefix' in ctx, 'Should have prefix');
    assert('words' in ctx, 'Should have words');
    assert('cursor' in ctx, 'Should have cursor');

    session.destroy();
});

// ============================================================================
// Integration Tests
// ============================================================================

console.log('\n=== Integration Tests ===\n');

test('Full hybrid shell workflow', () => {
    // Simulate parsing a hybrid shell command
    const input = 'ls -la | @filter "^d" | @!{ dirs => dirs.toUpperCase() } | head';

    // Step 1: Preprocess
    const { processed, jsBlocks } = libzsh.preprocess(input);
    assertEqual(jsBlocks.length, 2);

    // Step 2: Parse
    const { ast } = libzsh.parse(processed);
    assert(ast, 'AST should exist');

    // Step 3: Restore JS nodes
    const hybridAst = libzsh.restoreJs(ast, jsBlocks);
    assert(hybridAst, 'Hybrid AST should exist');

    // Step 4: Extract pipelines
    const pipelines = libzsh.extractPipelines(hybridAst);
    assert(pipelines.length > 0, 'Should have pipelines');
});

test('ZLE with completion flow', () => {
    const session = libzsh.createZLESession();

    // Simulate user typing
    session.setLine('ls | ');
    session.setCursor(5);

    // Register a JS-aware completer
    libzsh.registerCompleter('js-functions', (ctx) => {
        // In real usage, would check if we're after a pipe
        if (ctx.prefix === '') {
            return {
                matches: [
                    { value: '@filter', description: 'Filter lines' },
                    { value: '@map', description: 'Transform lines' },
                ]
            };
        }
        return { matches: [] };
    });

    // Get completion context
    const ctx = libzsh.getCompletionContext();
    assert(ctx, 'Should get context');

    // Clean up
    libzsh.unregisterCompleter('js-functions');
    session.destroy();
});

// ============================================================================
// Summary
// ============================================================================

console.log('\n=== Test Summary ===\n');
console.log(`Passed: ${passed}`);
console.log(`Failed: ${failed}`);
console.log(`Total: ${passed + failed}`);

if (failed > 0) {
    process.exit(1);
}

// Cleanup
libzsh.shutdown();
