import { spawn } from 'node:child_process';
import os from 'node:os';
import path from 'node:path';

const workspaceFolder = process.env.IDF_MCP_WORKSPACE_FOLDER ?? process.cwd();
const idfVersion = process.env.IDF_MCP_IDF_VERSION ?? 'v6.0';
const eimExecutable = path.join(
    process.env.USERPROFILE ?? os.homedir(),
    '.espressif',
    'tools',
    process.platform === 'win32' ? 'eim.exe' : 'eim',
);

const eim = spawn(eimExecutable, ['run', 'idf.py mcp-server', idfVersion], {
    cwd: workspaceFolder,
    env: {
        ...process.env,
        IDF_MCP_WORKSPACE_FOLDER: workspaceFolder,
    },
    stdio: ['pipe', 'pipe', 'pipe'],
});

let stdoutBuffer = '';

function routeLine(line) {
    const trimmedLine = line.trim();
    if (!trimmedLine) {
        return;
    }

    try {
        const message = JSON.parse(trimmedLine);
        if (message?.jsonrpc === '2.0') {
            process.stdout.write(`${trimmedLine}\n`);
            return;
        }
    } catch {
    }

    process.stderr.write(`${line}\n`);
}

eim.stdout.setEncoding('utf8');
eim.stdout.on('data', (chunk) => {
    stdoutBuffer += chunk;
    let newlineIndex = stdoutBuffer.indexOf('\n');
    while (newlineIndex !== -1) {
        routeLine(stdoutBuffer.slice(0, newlineIndex).replace(/\r$/, ''));
        stdoutBuffer = stdoutBuffer.slice(newlineIndex + 1);
        newlineIndex = stdoutBuffer.indexOf('\n');
    }
});

eim.stdout.on('end', () => {
    if (stdoutBuffer) {
        routeLine(stdoutBuffer);
        stdoutBuffer = '';
    }
});

eim.stderr.pipe(process.stderr);
process.stdin.pipe(eim.stdin);

eim.on('error', (error) => {
    process.stderr.write(`Failed to start ESP-IDF MCP server: ${error.message}\n`);
    process.exitCode = 1;
});

eim.on('close', (code) => {
    process.exitCode = code ?? 1;
});