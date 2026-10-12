// Golden data for FakeNitro, from upstream's own code: the send and edit
// listeners (sticker and emoji bypass), canUseEmote(), transformAst(),
// patchFakeNitroStickers(), shouldIgnoreEmbed() and filterAttachments(),
// cut out of src/plugins/fakeNitro/index.tsx and run here against stub stores.
// IconUtils.getEmojiURL is Discord's and is not in the repository; the stub
// below is the URL shape Qord assumes (see the plugin's notes).
//   node tests/plugins/data/generate-fakenitro-golden.mjs > tests/plugins/data/fakenitro-golden.json
import { readFileSync } from "node:fs";

const src = readFileSync(new URL("../../../ref/Equicord/src/plugins/fakeNitro/index.tsx", import.meta.url), "utf8");

const stripTypes = code => code
    .replace(/: URL \| null/g, "")
    .replace(/: Array<string>/g, "")
    .replace(/: Array<any>/g, "")
    .replace(/ as Array<any>/g, "")
    .replace(/([\w\])])!(?=[.)\]\s;,])/g, "$1");

// A method of the plugin object: from its signature to the closing "    },"
const method = (name, signature) => {
    const start = src.indexOf(`\n    ${name}(`);
    if (start < 0) throw new Error(`no method ${name}`);
    const bodyStart = src.indexOf("{\n", start) + 2;
    const end = src.indexOf("\n    },\n", bodyStart);
    return `${signature} {\n${stripTypes(src.slice(bodyStart, end))}\n}`;
};
const constant = name => {
    const m = new RegExp(`const ${name} = (.+);`).exec(src);
    if (!m) throw new Error(`no constant ${name}`);
    return m[1];
};
const between = (from, to) => {
    const start = src.indexOf(from);
    const end = src.indexOf(to, start);
    if (start < 0 || end < 0) throw new Error(`no block ${from}`);
    return src.slice(start + from.length, end);
};

const fakeNitroEmojiRegex = eval(constant("fakeNitroEmojiRegex"));
const fakeNitroStickerRegex = eval(constant("fakeNitroStickerRegex"));
const fakeNitroGifStickerRegex = eval(constant("fakeNitroGifStickerRegex"));
const hyperLinkRegex = eval(constant("hyperLinkRegex"));
const hyperLinkOnlyRegex = new RegExp(`^${hyperLinkRegex.source}$`);
const getWordBoundary = new Function(`return ${stripTypes(between("function getWordBoundary", "\n}\n").replace(/\(origStr: string, offset: number\)/, "function (origStr, offset)"))}\n}`)();

// Stub stores, filled per case
const state = { premium: 0, guildId: undefined, denied: new Set(), emojis: {}, stickers: {}, myRoles: {}, settings: {} };
const settings = { get store() { return state.settings; } };
const StickersStore = { getStickerById: id => state.stickers[id] };
const EmojiStore = { getCustomEmojiById: id => state.emojis[id] };
const GuildMemberStore = { getSelfMember: gid => ({ roles: state.myRoles[gid] ?? [] }) };
const IconUtils = { getEmojiURL: ({ id, animated, size }) => `https://cdn.discordapp.com/emojis/${id}.webp?size=${size}${animated ? "&animated=true" : ""}` };
const StickerFormatType = { PNG: 1, APNG: 2, LOTTIE: 3, GIF: 4 };
const logger = { error() {} };
const isUnusableRoleSubscriptionEmoji = () => false;
const has = perm => !state.denied.has(perm);
const hasExternalEmojiPerms = () => has("USE_EXTERNAL_EMOJIS");
const hasExternalStickerPerms = () => has("USE_EXTERNAL_STICKERS");
const hasEmbedPerms = () => has("EMBED_LINKS");
const hasAttachmentPerms = () => has("ATTACH_FILES");
let modals = 0;
const openModal = () => { ++modals; };
let embedNoticeAnswer = true;
let embedNotices = 0;
const showCannotEmbedNotice = async () => { ++embedNotices; return embedNoticeAnswer; };

const scope = { settings, StickersStore, EmojiStore, GuildMemberStore, IconUtils, StickerFormatType, logger, isUnusableRoleSubscriptionEmoji, hasExternalEmojiPerms, hasExternalStickerPerms, hasEmbedPerms, hasAttachmentPerms, openModal, showCannotEmbedNotice, getWordBoundary, fakeNitroEmojiRegex, fakeNitroStickerRegex, fakeNitroGifStickerRegex, hyperLinkRegex, hyperLinkOnlyRegex };
const build = code => new Function(...Object.keys(scope), `return (${code});`)(...Object.values(scope));

let animatedSent = [];
const plugin = {
    get guildId() { return state.guildId; },
    get canUseEmotes() { return state.premium > 0; },
    get canUseStickers() { return state.premium > 1; },
    sendAnimatedSticker(link, id) { animatedSent.push({ link, id }); },
};
for (const [name, sig] of [
    ["transformAst", "function (ast)"],
    ["patchFakeNitroStickers", "function (stickers, message)"],
    ["shouldIgnoreEmbed", "function (embed, message)"],
    ["filterAttachments", "function (attachments)"],
    ["getStickerLink", "function ({ format_type, id })"],
    ["canUseEmote", "function (e, channelId)"],
])
    plugin[name] = build(method(name, sig));

// The listeners, with the modal JSX taken out
let preSendBody = between("this.preSend = addMessagePreSendListener(async (channelId, messageObj, options) => {", "\n        });\n\n        this.preEdit");
preSendBody = preSendBody.replace(/openModal\(props => \([\s\S]*?\n {24}\)\);/, "openModal(null);");
const preEditBody = between("this.preEdit = addMessagePreEditListener(async (channelId, __, messageObj) => {", "\n        });\n    },");
const makePreSend = build(`function (s) { return async (channelId, messageObj, options) => {${stripTypes(preSendBody)}\n}; }`);
const makePreEdit = build(`function (s) { return async (channelId, __, messageObj) => {${stripTypes(preEditBody)}\n}; }`);

const defaults = {
    enableEmojiBypass: true, emojiSize: 48, transformEmojis: true, enableStickerBypass: true, stickerSize: 160,
    transformStickers: true, transformCompoundSentence: false, enableStreamQualityBypass: true,
    useStickerHyperLinks: true, useEmojiHyperLinks: true, hyperLinkText: "{{NAME}}", disableEmbedPermissionCheck: false,
};

const G1 = "100000000000000001", G2 = "100000000000000002";
const emojis = {
    "200000000000000001": { id: "200000000000000001", name: "pepe", animated: false, guildId: G1, available: true, managed: false, roles: [], type: 1 },
    "200000000000000002": { id: "200000000000000002", name: "party", animated: true, guildId: G1, available: true, managed: false, roles: [], type: 1 },
    "200000000000000003": { id: "200000000000000003", name: "far_away", animated: false, guildId: G2, available: true, managed: false, roles: [], type: 1 },
    "200000000000000004": { id: "200000000000000004", name: "lost", animated: false, guildId: G1, available: false, managed: false, roles: [], type: 1 },
    "200000000000000005": { id: "200000000000000005", name: "twitch", animated: false, guildId: G2, available: true, managed: true, roles: ["300000000000000001"], type: 1 },
};
const stickers = {
    "400000000000000001": { id: "400000000000000001", name: "Wave", format_type: 1, guild_id: G1, available: true },
    "400000000000000002": { id: "400000000000000002", name: "Dance Party", format_type: 2, guild_id: G2, available: true },
    "400000000000000003": { id: "400000000000000003", name: "gif one", format_type: 4, guild_id: G2, available: true },
    "400000000000000004": { id: "400000000000000004", name: "png far", format_type: 1, guild_id: G2, available: true },
    "400000000000000005": { id: "400000000000000005", name: "standard", format_type: 3, pack_id: "1", available: true },
    "400000000000000006": { id: "400000000000000006", name: "gone", format_type: 1, guild_id: G1, available: false },
    "400000000000000007": { id: "400000000000000007", name: "Ünï cödé!*~ &=+", format_type: 1, guild_id: G2, available: true },
};

const reset = c => {
    state.premium = c.premium ?? 0;
    state.guildId = c.guildId ?? undefined;
    state.denied = new Set(c.denied ?? []);
    state.emojis = emojis;
    state.stickers = stickers;
    state.myRoles = c.myRoles ?? {};
    state.settings = { ...defaults, ...(c.settings ?? {}) };
    animatedSent = [];
    modals = 0;
    embedNotices = 0;
    embedNoticeAnswer = c.confirm ?? true;
};

const validNonShortcutEmojis = content => {
    const seen = new Set();
    const out = [];
    for (const m of content.matchAll(/<a?:\w+:(\d+)>/g)) {
        const emoji = emojis[m[1]];
        if (emoji && !seen.has(m[1])) { seen.add(m[1]); out.push(emoji); }
    }
    return out;
};

const sendCases = [
    { name: "usable emoji in its server", guildId: G1, content: "hi <:pepe:200000000000000001>" },
    { name: "animated without nitro", guildId: G1, content: "<a:party:200000000000000002>" },
    { name: "other server without nitro", guildId: G1, content: "look <:far_away:200000000000000003> there" },
    { name: "no hyperlinks", guildId: G1, content: "x<:far_away:200000000000000003>y", settings: { useEmojiHyperLinks: false, emojiSize: 96 } },
    { name: "custom link text", guildId: G1, content: "<:far_away:200000000000000003>", settings: { hyperLinkText: "emoji {{NAME}} {{NAME}}" } },
    { name: "nitro, external allowed", premium: 2, guildId: G1, content: "<:far_away:200000000000000003>" },
    { name: "nitro, external denied", premium: 2, guildId: G1, content: "<:far_away:200000000000000003>", denied: ["USE_EXTERNAL_EMOJIS"] },
    { name: "unavailable emoji", premium: 2, guildId: G1, content: "<:lost:200000000000000004>" },
    { name: "twitch emoji with role", guildId: G1, content: "<:twitch:200000000000000005>", myRoles: { [G2]: ["300000000000000001"] } },
    { name: "twitch emoji without role", guildId: G1, content: "<:twitch:200000000000000005>" },
    { name: "in a DM", content: "<:pepe:200000000000000001> and <a:party:200000000000000002>" },
    { name: "sticker name encoding", guildId: G1, content: "", stickerIds: ["400000000000000007"], settings: { hyperLinkText: "$& {{NAME}}" } },
    { name: "unknown emoji left alone", guildId: G1, content: "<:nope:299999999999999999>" },
    { name: "emoji bypass off", guildId: G1, content: "<:far_away:200000000000000003>", settings: { enableEmojiBypass: false } },
    { name: "no embed permission, confirmed", guildId: G1, content: "<:far_away:200000000000000003>", denied: ["EMBED_LINKS"] },
    { name: "no embed permission, cancelled", guildId: G1, content: "<:far_away:200000000000000003>", denied: ["EMBED_LINKS"], confirm: false },
    { name: "no embed permission, check disabled", guildId: G1, content: "<:far_away:200000000000000003>", denied: ["EMBED_LINKS"], settings: { disableEmbedPermissionCheck: true } },
    { name: "sticker from its server", guildId: G1, content: "", stickerIds: ["400000000000000001"] },
    { name: "sticker from elsewhere", guildId: G1, content: "", stickerIds: ["400000000000000004"] },
    { name: "sticker with text", guildId: G1, content: "look", stickerIds: ["400000000000000004"], settings: { useStickerHyperLinks: false, stickerSize: 256 } },
    { name: "gif sticker", guildId: G1, content: "a ", stickerIds: ["400000000000000003"] },
    { name: "standard sticker", guildId: G1, content: "", stickerIds: ["400000000000000005"] },
    { name: "unavailable sticker", guildId: G1, premium: 2, content: "", stickerIds: ["400000000000000006"] },
    { name: "nitro basic sticker", guildId: G1, premium: 3, content: "", stickerIds: ["400000000000000004"] },
    { name: "nitro classic sticker", guildId: G1, premium: 1, content: "", stickerIds: ["400000000000000004"] },
    { name: "apng sticker", guildId: G1, content: "", stickerIds: ["400000000000000002"] },
    { name: "apng sticker without attach permission", guildId: G1, content: "", stickerIds: ["400000000000000002"], denied: ["ATTACH_FILES"] },
    { name: "sticker bypass off", guildId: G1, content: "", stickerIds: ["400000000000000004"], settings: { enableStickerBypass: false } },
    { name: "sticker and emoji", guildId: G1, content: "<:far_away:200000000000000003>", stickerIds: ["400000000000000004"] },
];
const send = [];
for (const c of sendCases) {
    reset(c);
    const messageObj = { content: c.content, validNonShortcutEmojis: validNonShortcutEmojis(c.content) };
    const options = { stickerIds: c.stickerIds ? [...c.stickerIds] : undefined };
    const result = await makePreSend.call(plugin, state.settings)("500000000000000001", messageObj, options);
    send.push({ ...c, expected: { content: messageObj.content, stickerIds: options.stickerIds ?? [], cancel: result?.cancel ?? false, animatedSent, modals, embedNotices } });
}

const editCases = [
    { name: "edit with an emoji from elsewhere", guildId: G1, content: "edit <:far_away:200000000000000003>!" },
    { name: "edit with an animated emoji", guildId: G1, content: "<a:party:200000000000000002><:pepe:200000000000000001>" },
    { name: "escaped emoji untouched", guildId: G1, content: "\\<:far_away:200000000000000003>" },
    { name: "edit, bypass off", guildId: G1, content: "<:far_away:200000000000000003>", settings: { enableEmojiBypass: false } },
    { name: "edit without embed permission", guildId: G1, content: "<:far_away:200000000000000003>", denied: ["EMBED_LINKS"], confirm: false },
];
const edit = [];
for (const c of editCases) {
    reset(c);
    const messageObj = { content: c.content };
    const result = await makePreEdit.call(plugin, state.settings)("500000000000000001", undefined, messageObj);
    edit.push({ ...c, expected: { content: messageObj.content, cancel: result?.cancel ?? false, embedNotices } });
}

// transformAst() on single links (the case Qord transforms without compound sentences)
reset({});
const links = [
    "https://cdn.discordapp.com/emojis/200000000000000001.webp?size=48&name=pepe&lossless=true",
    "https://cdn.discordapp.com/emojis/299999999999999999.webp?size=48&name=some+thing&lossless=true",
    "https://cdn.discordapp.com/emojis/299999999999999998.gif?size=48&name=x",
    "https://cdn.discordapp.com/emojis/299999999999999997.webp?size=48&animated=true",
    "https://cdn.discordapp.com/emojis/299999999999999996.png",
    "https://cdn.discordapp.com/emojis/299999999999999995.webp?size=48&name=%C3%A9%20e",
    "https://media.discordapp.net/stickers/400000000000000004.png?size=160&name=png+far&lossless=true",
    "https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif",
    "https://cdn.discordapp.com/attachments/1/2/499999999999999999.gif",
    "https://example.com/emojis/12.png",
    "https://example.com/other.png",
];
const ast = links.map(target => {
    const out = plugin.transformAst([{ type: "link", target }]);
    return { target, result: out.length ? out[0] : null };
});

const stickerCases = [
    { content: "https://media.discordapp.net/stickers/400000000000000004.png?size=160&name=png+far&lossless=true" },
    { content: "https://media.discordapp.net/stickers/499999999999999999.png?size=160&name=Some+Name" },
    { content: "[png far](https://media.discordapp.net/stickers/499999999999999999.png?size=160&name=x)" },
    { content: "[two words](https://media.discordapp.net/stickers/499999999999999999.png?size=160&name=x)" },
    { content: "look https://media.discordapp.net/stickers/499999999999999999.png?size=160" },
    { content: "look https://media.discordapp.net/stickers/499999999999999999.png?size=160", compound: true },
    { content: "", gifs: ["https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif", "https://cdn.discordapp.com/attachments/1/2/499999999999999999.gif"] },
    { content: "plain text" },
];
const stickerResults = stickerCases.map(c => {
    reset({ settings: { transformCompoundSentence: !!c.compound } });
    const message = { content: c.content, attachments: (c.gifs ?? []).map(url => ({ url, content_type: "image/gif" })) };
    return { ...c, expected: plugin.patchFakeNitroStickers([], message) };
});

const embedCases = [
    { content: "https://cdn.discordapp.com/emojis/1.webp?size=48", type: "image", url: "https://cdn.discordapp.com/emojis/1.webp?size=48" },
    { content: "[x](https://cdn.discordapp.com/emojis/1.webp?size=48)", type: "image", url: "https://cdn.discordapp.com/emojis/1.webp?size=48" },
    { content: "[two words](https://cdn.discordapp.com/emojis/1.webp?size=48)", type: "image", url: "https://cdn.discordapp.com/emojis/1.webp?size=48" },
    { content: "hey https://cdn.discordapp.com/emojis/1.webp?size=48", type: "image", url: "https://cdn.discordapp.com/emojis/1.webp?size=48" },
    { content: "hey https://cdn.discordapp.com/emojis/1.webp?size=48", type: "image", url: "https://cdn.discordapp.com/emojis/1.webp?size=48", compound: true },
    { content: "https://media.discordapp.net/stickers/5.png", type: "image", url: "https://media.discordapp.net/stickers/5.png" },
    { content: "https://media.discordapp.net/stickers/5.png", type: "image", url: "https://media.discordapp.net/stickers/5.png", stickers: false },
    { content: "https://cdn.discordapp.com/emojis/1.webp", type: "rich", url: "https://cdn.discordapp.com/emojis/1.webp" },
    { content: "https://cdn.discordapp.com/emojis/1.webp", type: "image", url: "https://cdn.discordapp.com/emojis/2.webp" },
    { content: "https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif", type: "image", url: "https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif" },
    { content: "https://example.com/a.png", type: "image", url: "https://example.com/a.png" },
];
const embedResults = embedCases.map(c => {
    reset({ settings: { transformCompoundSentence: !!c.compound, transformStickers: c.stickers ?? true } });
    return { ...c, expected: plugin.shouldIgnoreEmbed({ type: c.type, url: c.url }, { content: c.content }) };
});

reset({});
const attachments = [
    { url: "https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif", content_type: "image/gif" },
    { url: "https://cdn.discordapp.com/attachments/1/2/499999999999999999.gif", content_type: "image/gif" },
    { url: "https://cdn.discordapp.com/attachments/1/2/400000000000000002.gif", content_type: "image/png" },
];
const kept = plugin.filterAttachments(attachments);
const attachmentResults = attachments.map(a => ({ ...a, hidden: !kept.includes(a) }));

const boundaries = [["a b", -1], ["a b", 0], ["a b", 1], ["a b", 3], ["a b", 1], ["", 0], ["xy", 1]]
    .map(([s, offset]) => ({ s, offset, expected: getWordBoundary(s, offset) }));

console.log(JSON.stringify({ emojis, stickers, boundaries, send, edit, ast, stickers_: stickerResults, embeds: embedResults, attachments: attachmentResults }, null, 1));
