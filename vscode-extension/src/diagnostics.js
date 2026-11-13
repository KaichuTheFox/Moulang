'use strict';

const { execFile } = require('node:child_process');
const path = require('node:path');
const vscode = require('vscode');

function fileKey(file) {
    const normalized = path.resolve(file);
    return process.platform === 'win32' ? normalized.toLowerCase() : normalized;
}

function diagnosticsFor(document, stderr) {
    const key = fileKey(document.uri.fsPath);
    const results = [];
    for (const line of stderr.split(/\r?\n/)) {
        const match = /^(.*):(\d+):(\d+): (error ML\d+|warning TI-[BDP]\d+): (.*)$/.exec(line);
        if (!match || fileKey(match[1]) !== key) continue;
        const lineNumber = Math.max(0, Math.min(document.lineCount - 1, Number(match[2]) - 1));
        const lineText = document.lineAt(lineNumber).text;
        const characters = [...lineText];
        const column = Math.max(0, Number(match[3]) - 1);
        const start = characters.slice(0, column).join('').length;
        const end = start + (characters[column]?.length ?? 0);
        const range = new vscode.Range(lineNumber, start, lineNumber, end);
        const [severity, code] = match[4].split(' ');
        const warning = severity === 'warning';
        const diagnostic = new vscode.Diagnostic(range, match[5],
            warning ? vscode.DiagnosticSeverity.Warning : vscode.DiagnosticSeverity.Error);
        diagnostic.code = code;
        diagnostic.source = warning ? 'Tiki Inspector' : 'Moulang Check';
        results.push(diagnostic);
    }
    return results;
}

function registerLiveDiagnostics(context) {
    const collection = vscode.languages.createDiagnosticCollection('moulang-live');
    const pending = new Map();

    function cancel(key) {
        const job = pending.get(key);
        if (!job) return;
        pending.delete(key);
        clearTimeout(job.timer);
        job.child?.kill();
    }

    function schedule(document) {
        if (document.languageId !== 'moulang' || document.uri.scheme !== 'file') return;
        const key = document.uri.toString();
        cancel(key);
        const config = vscode.workspace.getConfiguration('moulang', document.uri);
        const executable = config.get('executablePath');
        if (!vscode.workspace.isTrusted || !vscode.workspace.getWorkspaceFolder(document.uri) ||
            !config.get('liveDiagnostics') || !executable || executable.includes('\0')) {
            collection.delete(document.uri);
            return;
        }

        const file = document.uri.fsPath;
        const version = document.version;
        const args = ['check', file, '--stdin', '--library'];
        const tikiMode = config.get('tikiMode');
        if (['basic', 'perf', 'deep', 'all'].includes(tikiMode)) args.push('--tiki=' + tikiMode);
        const job = {};
        job.timer = setTimeout(() => {
            job.child = execFile(executable, args, {
                cwd: path.dirname(file), encoding: 'utf8', windowsHide: true, timeout: 10000
            }, (_, __, stderr) => {
                if (pending.get(key) !== job) return;
                pending.delete(key);
                if (document.version === version && !document.isClosed) {
                    collection.set(document.uri, diagnosticsFor(document, stderr));
                }
            });
            job.child.stdin.on('error', () => {});
            job.child.stdin.end(document.getText(), 'utf8');
        }, 300);
        pending.set(key, job);
    }

    context.subscriptions.push(collection,
        vscode.workspace.onDidOpenTextDocument(schedule),
        vscode.workspace.onDidChangeTextDocument(event => {
            if (event.contentChanges.length) schedule(event.document);
        }),
        vscode.workspace.onDidCloseTextDocument(document => {
            cancel(document.uri.toString());
            collection.delete(document.uri);
        }),
        vscode.workspace.onDidChangeConfiguration(event => {
            if (event.affectsConfiguration('moulang')) vscode.workspace.textDocuments.forEach(schedule);
        }),
        { dispose: () => pending.forEach((_, key) => cancel(key)) }
    );
    vscode.workspace.textDocuments.forEach(schedule);
}

module.exports = { registerLiveDiagnostics };
