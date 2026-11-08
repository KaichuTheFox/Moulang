'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { test } = require('node:test');
const vm = require('node:vm');
const { resolveCompiler } = require('../../vscode-extension/src/compiler');

const root = path.resolve(__dirname, '../../build/vscode-unit');

test('compiler lookup supports absolute, relative, and PATH names with spaces', async () => {
    await fs.mkdir(root, { recursive: true });
    const directory = await fs.mkdtemp(path.join(root, 'compiler '));
    const binary = path.join(directory, process.platform === 'win32' ? 'moulang.exe' : 'moulang');
    await fs.writeFile(binary, 'lookup fixture');
    assert.equal(await resolveCompiler(binary, root, ''), binary);
    assert.equal(await resolveCompiler(path.relative(root, binary), root, ''), binary);
    assert.equal(await resolveCompiler('moulang', root, '"' + directory + '"'), binary);
    assert.equal(await resolveCompiler('not-installed', root, directory), undefined);
    assert.equal(await resolveCompiler(directory, root, ''), undefined);
    assert.equal(await resolveCompiler('', root, directory), undefined);
    assert.equal(await resolveCompiler('bad\0name', root, directory), undefined);
});

async function missingCompilerHost(selection) {
    const callbacks = new Map();
    const notices = [];
    const commands = [];
    let taskCount = 0;
    const vscode = {
        window: {
            activeTextEditor: { document: {
                languageId: 'moulang', isDirty: false,
                uri: { scheme: 'file', fsPath: path.join(root, 'main.m') }
            } },
            showErrorMessage: async (...args) => { notices.push(args); return selection; }
        },
        workspace: {
            getWorkspaceFolder: () => ({}),
            getConfiguration: () => ({ get: () => path.join(root, 'compiler-does-not-exist.exe') })
        },
        languages: { registerCompletionItemProvider: () => ({ dispose() {} }) },
        commands: {
            registerCommand: (name, callback) => { callbacks.set(name, callback); return { dispose() {} }; },
            executeCommand: async (...args) => { commands.push(args); }
        },
        tasks: { executeTask: async () => { ++taskCount; } }
    };
    const filename = path.resolve(__dirname, '../../vscode-extension/src/extension.js');
    const module = { exports: {} };
    vm.runInNewContext(await fs.readFile(filename, 'utf8'), {
        module, require: name => {
            if (name === 'vscode') return vscode;
            if (name === './diagnostics') return { registerLiveDiagnostics() {} };
            if (name === './compiler') return { resolveCompiler };
            if (name === 'node:path') return path;
            throw new Error('Unexpected dependency: ' + name);
        }
    }, { filename });
    module.exports.activate({ subscriptions: [] });
    await callbacks.get('moulang.run')();
    return { notices, commands, taskCount };
}

test('missing compiler offers settings and opens the exact setting', async () => {
    const result = await missingCompilerHost('Open Settings');
    assert.equal(result.notices.length, 1);
    assert.match(result.notices[0][0], /Moulang compiler not found/);
    assert.equal(result.notices[0][1], 'Open Settings');
    assert.deepEqual(result.commands, [['workbench.action.openSettings', 'moulang.executablePath']]);
    assert.equal(result.taskCount, 0);
});

test('dismissing the missing compiler notice starts no task and opens no settings', async () => {
    const result = await missingCompilerHost(undefined);
    assert.equal(result.notices.length, 1);
    assert.equal(result.commands.length, 0);
    assert.equal(result.taskCount, 0);
});
