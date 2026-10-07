// Dev-only: regenerates textdiff-golden.json by running Equicord's
// MessageLogger diffUtils.ts (types removed) under Node:
//   node generate-diff-golden.mjs
import { writeFileSync } from "fs";

function tokenizeMessage(text) {
    const tokens = [];
    let i = 0;
    while (i < text.length) {
        if (text[i] === "<" && (text.slice(i + 1, i + 3) === ":" || text.slice(i + 1, i + 3) === "a:")) {
            const endIndex = text.indexOf(">", i);
            if (endIndex !== -1) { tokens.push(text.slice(i, endIndex + 1)); i = endIndex + 1; continue; }
        }
        if (text[i] === "<" && text[i + 1] === "@" || (text[i] === "<" && text[i + 1] === "#")) {
            const endIndex = text.indexOf(">", i);
            if (endIndex !== -1) { tokens.push(text.slice(i, endIndex + 1)); i = endIndex + 1; continue; }
        }
        const char = Array.from(text.slice(i))[0];
        tokens.push(char);
        i += char.length;
    }
    return tokens;
}

function createWordDiff(oldText, newText) {
    if (oldText.length < newText.length && newText.startsWith(oldText)) {
        const parts = [];
        if (oldText.length > 0) parts.push({ type: "unchanged", text: oldText });
        parts.push({ type: "added", text: newText.slice(oldText.length) });
        return parts;
    }
    if (oldText.length < newText.length && newText.endsWith(oldText)) {
        const parts = [];
        parts.push({ type: "added", text: newText.slice(0, newText.length - oldText.length) });
        if (oldText.length > 0) parts.push({ type: "unchanged", text: oldText });
        return parts;
    }
    if (newText.length < oldText.length && oldText.startsWith(newText)) {
        const parts = [];
        if (newText.length > 0) parts.push({ type: "unchanged", text: newText });
        parts.push({ type: "removed", text: oldText.slice(newText.length) });
        return parts;
    }
    if (newText.length < oldText.length && oldText.endsWith(newText)) {
        const parts = [];
        parts.push({ type: "removed", text: oldText.slice(0, oldText.length - newText.length) });
        if (newText.length > 0) parts.push({ type: "unchanged", text: newText });
        return parts;
    }
    const oldChars = tokenizeMessage(oldText);
    const newChars = tokenizeMessage(newText);
    const dp = [];
    for (let i = 0; i <= oldChars.length; i++) { dp[i] = []; for (let j = 0; j <= newChars.length; j++) dp[i][j] = 0; }
    for (let i = 1; i <= oldChars.length; i++)
        for (let j = 1; j <= newChars.length; j++)
            dp[i][j] = oldChars[i - 1] === newChars[j - 1] ? dp[i - 1][j - 1] + 1 : Math.max(dp[i - 1][j], dp[i][j - 1]);
    let i = oldChars.length, j = newChars.length;
    const diffParts = [];
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && oldChars[i - 1] === newChars[j - 1]) { diffParts.unshift({ type: "unchanged", text: oldChars[i - 1] }); i--; j--; }
        else if (j > 0 && (i === 0 || dp[i][j - 1] > dp[i - 1][j])) { diffParts.unshift({ type: "added", text: newChars[j - 1] }); j--; }
        else if (i > 0) { diffParts.unshift({ type: "removed", text: oldChars[i - 1] }); i--; }
    }
    const grouped = [];
    for (const part of diffParts) {
        const last = grouped[grouped.length - 1];
        if (last && last.type === part.type) last.text += part.text; else grouped.push(part);
    }
    return grouped;
}

const cases = [
    ["hello", "hello world"], ["world", "hello world"], ["hello world", "hello"], ["hello world", "world"],
    ["the quick brown fox", "the slow brown dog"], ["abc", "xyz"], ["", "new"], ["old", ""], ["same", "same"],
    ["hi <@123> there", "hi <@456> there"], ["emoji <a:wave:111> ok", "emoji <a:wave:222> ok"],
    ["static <:smile:111> ok", "static <:smile:222> ok"], ["ping <#999>", "pong <#999>"],
    ["a😀b", "a😃b"], ["line one\nline two", "line one\nline 2"], ["teh typo", "the typo"],
];
writeFileSync(new URL("./textdiff-golden.json", import.meta.url),
    JSON.stringify(cases.map(([o, n]) => ({ old: o, new: n, parts: createWordDiff(o, n) })), null, 1) + "\n");
