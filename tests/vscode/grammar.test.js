'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { test, before, after } = require('node:test');
const textmate = require('../../vscode-extension/node_modules/vscode-textmate');
const oniguruma = require('../../vscode-extension/node_modules/vscode-oniguruma');

let registry;
let grammar;
before(async () => {
    const wasm = await fs.readFile(require.resolve('../../vscode-extension/node_modules/vscode-oniguruma/release/onig.wasm'));
    await oniguruma.loadWASM(wasm.buffer.slice(wasm.byteOffset, wasm.byteOffset + wasm.byteLength));
    const filename = path.resolve(__dirname, '../../vscode-extension/syntaxes/moulang.tmLanguage.json');
    registry = new textmate.Registry({
        onigLib: Promise.resolve({
            createOnigScanner: patterns => new oniguruma.OnigScanner(patterns),
            createOnigString: text => new oniguruma.OnigString(text)
        }),
        loadGrammar: async scope => scope === 'source.moulang'
            ? textmate.parseRawGrammar(await fs.readFile(filename, 'utf8'), filename) : null
    });
    grammar = await registry.loadGrammar('source.moulang');
    assert.ok(grammar);
});
after(() => registry?.dispose());

function scopesAt(result, index) {
    return result.tokens.find(token => token.startIndex <= index && index < token.endIndex)?.scopes ?? [];
}

test('TextMate recognizes all supported escape forms', () => {
    const escapes = ['\\n', '\\r', '\\t', '\\\\', '\\"', '\\0', '\\x4F', '\\u{1f600}'];
    const source = 'print("' + escapes.join(' ') + '")';
    const result = grammar.tokenizeLine(source, textmate.INITIAL);
    let offset = 7;
    for (const escape of escapes) {
        const index = source.indexOf(escape, offset);
        assert.ok(index >= 0);
        assert.ok(scopesAt(result, index).includes('constant.character.escape.moulang'), escape);
        offset = index + escape.length;
    }
});

test('escaped quotes and backslashes preserve string boundaries', () => {
    const source = 'print("before\\"after\\\\") // done';
    const result = grammar.tokenizeLine(source, textmate.INITIAL);
    assert.ok(scopesAt(result, source.indexOf('after')).includes('string.quoted.double.moulang'));
    assert.ok(scopesAt(result, source.indexOf('//')).includes('comment.line.double-slash.moulang'));
    const unclosed = grammar.tokenizeLine('print("unclosed', textmate.INITIAL);
    const next = grammar.tokenizeLine('print(1)', unclosed.ruleStack);
    assert.ok(!scopesAt(next, 0).includes('string.quoted.double.moulang'));
});

test('TextMate marks structurally malformed escapes invalid', () => {
    for (const escape of ['\\q', '\\xG0', '\\u{}', '\\u{1234567}']) {
        const result = grammar.tokenizeLine('print("' + escape + '")', textmate.INITIAL);
        assert.ok(scopesAt(result, 7).includes('invalid.illegal.escape.moulang'), escape);
    }
});
