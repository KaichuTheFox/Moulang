'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { downloadAndUnzipVSCode, resolveCliPathFromVSCodeExecutablePath, runTests } = require('../../vscode-extension/node_modules/@vscode/test-electron');

const root = path.resolve(__dirname, '../..');
const extensionSource = path.join(root, 'vscode-extension');
const version = '1.144.0';

function execute(executable, args, env = process.env) {
    const result = spawnSync(executable, args, { cwd: extensionSource, env, encoding: 'utf8', windowsHide: true });
    if (result.stdout) process.stdout.write(result.stdout);
    if (result.stderr) process.stderr.write(result.stderr);
    if (result.error) throw result.error;
    assert.equal(result.status, 0, 'Command failed: ' + path.basename(executable));
}

async function main() {
    assert.equal(process.platform, 'win32', 'The current compiler and integration tests require Windows.');
    const compiler = path.resolve(process.env.MOULANG_TEST_COMPILER ?? path.join(root, 'build/windows-release/bin/moulang.exe'));
    assert.ok((await fs.stat(compiler)).isFile(), 'Set MOULANG_TEST_COMPILER to the compiler executable.');
    const cachePath = path.join(root, 'build/vscode-download');
    const executable = await downloadAndUnzipVSCode({ version, cachePath, platform: 'win32-x64-archive', timeout: 60000 });
    assert.ok(path.relative(cachePath, executable).split(path.sep)[0] !== '..', 'Use the downloaded Code, never the installed Code.');
    const cliShim = resolveCliPathFromVSCodeExecutablePath(executable);
    const cliMatch = (await fs.readFile(cliShim, 'utf8')).match(/"%~dp0([^"\r\n]*[\\/]resources[\\/]app[\\/]out[\\/]cli\.js)"/i);
    assert.ok(cliMatch, 'Find cli.js from the archive launcher, including versioned resource layouts.');
    const cliScript = path.resolve(path.dirname(cliShim), cliMatch[1]);
    assert.ok((await fs.stat(cliScript)).isFile());
    const runRoot = path.join(root, 'build/vscode-runs');
    await fs.mkdir(runRoot, { recursive: true });
    const run = await fs.mkdtemp(path.join(runRoot, 'run-'));
    const manifest = JSON.parse(await fs.readFile(path.join(extensionSource, 'package.json'), 'utf8'));
    const vsix = path.join(run, 'moulang-' + manifest.version + '.vsix');
    execute(process.execPath, [path.join(extensionSource, 'node_modules/@vscode/vsce/vsce'),
        'package', '--no-dependencies', '--out', vsix]);
    const temp = path.join(run, 'temp');
    await fs.mkdir(temp);
    const env = { ...process.env, MOULANG_TEST_COMPILER: compiler, TEMP: temp, TMP: temp };
    // Avoid inheriting an unrelated portable profile or Electron CLI mode from the host.
    delete env.VSCODE_PORTABLE;
    delete env.ELECTRON_RUN_AS_NODE;
    for (const mode of ['source', 'vsix']) {
        const profile = path.join(run, mode);
        const workspace = path.join(profile, 'workspace');
        const data = path.join(profile, 'user-data');
        const extensions = path.join(profile, 'extensions');
        await Promise.all([workspace, data, extensions].map(directory => fs.mkdir(directory, { recursive: true })));
        await fs.mkdir(path.join(data, 'User'));
        await fs.writeFile(path.join(data, 'User/settings.json'), JSON.stringify({
            'chat.disableAIFeatures': true, 'workbench.startupEditor': 'none', 'update.mode': 'none'
        }));
        const profileArgs = ['--user-data-dir=' + data, '--extensions-dir=' + extensions];
        let extensionDevelopmentPath = extensionSource;
        if (mode === 'vsix') {
            // Invoke the archive's cli.js directly to preserve spaces and Unicode without cmd quoting.
            execute(executable, [cliScript,
                ...profileArgs, '--install-extension', vsix], { ...env, ELECTRON_RUN_AS_NODE: '1' });
            const entries = await fs.readdir(extensions);
            const installed = entries.find(entry => entry.toLowerCase() ===
                (manifest.publisher + '.' + manifest.name + '-' + manifest.version).toLowerCase());
            assert.ok(installed, 'Expected the VSIX to install in the isolated extensions directory.');
            extensionDevelopmentPath = path.join(extensions, installed);
        }
        console.log('Testing ' + mode + ' extension with downloaded VS Code ' + version);
        const result = await runTests({
            vscodeExecutablePath: executable,
            extensionDevelopmentPath,
            extensionTestsPath: path.join(__dirname, 'suite.js'),
            extensionTestsEnv: env,
            launchArgs: [workspace, ...profileArgs, '--disable-gpu', '--new-window', '--disable-extension=GitHub.copilot-chat']
        });
        assert.equal(result, 0);
    }
    console.log('PASS: isolated downloaded VS Code source and packaged extension suites.');
}

main().catch(error => { console.error(error); process.exitCode = 1; });
