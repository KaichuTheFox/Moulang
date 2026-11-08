'use strict';

const fs = require('node:fs/promises');
const path = require('node:path');

async function resolveCompiler(configured, workingDirectory, searchPath = process.env.PATH ?? '') {
    if (typeof configured !== 'string' || !configured.trim() || configured.includes('\0')) return undefined;
    const explicit = path.isAbsolute(configured) || /[\\/]/.test(configured);
    const directories = explicit ? [workingDirectory] : searchPath.split(path.delimiter).filter(Boolean);
    for (const directory of directories) {
        const base = path.resolve(directory.replace(/^"|"$/g, ''), configured);
        const candidates = process.platform === 'win32' && !path.extname(base) ? [base + '.exe', base] : [base];
        for (const candidate of candidates) {
            try {
                if ((await fs.stat(candidate)).isFile()) return candidate;
            } catch (error) {
                if (!['ENOENT', 'ENOTDIR', 'EACCES', 'EPERM'].includes(error.code)) throw error;
            }
        }
    }
    return undefined;
}

module.exports = { resolveCompiler };
