'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { test } = require('node:test');
const root = path.resolve(__dirname, '../..');

test('project repository metadata and links use the final location', async () => {
    const manifest = JSON.parse(await fs.readFile(path.join(root, 'vscode-extension/package.json'), 'utf8'));
    assert.equal(manifest.publisher + '.' + manifest.name, 'KaichuTheFox.moulang');
    assert.equal(manifest.displayName, 'Moulang for VS Code');
    assert.equal(manifest.repository.url, 'https://github.com/KaichuTheFox/Moulang.git');
    assert.equal(manifest.homepage, 'https://github.com/KaichuTheFox/Moulang#readme');
    assert.equal(manifest.bugs.url, 'https://github.com/KaichuTheFox/Moulang/issues');
    const files = ['README.md', 'CHANGELOG.md', 'vscode-extension/README.md',
        ...(await fs.readdir(path.join(root, 'docs'))).filter(name => name.endsWith('.md')).map(name => 'docs/' + name)];
    let ownUrls = 0;
    let localLinks = 0;
    for (const file of files) {
        const text = await fs.readFile(path.join(root, file), 'utf8');
        for (const match of text.matchAll(/https:\/\/github\.com\/KaichuTheFox\/([A-Za-z0-9_.-]+)/g)) {
            ++ownUrls;
            const repository = match[1].replace(/\.$/, '');
            assert.ok(['Moulang', 'Moulang.git'].includes(repository), file + ': inconsistent project URL');
        }
        for (const match of text.matchAll(/\]\(([^)\s]+)\)/g)) {
            const target = match[1].split('#')[0];
            if (!target || /^[a-z][a-z0-9+.-]*:/i.test(target)) continue;
            const resolved = path.resolve(root, path.dirname(file), decodeURIComponent(target));
            assert.ok(!path.relative(root, resolved).startsWith('..'), file + ': link leaves repository');
            await fs.access(resolved);
            ++localLinks;
        }
    }
    assert.ok(ownUrls >= 3, 'Expected project URLs to be checked.');
    assert.ok(localLinks > 10, 'Expected relative documentation links to be checked.');
});
