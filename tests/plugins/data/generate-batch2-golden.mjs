// Dev-only: regenerates writeuppercase-golden.json, polishwording-golden.json and
// ingtoninator-golden.json by running the upstream Equicord functions verbatim
// (types removed, settings passed in, Math.random replaced by a fixed sequence):
//   node generate-batch2-golden.mjs
import { writeFileSync } from "fs";

let randomValues = [];
Math.random = () => randomValues.length ? randomValues.shift() : 0;
const out = (name, data) => writeFileSync(new URL(`./${name}`, import.meta.url), JSON.stringify(data, null, 1) + "\n");

// ---- WriteUpperCase (src/equicordplugins/writeUpperCase/index.ts) ----
function writeUpperCase(content, blockedWords) {
    const blockedWordsArray = blockedWords.split(", ");
    const sentences = content.split(/(?<=[.!?]+['")\]]*)(\s+)/);
    return sentences.map((element, i) => {
        if (i % 2 === 1) return element;
        if (!blockedWordsArray.some(word => element.toLowerCase().startsWith(word.toLocaleLowerCase()))) {
            return element.charAt(0).toUpperCase() + element.slice(1);
        } else {
            return element;
        }
    }).join("");
}
const wuc = [
    ["hello world. how are you? fine!", "http, https, ok"],
    ["ok so. ok then", "http, https, ok"],
    ["see https://x.com. nice", "http, https, ok"],
    ["he said \"hi.\" then left", "http, https, ok"],
    ["(wow!) amazing.  really\n\nyes", "http, https, ok"],
    ["ßtraße is long. éclair", "http, https, ok"],
    ["a...b... c", "http, https, ok"],
    ["", "http, https, ok"],
    ["   leading space. x", "http, https, ok"],
    ["ends with dot. ", "http, https, ok"],
    ["Okay fine. okay", "ok"],
];
out("writeuppercase-golden.json", wuc.map(([input, blocked]) => ({ input, blocked, expected: writeUpperCase(input, blocked) })));

// ---- PolishWording (src/equicordplugins/polishWording/index.ts) ----
let store;
function textProcessing(input) {
    if (store.quickDisable) return input;
    let text = input;
    const codeBlockRegex = /```[\s\S]*?```|`[\s\S]*?`/g;
    const codeBlocks = [];
    text = text.replace(codeBlockRegex, match => {
        codeBlocks.push(match);
        return `__CODE_BLOCK_${codeBlocks.length - 1}__`;
    });
    if (store.fixApostrophes || store.expandContractions) text = ensureApostrophe(text);
    if (store.fixCapitalization) text = capitalize(text);
    if (store.fixPunctuation && (Math.random() * 100 < store.fixPunctuationFrequency)) text = addPeriods(text);
    if (store.expandContractions) text = expandContractions(text);
    text = text.replace(/__CODE_BLOCK_(\d+)__/g, (_, index) => codeBlocks[parseInt(index)]);
    return text;
}
const contractionsMap = {
    "wasn't": "was not", "can't": "cannot", "don't": "do not", "won't": "will not", "isn't": "is not",
    "aren't": "are not", "haven't": "have not", "hasn't": "has not", "hadn't": "had not", "doesn't": "does not",
    "didn't": "did not", "shouldn't": "should not", "wouldn't": "would not", "couldn't": "could not",
    "that's": "that is", "what's": "what is", "there's": "there is", "how's": "how is", "where's": "where is",
    "when's": "when is", "who's": "who is", "why's": "why is", "you'll": "you will", "i'll": "I will",
    "they'll": "they will", "it'll": "it will", "i'm": "I am", "you're": "you are", "they're": "they are",
    "he's": "he is", "she's": "she is", "i've": "I have", "you've": "you have", "we've": "we have",
    "they've": "they have", "you'd": "you would", "he'd": "he would", "she'd": "she would", "it'd": "it would",
    "we'd": "we would", "they'd": "they would", "y'all": "you all", "here's": "here is",
};
const missingApostropheMap = {};
for (const contraction in contractionsMap) missingApostropheMap[removeApostrophes(contraction.toLowerCase())] = contraction;
function getCapData(str) { const a = []; for (const c of str) if (c.match(/[a-zA-Z]/)) a.push(c === c.toUpperCase()); return a; }
function restoreCap(str, data) {
    let r = "", d = 0;
    for (let i = 0; i < str.length; i++) {
        const c = str[i];
        if (!c.match(/[a-zA-Z]/)) { r += c; continue; }
        r += data[d] ? c.toUpperCase() : c.toLowerCase();
        if (d < data.length - 1) d++;
    }
    return r;
}
function ensureApostrophe(t) {
    const re = new RegExp(`\\b(${Object.keys(missingApostropheMap).join("|")})\\b`, "gi");
    return t.replace(re, m => {
        const l = m.toLowerCase();
        if (Object.prototype.hasOwnProperty.call(missingApostropheMap, l)) return restoreCap(missingApostropheMap[l], getCapData(m));
        return m;
    });
}
function expandContractions(t) {
    const re = new RegExp(`\\b(${Object.keys(contractionsMap).join("|")})\\b`, "gi");
    return t.replace(re, m => {
        const l = m.toLowerCase();
        if (Object.prototype.hasOwnProperty.call(contractionsMap, l)) return restoreCap(contractionsMap[l], getCapData(m));
        return m;
    });
}
function removeApostrophes(s) { return s.replace(/'/g, ""); }
function capitalize(textInput) {
    const sentenceSplitRegex = /((?<!\w\.\w.)(?<!\b[A-Z][a-z]\.)(?<![A-Z]\.)(?<!\.)(?<=[.?!])\s+|\n+)/;
    const parts = textInput.split(sentenceSplitRegex);
    const filteredParts = parts.filter(part => part !== undefined && part !== null);
    const blockedWordsArray = (store.blockedWords || "").split(/,\s?/).filter(bw => bw).map(bw => bw.toLowerCase());
    let result = "";
    for (let i = 0; i < filteredParts.length; i++) {
        const element = filteredParts[i];
        const isSentence = !sentenceSplitRegex.test(element);
        if (isSentence) {
            if (!element) continue;
            else if (element.trim() === "") { result += element; continue; }
            const firstWordMatch = element.match(/^\s*([\w'-]+)/);
            const firstWord = firstWordMatch ? firstWordMatch[1].toLowerCase() : "";
            const isBlocked = firstWord ? blockedWordsArray.includes(firstWord) : false;
            if (!isBlocked && !element.startsWith("http")) {
                result += element.replace(/^(\s*)(\S)/, (match, leadingSpace, firstChar) => leadingSpace + firstChar.toUpperCase());
            } else {
                result += element;
            }
        } else {
            if (element) result += element;
        }
    }
    result = result.replace(/\bi\b(?!\s+is\b)(?=['\s]|$)/g, "I");
    return result;
}
function addPeriods(textInput) {
    if (!textInput) return "";
    const lines = textInput.split("\n");
    const processedLines = [];
    for (let i = 0; i < lines.length; i++) {
        const strippedLine = lines[i].trimEnd();
        const urlRegex = /https?:\/\/\S+$|www\.\S+$/;
        if (!strippedLine) {
            if (i < lines.length - 1) processedLines.push("");
        } else {
            const lastChar = strippedLine.slice(-1);
            if (/[A-Za-z0-9]/.test(lastChar) && !urlRegex.test(strippedLine)) { processedLines.push(strippedLine + "."); continue; }
            processedLines.push(strippedLine);
        }
    }
    return processedLines.join("\n");
}
const defaults = { quickDisable: false, blockedWords: "", fixApostrophes: true, expandContractions: false,
    fixCapitalization: false, fixPunctuation: false, fixPunctuationFrequency: 100 };
const all = { ...defaults, fixCapitalization: true, fixPunctuation: true };
const pw = [
    ["dont do that, im busy and youre late", defaults, []],
    ["Dont SHOUT. CANT stop", defaults, []],
    ["i think i'm fine. what about you? i is a letter", { ...defaults, fixCapitalization: true }, []],
    ["e.g. this stays. Mr. Smith arrived. U.S. is big. ok", { ...defaults, fixCapitalization: true }, []],
    ["first line\nsecond line\n\nthird", all, [0.5]],
    ["no period here please", all, [0.99]],
    ["no period here please", { ...all, fixPunctuationFrequency: 50 }, [0.75]],
    ["visit https://example.com\nor www.x.org", all, [0]],
    ["keep `dont` and ```\ncant\n``` but fix dont", defaults, []],
    ["I can't and you won't. they'd go", { ...defaults, expandContractions: true }, []],
    ["CANT WONT", { ...defaults, expandContractions: true }, []],
    ["lol so. bruh moment. ok", { ...all, blockedWords: "lol, bruh" }, [0]],
    ["quick", { ...all, quickDisable: true }, [0]],
    ["ill be there. hes here", defaults, []],
    ["trailing spaces   \nnext   ", all, [0]],
    ["", all, [0]],
    ["__CODE_BLOCK_0__ typed by hand", { ...defaults, fixApostrophes: false }, []],
];
out("polishwording-golden.json", pw.map(([input, options, random]) => {
    store = options;
    randomValues = [...random];
    let expected = textProcessing(input);
    return { input, options, random, expected };
}));

// ---- Ingtoninator (src/equicordplugins/ingtoninator/index.tsx) ----
const isLegal = word => {
    if (word === "i" || word === "I") return false;
    const lastChar = word.slice(-1).toLowerCase();
    if (["a", "e", "o", "u", "y"].includes(lastChar)) return false;
    return true;
};
const getWords = string => {
    const linkRegex = /https?:\/\/[^\s]+\.[^\s]+/g;
    const linkRanges = new Set();
    for (const match of string.matchAll(linkRegex)) {
        const start = match.index;
        const end = start + match[0].length;
        for (let i = start; i < end; i++) linkRanges.add(i);
    }
    return Array.from(string.matchAll(/[\p{L}]+/gu), match => ({ word: match[0], startIndex: match.index }))
        .filter(match => !linkRanges.has(match.startIndex));
};
const chooseRandomWord = message => {
    const words = getWords(message);
    while (words.length > 0) {
        const index = Math.floor(Math.random() * words.length);
        const wordMatch = words[index];
        if (!isLegal(wordMatch.word)) { words.splice(index, 1); continue; }
        return wordMatch;
    }
    return null;
};
const ington = word => {
    if (word.endsWith("INGTON")) return "";
    if (word.endsWith("INGTO")) return "N";
    if (word.endsWith("INGT")) return "ON";
    if (word.endsWith("ING")) return "TON";
    if (word.endsWith("IN")) return "GTON";
    if (word.endsWith("I")) return "NGTON";
    return "INGTON";
};
function handle(msg) {
    if (!msg || !msg.trim()) return msg;
    const wordMatch = chooseRandomWord(msg);
    if (wordMatch === null) return msg;
    const { word } = wordMatch;
    const wordUpper = word.toUpperCase();
    const isUpper = word === wordUpper;
    let insertion = ington(wordUpper);
    if (!isUpper) insertion = insertion.toLowerCase();
    const idx = wordMatch.startIndex + word.length;
    return msg.slice(0, idx) + insertion + msg.slice(idx);
}
const ing = [
    ["hello world", [0]], ["hello world", [0.99]], ["WASHING machine", [0]], ["I am here", [0, 0]],
    ["see https://example.com now", [0.4]], ["café time", [0]], ["only vowels a e", [0, 0]],
    ["Testing testINGTO", [0.7]], ["   ", [0]], ["", [0]], ["日本語 text", [0]], ["Kevin", [0]],
];
out("ingtoninator-golden.json", ing.map(([input, random]) => {
    randomValues = [...random];
    return { input, random, expected: handle(input) };
}));
