// Use only configure/compiler/linker/upload output, never runtime fault injection.
export function findBuildDiagnostics(text) {
    return text.split(/\r?\n/).filter(line=>/\b(?:warning|error)\s*:|CMake Warning|\bWarning[!:]|\b[A-Za-z]+Warning:|^\s*\[(?:Warn|Error)\]/i.test(line));
}
