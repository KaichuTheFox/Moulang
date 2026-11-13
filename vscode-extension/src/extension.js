'use strict';

const path = require('node:path');
const vscode = require('vscode');
const { registerLiveDiagnostics } = require('./diagnostics');
const { resolveCompiler } = require('./compiler');
const builtins = ['print', 'type', 'input', 'format', 'append', 'len', 'readText', 'writeText', 'RText', 'WText', 'AText'];
const listMethods = ['append', 'insert', 'remove', 'sort', 'pop'];
const standardExports = {
    math: ['abs', 'min', 'max', 'sqrt', 'pow'],
    text: ['len', 'find', 'substring', 'replace', 'split', 'join', 'lowerAscii', 'upperAscii'],
    time: ['unixMillis', 'monotonicNanos', 'formatUtcDate'],
    random: ['seed', 'nextInt', 'nextDouble']
};

function memberCompletions(document, before) {
    const receiver = before.match(/(?<![.\w\p{Script=Hangul}])([A-Za-z_\p{Script=Hangul}][A-Za-z_0-9\p{Script=Hangul}]*)[ \t]*\.$/u);
    if (receiver) {
        const imports = /^[ \t]*import[ \t]+"std:(math|text|time|random)"(?:[ \t]+as[ \t]+([A-Za-z_\p{Script=Hangul}][A-Za-z_0-9\p{Script=Hangul}]*))?[ \t]*(?=\/\/|\r?$)/gmu;
        for (const [, module, alias] of document.getText().matchAll(imports)) {
            if ((alias ?? module) === receiver[1]) return standardExports[module];
        }
    }
    return listMethods;
}

async function offerCompilerSettings(message) {
    const action = await vscode.window.showErrorMessage(message, 'Open Settings');
    if (action === 'Open Settings') {
        await vscode.commands.executeCommand('workbench.action.openSettings', 'moulang.executablePath');
    }
}

function activate(context) {
    registerLiveDiagnostics(context);
    const selector = [{ language: 'moulang', scheme: 'file' }, { language: 'moulang', scheme: 'untitled' }];
    context.subscriptions.push(
        vscode.languages.registerCompletionItemProvider(selector, {
            provideCompletionItems(document, position) {
                const range = document.getWordRangeAtPosition(position, /[A-Za-z_\p{Script=Hangul}][A-Za-z_0-9\p{Script=Hangul}]*/u);
                const prefix = range ? document.getText(new vscode.Range(range.start, position)) : '';
                const before = document.lineAt(position.line).text.slice(0, (range?.start ?? position).character);
                const member = before.endsWith('.');
                const candidates = member ? memberCompletions(document, before) : [...builtins, 'void', 'char', 'short', 'int', 'long', 'float', 'double',
                    'bool', 'str', 'var', 'struct', 'class', 'new', 'this', 'public', 'private', 'def', 'return',
                    'if', 'elif', 'else', 'while', 'for', 'foreach', 'in', 'break', 'continue', 'import',
                    'true', 'false', 'null'];
                return candidates
                    .filter(word => word.startsWith(prefix))
                    .map(word => {
                        const builtin = member || builtins.includes(word);
                        const kind = builtin ? vscode.CompletionItemKind.Function : vscode.CompletionItemKind.Keyword;
                        const item = new vscode.CompletionItem(word, kind);
                        item.range = range ?? new vscode.Range(position, position);
                        item.insertText = builtin ? new vscode.SnippetString(`${word}($0)`) : word;
                        return item;
                    });
            }
        }, '.'),
        vscode.commands.registerCommand('moulang.run', async () => {
            const document = vscode.window.activeTextEditor?.document;
            if (document?.languageId !== 'moulang' || document.uri.scheme !== 'file') {
                vscode.window.showErrorMessage('Open a saved Moulang source file to run it.');
                return;
            }
            const folder = vscode.workspace.getWorkspaceFolder(document.uri);
            if (!folder) {
                vscode.window.showErrorMessage('Open the folder containing this Moulang file before running it.');
                return;
            }
            if (document.isDirty && !(await document.save())) {
                vscode.window.showErrorMessage('Save the Moulang file before running it.');
                return;
            }

            const configured = vscode.workspace.getConfiguration('moulang', document.uri).get('executablePath');
            const executable = await resolveCompiler(configured, path.dirname(document.uri.fsPath));
            if (!executable) {
                await offerCompilerSettings('Moulang compiler not found. Set moulang.executablePath to the full path to moulang.exe.');
                return;
            }
            const execution = new vscode.ProcessExecution(executable, ['run', document.uri.fsPath], {
                cwd: path.dirname(document.uri.fsPath)
            });
            const task = new vscode.Task({ type: 'moulang' }, folder, 'Run current file', 'Moulang', execution, ['$moulang']);
            task.presentationOptions = { reveal: vscode.TaskRevealKind.Always };
            try {
                return await vscode.tasks.executeTask(task);
            } catch (error) {
                await offerCompilerSettings('Could not start the Moulang compiler. Check its path and permissions.');
            }
        })
    );
}

module.exports = { activate };
