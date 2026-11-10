'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const vscode = require('vscode');

async function runCurrentFile() {
    let listener;
    let started;
    let timeout;
    const source = vscode.window.activeTextEditor.document.uri.fsPath;
    const compiler = process.env.MOULANG_TEST_COMPILER;
    let execution;
    try {
        started = vscode.tasks.onDidStartTask(event => {
            if (event.execution.task.definition.type === 'moulang') execution = event.execution.task.execution;
        });
        const completed = new Promise((resolve, reject) => {
            listener = vscode.tasks.onDidEndTaskProcess(event => {
                if (event.execution.task.definition.type === 'moulang') resolve(event.exitCode);
            });
            timeout = setTimeout(() => reject(new Error('Moulang task did not finish.')), 15000);
        });
        await vscode.commands.executeCommand('moulang.run');
        const exitCode = await completed;
        assert.ok(execution instanceof vscode.ProcessExecution, 'Expected a process task.');
        assert.equal(execution.process.toLowerCase(), compiler.toLowerCase());
        assert.deepEqual(execution.args, ['run', source]);
        assert.equal(execution.options.cwd, path.dirname(source));
        return exitCode;
    } finally {
        listener?.dispose();
        started?.dispose();
        clearTimeout(timeout);
    }
}

async function waitFor(test) {
    for (let attempt = 0; attempt < 60 && !test(); ++attempt) {
        await new Promise(resolve => setTimeout(resolve, 100));
    }
    return test();
}

async function run() {
    const compiler = process.env.MOULANG_TEST_COMPILER;
    assert.ok(compiler, 'Set MOULANG_TEST_COMPILER to the full path of the compiler.');
    const workspace = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    assert.ok(workspace, 'Open a folder in the Extension Development Host.');
    const directory = await fs.mkdtemp(path.join(workspace, 'vscode test '));
    const file = path.join(directory, '\uBA38\uB7AD test.m');
    try {
        await fs.writeFile(file, 'p');
        const document = await vscode.workspace.openTextDocument(file);
        assert.equal(document.languageId, 'moulang');
        let editor = await vscode.window.showTextDocument(document);
        const extension = vscode.extensions.getExtension('KaichuTheFox.moulang');
        assert.ok(extension, 'Expected the Moulang extension ID.');
        assert.equal(extension.packageJSON.displayName, 'Moulang for VS Code');
        assert.equal(extension.packageJSON.repository.url, 'https://github.com/KaichuTheFox/Moulang.git');
        assert.equal(extension.packageJSON.homepage, 'https://github.com/KaichuTheFox/Moulang#readme');
        assert.equal(extension.packageJSON.bugs.url, 'https://github.com/KaichuTheFox/Moulang/issues');
        const runCommand = extension.packageJSON.contributes.commands.find(item => item.command === 'moulang.run');
        const runButton = extension.packageJSON.contributes.menus['editor/title']
            .find(item => item.command === 'moulang.run');
        assert.equal(runCommand.icon, '$(play)');
        assert.equal(runButton.group, 'navigation');
        assert.equal(runButton.when, 'resourceLangId == moulang && resourceScheme == file');
        await extension.activate();
        assert.ok(extension.isActive);
        assert.ok((await vscode.commands.getCommands(true)).includes('moulang.run'));
        assert.equal(vscode.workspace.getConfiguration('moulang', document.uri)
            .inspect('executablePath').defaultValue, 'moulang');
        await vscode.workspace.getConfiguration('moulang').update(
            'liveDiagnostics', false, vscode.ConfigurationTarget.Global
        );
        const cursor = new vscode.Position(0, 1);
        editor.selection = new vscode.Selection(cursor, cursor);
        const results = await vscode.commands.executeCommand(
            'vscode.executeCompletionItemProvider', document.uri, cursor
        );
        const print = results.items.find(item => item.label === 'print');
        assert.ok(print, `Expected print after typing p. Found ${JSON.stringify(results.items.map(item => item.label))}.`);
        assert.ok(await editor.insertSnippet(print.insertText, print.range.replacing ?? print.range));
        assert.equal(document.getText(), 'print()');
        assert.equal(editor.selection.active.character, 6);

        await editor.edit(edit => edit.replace(new vscode.Range(0, 0, 0, 7), 'app'));
        const appendResults = await vscode.commands.executeCommand(
            'vscode.executeCompletionItemProvider', document.uri, new vscode.Position(0, 3)
        );
        assert.ok(appendResults.items.some(item => item.label === 'append'));
        await editor.edit(edit => edit.replace(new vscode.Range(0, 0, 0, 3), 'fl'));
        const numericResults = await vscode.commands.executeCommand(
            'vscode.executeCompletionItemProvider', document.uri, new vscode.Position(0, 2)
        );
        assert.ok(numericResults.items.some(item => item.label === 'float'));
        await editor.edit(edit => edit.replace(new vscode.Range(0, 0, 0, 2), 'stru'));
        const structureResults = await vscode.commands.executeCommand(
            'vscode.executeCompletionItemProvider', document.uri, new vscode.Position(0, 4)
        );
        assert.ok(structureResults.items.some(item => item.label === 'struct'));
        await editor.edit(edit => edit.replace(new vscode.Range(0, 0, 0, 4), 'str'));
        const textResults = await vscode.commands.executeCommand(
            'vscode.executeCompletionItemProvider', document.uri, new vscode.Position(0, 3)
        );
        assert.ok(textResults.items.some(item => item.label === 'str'));
        assert.ok(!textResults.items.some(item => item.label === 'string'));
        await editor.edit(edit => edit.replace(new vscode.Range(0, 0, 0, 3), 'list.po'));
        const methodResults = await vscode.commands.executeCommand(
            'vscode.executeCompletionItemProvider', document.uri, new vscode.Position(0, 7)
        );
        assert.ok(methodResults.items.some(item => item.label === 'pop'));
        await editor.edit(edit => edit.replace(
            new vscode.Range(0, 0, 0, 7),
            'struct Point { int x }\nmain() { Point? p = Point(10)\nprint(p?.x ?? 0) }\n'
        ));
        await vscode.workspace.getConfiguration('moulang').update(
            'executablePath', compiler, vscode.ConfigurationTarget.Global
        );
        assert.equal(await runCurrentFile(), 0);
        assert.match(await fs.readFile(file, 'utf8'), /^main\(\)/m);
        const second = path.join(directory, 'second.m');
        await fs.writeFile(second,
            'main() {\n    str[] names = ["second"]\n    names.append("active") { print(names) }\n' +
            '    print("type: {}") { format(type(names)) }\n}\n');
        await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(second));
        assert.equal(await runCurrentFile(), 0);
        editor = await vscode.window.showTextDocument(document);
        const end = document.positionAt(document.getText().length);
        await editor.edit(edit => edit.replace(new vscode.Range(new vscode.Position(0, 0), end),
            'main() { print(undefinedName) }\n'));
        assert.equal(await runCurrentFile(), 1);
        const findDiagnostic = source => vscode.languages.getDiagnostics(document.uri)
            .find(diagnostic => diagnostic.code === 'ML3004' && diagnostic.source === source);
        const diagnostic = await waitFor(() => findDiagnostic('Moulang'));
        const reported = vscode.languages.getDiagnostics().flatMap(([uri, diagnostics]) =>
            diagnostics.filter(item => item.source === 'Moulang').map(item => [uri.fsPath, item.code, item.message]));
        assert.ok(diagnostic, `Expected a compiler error on the Unicode source file. Found ${JSON.stringify(reported)}.`);
        assert.equal(diagnostic.range.start.line, 0);
        assert.match(diagnostic.message, /Undeclared variable/);
        await vscode.workspace.getConfiguration('moulang').update(
            'liveDiagnostics', true, vscode.ConfigurationTarget.Global
        );
        const replaceSource = text => editor.edit(edit => edit.replace(
            new vscode.Range(new vscode.Position(0, 0), document.positionAt(document.getText().length)), text));
        await replaceSource('main() { print(missingLive) }\n');
        const live = await waitFor(() => {
            const result = findDiagnostic('Moulang Check');
            return result?.message.includes('missingLive') ? result : undefined;
        });
        assert.ok(live, 'Expected live diagnostics from unsaved source.');
        assert.match(await fs.readFile(file, 'utf8'), /undefinedName/);
        await replaceSource('main() { print("fixed") }\n');
        assert.ok(await waitFor(() => !findDiagnostic('Moulang Check')));
        const badEscape = 'main() { print("' + String.fromCodePoint(0x1F600) + '\\q") }\n';
        await replaceSource(badEscape);
        const escapeError = await waitFor(() => vscode.languages.getDiagnostics(document.uri)
            .find(item => item.source === 'Moulang Check' && item.code === 'ML1004'));
        assert.ok(escapeError, 'Expected an unsaved string escape diagnostic.');
        assert.equal(escapeError.range.start.line, 0);
        assert.equal(escapeError.range.start.character, badEscape.indexOf('\\'));
        await replaceSource('main() { print("A\\0B" == "A\\x00B") }\n');
        assert.ok(await waitFor(() => !vscode.languages.getDiagnostics(document.uri)
            .some(item => item.source === 'Moulang Check')));
        assert.equal(await runCurrentFile(), 0);
        console.log('PASS: Moulang activation, menus, current-file arguments, completion, execution, compiler/escape diagnostics and unsaved edits.');
    } finally {
        // Task terminals may retain this working directory until Code exits.
        // Preserve fixtures for diagnosis rather than masking a test failure with EBUSY.
        console.log('Test fixtures retained in ' + directory);
    }
}

module.exports = { run };
