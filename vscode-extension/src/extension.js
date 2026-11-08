'use strict';

const path = require('node:path');
const vscode = require('vscode');
const { registerLiveDiagnostics } = require('./diagnostics');
const { resolveCompiler } = require('./compiler');

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
                const range = document.getWordRangeAtPosition(position, /[A-Za-z_]\w*/);
                const prefix = range ? document.getText(new vscode.Range(range.start, position)) : '';
                return ['print', 'append', 'readText', 'writeText', 'void', 'char', 'short', 'int', 'long', 'float', 'double',
                    'bool', 'string', 'var', 'struct', 'class', 'new', 'this', 'public', 'private', 'def', 'return',
                    'if', 'elif', 'else', 'while', 'for', 'foreach', 'in', 'break', 'continue', 'import',
                    'true', 'false', 'null']
                    .filter(word => word.startsWith(prefix))
                    .map(word => {
                        const builtin = ['print', 'append', 'readText', 'writeText'].includes(word);
                        const kind = builtin ? vscode.CompletionItemKind.Function : vscode.CompletionItemKind.Keyword;
                        const item = new vscode.CompletionItem(word, kind);
                        item.range = range ?? new vscode.Range(position, position);
                        item.insertText = builtin ? new vscode.SnippetString(`${word}($0)`) : word;
                        return item;
                    });
            }
        }),
        vscode.commands.registerCommand('moulang.run', async () => {
            const document = vscode.window.activeTextEditor?.document;
            if (document?.languageId !== 'moulang' || document.uri.scheme !== 'file') {
                vscode.window.showErrorMessage('Open a saved Moulang (.m) file to run it.');
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
