// Dev-only: regenerates textreplace-golden.json and unindent-golden.json by
// running Equicord's TextReplace applyRules/stringToRegex and Unindent
// unindent/unindentMsg verbatim (types removed) under Node:
//   node generate-text-golden.mjs
import { writeFileSync } from "fs";

// ---- TextReplace (src/plugins/textReplace/index.tsx) ----
function stringToRegex(str) {
    const match = str.match(/^(\/)?(.+?)(?:\/([gimsuyv]*))?$/);
    return match
        ? new RegExp(
            match[2],
            match[3]
                ?.split("")
                .filter((char, pos, flagArr) => flagArr.indexOf(char) === pos)
                .join("")
            ?? "g"
        )
        : new RegExp(str);
}
function applyRules(content, scope, store) {
    if (content.length === 0) return content;
    for (const rule of store.stringRules) {
        if (!rule.find) continue;
        if (rule.onlyIfIncludes && !content.includes(rule.onlyIfIncludes)) continue;
        if (rule.scope !== "allMessages" && rule.scope !== scope && scope !== "allMessages") continue;
        content = ` ${content} `.replaceAll(rule.find, rule.replace.replaceAll("\\n", "\n")).replace(/^\s|\s$/g, "");
    }
    for (const rule of store.regexRules) {
        if (!rule.find) continue;
        if (rule.onlyIfIncludes && !content.includes(rule.onlyIfIncludes)) continue;
        if (rule.scope !== "allMessages" && rule.scope !== scope && scope !== "allMessages") continue;
        try {
            const regex = stringToRegex(rule.find);
            content = content.replace(regex, rule.replace.replaceAll("\\n", "\n"));
        } catch (e) {
            // upstream logs "Invalid regex"
        }
    }
    content = content.trim();
    return content;
}

const r = (find, replace, extra = {}) => ({ name: "", find, replace, onlyIfIncludes: "", scope: "myMessages", id: "x", ...extra });
const trCases = [
    { name: "simple", s: [r("brb", "be right back")], x: [], input: "brb, brb!", scope: "myMessages" },
    { name: "newline escape", s: [r("nl", "a\\nb")], x: [], input: "x nl y", scope: "myMessages" },
    { name: "dollar in string rule", s: [r("cat", "[$&|$$]")], x: [], input: "a cat", scope: "myMessages" },
    { name: "padding and edge whitespace", s: [r("hi", "hello")], x: [], input: "hi", scope: "myMessages" },
    { name: "find with spaces", s: [r(" x ", "-")], x: [], input: "x y x", scope: "myMessages" },
    { name: "only if includes", s: [r("a", "b", { onlyIfIncludes: "zzz" }), r("c", "d", { onlyIfIncludes: "c" })], x: [], input: "a c", scope: "myMessages" },
    { name: "chained rules", s: [r("a", "b"), r("b", "c")], x: [], input: "a", scope: "myMessages" },
    { name: "others scope skipped on send", s: [r("a", "b", { scope: "othersMessages" })], x: [], input: "a", scope: "myMessages" },
    { name: "others scope on incoming", s: [r("a", "b", { scope: "othersMessages" }), r("c", "d")], x: [], input: "a c", scope: "othersMessages" },
    { name: "all scope everywhere", s: [r("a", "b", { scope: "allMessages" })], x: [], input: "a", scope: "othersMessages" },
    { name: "tester applies every scope", s: [r("a", "b", { scope: "othersMessages" }), r("c", "d")], x: [], input: "a c", scope: "allMessages" },
    { name: "empty find skipped", s: [r("", "x")], x: [r("", "y")], input: "abc", scope: "myMessages" },
    { name: "empty content", s: [r("a", "b")], x: [], input: "", scope: "myMessages" },
    { name: "whitespace only content", s: [], x: [], input: "   ", scope: "myMessages" },
    { name: "result trimmed", s: [], x: [r("x", "")], input: "  x  y x ", scope: "myMessages" },
    { name: "bare regex is global", s: [], x: [r("o+", "0")], input: "foo boo", scope: "myMessages" },
    { name: "slash regex without flags", s: [], x: [r("/o/", "0")], input: "foo", scope: "myMessages" },
    { name: "slash regex flags", s: [], x: [r("/O/gi", "0")], input: "fOo", scope: "myMessages" },
    { name: "duplicate flags", s: [], x: [r("/o/ggi", "0")], input: "fOo", scope: "myMessages" },
    { name: "flags without leading slash", s: [], x: [r("o/i", "0")], input: "fOo", scope: "myMessages" },
    { name: "captures", s: [], x: [r("/(\\w+)@(\\w+)/g", "$2 at $1")], input: "me@home you@work", scope: "myMessages" },
    { name: "named capture", s: [], x: [r("(?<w>\\d+)", "<$<w>>")], input: "a1b22", scope: "myMessages" },
    { name: "invalid regex skipped", s: [], x: [r("/(/", "x"), r("a", "b")], input: "a(", scope: "myMessages" },
    { name: "regex newline replacement", s: [], x: [r(";", "\\n")], input: "a;b", scope: "myMessages" },
    { name: "find containing newline", s: [], x: [r("a\nb", "X")], input: "a\nb a\nb", scope: "myMessages" },
    { name: "find ending in newline", s: [], x: [r("b\n", "X")], input: "ab\nb\nc", scope: "myMessages" },
    { name: "regex scope others", s: [], x: [r("a", "b", { scope: "othersMessages" })], input: "a", scope: "othersMessages" },
    { name: "string then regex order", s: [r("x", "y")], x: [r("y", "z")], input: "x", scope: "myMessages" },
    { name: "multiline flag", s: [], x: [r("/^/gm", "> ")], input: "a\nb", scope: "myMessages" },
    { name: "dollar end", s: [], x: [r("/b$/", "!")], input: "ab\nab", scope: "myMessages" },
    { name: "unicode", s: [r("é", "e")], x: [r("/ß/", "ss")], input: "café ßtraße", scope: "myMessages" },
    { name: "zero width regex", s: [], x: [r("\\b", "|")], input: "ab cd", scope: "myMessages" },
];
writeFileSync(new URL("./textreplace-golden.json", import.meta.url), JSON.stringify(trCases.map(c => ({
    name: c.name, stringRules: c.s, regexRules: c.x, input: c.input, scope: c.scope,
    expected: applyRules(c.input, c.scope, { stringRules: c.s, regexRules: c.x })
})), null, 1) + "\n");

// ---- Unindent (src/plugins/unindent/index.ts) ----
function unindent(str) {
    str = str.replace(/\t/g, "    ");
    const minIndent = str.match(/^ *(?=\S)/gm)
        ?.reduce((prev, curr) => Math.min(prev, curr.length), Infinity) ?? 0;
    if (!minIndent) return str;
    return str.replace(new RegExp(`^ {${minIndent}}`, "gm"), "");
}
function unindentMsg(content) {
    return content.replace(/```(.|\n)*?```/g, m => {
        const lines = m.split("\n");
        if (lines.length < 2) return m;
        let suffix = "";
        if (lines[lines.length - 1] === "```") suffix = lines.pop();
        return `${lines[0]}\n${unindent(lines.slice(1).join("\n"))}\n${suffix}`;
    });
}
const unCases = [
    ["basic", "```\n    a\n      b\n```"],
    ["language line kept", "```py\n    def f():\n        pass\n```"],
    ["tabs", "```\n\tif x:\n\t\ty\n```"],
    ["mixed tabs and spaces", "```\n  \ta\n      b\n```"],
    ["blank lines ignored", "```\n    a\n\n    b\n```"],
    ["whitespace-only line", "```\n    a\n  \n    b\n```"],
    ["already flush", "```\na\n  b\n```"],
    ["inline code untouched", "use `  x  ` and ```  y  ``` here"],
    ["single line block untouched", "```    one line```"],
    ["closing fence on code line", "```\n    a\n    b```"],
    ["empty block", "```\n```"],
    ["two blocks", "x\n```\n  a\n```\ny\n```js\n        b\n          c\n```"],
    ["no code", "hello\n    world"],
    ["unclosed", "```\n    a\n    b"],
    ["text around", "before ```\n   a\n   b\n``` after"],
    ["crlf not matched across lines", "```\r\n    a\r\n```"],
    ["nbsp line", "```\n    a\n\u00a0 b\n```"],
];
writeFileSync(new URL("./unindent-golden.json", import.meta.url), JSON.stringify(unCases.map(([name, input]) => ({
    name, input, expected: unindentMsg(input)
})), null, 1) + "\n");
