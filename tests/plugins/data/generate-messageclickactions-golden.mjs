// Golden data for MessageClickActions: normalizeEmoji() and the reaction list
// (getConfiguredReactionEmojis), read from upstream's source and run here, plus
// the quote text (copied from quoteMessage()).
//   node tests/plugins/data/generate-messageclickactions-golden.mjs > tests/plugins/data/messageclickactions-golden.json
import { readFileSync } from "node:fs";

const src = readFileSync(new URL("../../../ref/Equicord/src/plugins/messageClickActions/index.ts", import.meta.url), "utf8");
const fn = name => {
    const start = src.indexOf(`function ${name}(`);
    const end = src.indexOf("\n}\n", start) + 3;
    return src.slice(start, end).replace(/: string \| null/g, "").replace(/\(emoji: string\)/g, "(emoji)")
        .replace(/\(emoji\): emoji is string/g, "(emoji)");
};
const MAX_ADDITIONAL_REACT_EMOJIS = Number(/MAX_ADDITIONAL_REACT_EMOJIS = (\d+)/.exec(
    readFileSync(new URL("../../../ref/Equicord/src/plugins/messageClickActions/ReactEmojiSetting.tsx", import.meta.url), "utf8"))?.[1] ?? 3);
const settings = { store: {} };
const normalizeEmoji = new Function(`${fn("normalizeEmoji")}; return normalizeEmoji;`)();
const getConfigured = new Function("settings", "normalizeEmoji", "MAX_ADDITIONAL_REACT_EMOJIS",
    `${fn("getConfiguredReactionEmojis")}; return getConfiguredReactionEmojis;`)(settings, normalizeEmoji, MAX_ADDITIONAL_REACT_EMOJIS);

const emojis = ["💀", "  👍  ", "", "   ", "<:pepe:123456789012345678>", "<a:party:223456789012345678>",
    ":blob:323456789012345678:", "wave:423456789012345678", "name~1:523456789012345678", "not a custom one", "<:x:12>"];
const normalized = emojis.map(e => ({ input: e, expected: normalizeEmoji(e) }));

const reactCases = [
    ["💀", false, ""],
    ["💀", true, "👍, 🎉\n❤️"],
    ["💀", true, "💀,👍,👍,🎉,❤️,🔥"],
    ["<:pepe:123456789012345678>", true, ",, 👍 ,"],
    ["", true, "👍"],
    ["  ", false, "👍"],
].map(([reactEmoji, addAdditionalReacts, additionalReactEmojis]) => {
    settings.store = { reactEmoji, addAdditionalReacts, additionalReactEmojis };
    return { reactEmoji, addAdditionalReacts, additionalReactEmojis, expected: getConfigured() };
});

const quote = content => content.split("\n").map(line => `> ${line}`).join("\n") + "\n";
const quotes = ["hello", "two\nlines", "", "trailing\n"].map(c => ({ input: c, expected: quote(c) }));

console.log(JSON.stringify({ max: MAX_ADDITIONAL_REACT_EMOJIS, normalized, reactCases, quotes }, null, 1));
