import fs from 'node:fs';
import path from 'node:path';

export async function captureLogCursor(configurationPath) {
    const root = path.join(path.dirname(configurationPath), 'log');
    const directories = (await fs.promises.readdir(root, {withFileTypes:true}))
        .filter(entry => entry.isDirectory()).map(entry => entry.name).sort();
    if (!directories.length) throw Error('No bridge log directory');
    const file = path.join(root, directories.at(-1), 'log.log');
    return {file};
}

export function selectAttributeReports(text, device, endpoint, cluster) {
    return text.split(/\r?\n/).filter(line =>
        line.includes("Received Zigbee message from '" + device + "'") &&
        line.includes("type 'attributeReport'") &&
        line.includes("cluster '" + cluster + "'") &&
        line.includes("from endpoint " + endpoint + " "));
}

export function selectResponseWindow(content, startTransaction, endTransaction) {
    const start = content.lastIndexOf(startTransaction);
    const end = content.indexOf(endTransaction);
    if (start < 0 || end <= start) throw Error('Bridge log lacks fresh correlated observation markers');
    return content.slice(content.indexOf('\n', start) + 1, end);
}

export async function readNewLog(cursor, startTransaction, endTransaction) {
    const directory = path.dirname(cursor.file);
    // Winston rotates log.log to log1.log, then log2.log. Read oldest first;
    // timestamps and network filesystem sizes are not observation boundaries.
    const files = (await fs.promises.readdir(directory))
        .map(name => ({name, match: /^log(\d*)\.log$/.exec(name)}))
        .filter(entry => entry.match)
        .sort((a, b) => Number(b.match[1] || 0) - Number(a.match[1] || 0));
    const chunks = [];
    for (const entry of files) chunks.push(await fs.promises.readFile(path.join(directory, entry.name), 'utf8'));
    return selectResponseWindow(chunks.join('\n'), startTransaction, endTransaction);
}
