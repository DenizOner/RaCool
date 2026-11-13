import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

export function checkTestLayout(root) {
    if (!fs.existsSync(path.join(root, 'src'))) throw Error('Missing source tree');
    const suites = JSON.parse(fs.readFileSync(path.join(root, 'test/target_suites.json'), 'utf8'));
    const entryDirectories = new Set(suites.map(suite => {
        const entry = suite.entry ?? suite.file;
        if (!/^test\/test_target_[^/]+\/test_main\.c$/.test(entry))
            throw Error('Invalid PlatformIO target entry: ' + entry);
        return entry.split('/')[1];
    }));
    const excluded = new Set(['build', '__pycache__']);
    const supportDirectories = new Set(['diagnostic', 'target', 'mocks']);
    let modules = 0;
    function walk(parts) {
        for (const entry of fs.readdirSync(path.join(root, 'test', ...parts), {withFileTypes: true})) {
            if (!entry.isDirectory() || excluded.has(entry.name)) continue;
            const next = [...parts, entry.name];
            if (next[0] === 'Infrastructure' || entryDirectories.has(next[0])) continue;
            if (next.length > 2 && supportDirectories.has(entry.name)) continue;
            if (!fs.existsSync(path.join(root, 'src', ...next)) ||
                !fs.statSync(path.join(root, 'src', ...next)).isDirectory())
                throw Error('Test directory has no source counterpart: test/' + next.join('/'));
            if (next.length === 2) modules++;
            walk(next);
        }
    }
    walk([]);
    return {modules, targetEntryDirectories: entryDirectories.size};
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
    console.log(JSON.stringify(checkTestLayout(path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..'))));
