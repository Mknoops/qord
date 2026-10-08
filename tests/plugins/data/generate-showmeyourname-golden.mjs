// Golden data for ShowMeYourName. splitTemplate, parseTemplateItem and
// validTemplate are copied verbatim from upstream; compose() is upstream's
// renderUsername() reduced to the text its name element shows (first name,
// then separator + prefix + name + suffix), without styling.
//   node tests/plugins/data/generate-showmeyourname-golden.mjs > tests/plugins/data/showmeyourname-golden.json
const symbolPattern = /^[\p{S}\p{P}]{1,3}$/iu;
const templatePattern = /(?:\{(?:custom|friend|nick|display|user)(?:,\s*(?:custom|friend|nick|display|user))*\})/iu;

function splitTemplate(template) {
    const items = template.trim().split(/(?<!,\s*)\s+/);
    return items;
}

function parseTemplateItem(entry) {
    const [prefix, suffix] = entry.split(templatePattern);
    const names = entry.replace(prefix, "").replace(suffix, "").trim().replaceAll(/{|}/g, "").split(/,\s*/);

    return {
        prefix: prefix ? prefix.trim() : "",
        suffix: suffix ? suffix.trim() : "",
        targetProcessedNames: names.map(name => name.trim()).filter(name => name.length > 0)
    };
}

function validTemplate(value) {
    const items = splitTemplate(value);
    if (items.length > 4 || !items.length) return false;
    const invalidOptions = items.some(item => {
        const { prefix, suffix, targetProcessedNames } = parseTemplateItem(item);
        return (
            prefix.length > 3 ||
            suffix.length > 3 ||
            (!!prefix && !symbolPattern.test(prefix)) ||
            (!!suffix && !symbolPattern.test(suffix)) ||
            targetProcessedNames.length > 5 ||
            targetProcessedNames.some(name => !["custom", "friend", "nick", "display", "user"].includes(name.trim()))
        );
    });
    return !invalidOptions;
}

function compose(n, includedNames, nameSeparator, removeDuplicates) {
    const names = { user: n.user, display: n.display, nick: n.nick, friend: n.friend, custom: n.custom };
    const outputs = [];
    for (const option of splitTemplate(includedNames)) {
        const { prefix, suffix, targetProcessedNames } = parseTemplateItem(option);
        let chosenName = null, chosenType = "";
        for (const name of targetProcessedNames) {
            if (!(name in names)) continue;
            chosenName = names[name];
            chosenType = name;
            if (chosenName) break;
        }
        if (!chosenName) continue;
        outputs.push({ prefix: prefix || "", name: chosenName, type: chosenType, suffix: suffix || "" });
    }
    if (!outputs.length) outputs.push({ prefix: "", name: n.user || "Unknown", suffix: "" });
    let first = outputs.shift(), second = outputs.shift(), third = outputs.shift(), fourth = outputs.shift(), fifth = outputs.shift();
    if (!n.user) return "";
    const prioritizeUsername = (new Set([first, second, third, fourth, fifth].filter(Boolean).map(pos => pos.name.toLowerCase())).size > 1);
    if (removeDuplicates) {
        fifth && fourth && fifth.name.toLowerCase() === fourth.name.toLowerCase() ? fifth.type === "user" && prioritizeUsername ? fourth = null : fifth = null : null;
        fifth && third && fifth.name.toLowerCase() === third.name.toLowerCase() ? fifth.type === "user" && prioritizeUsername ? third = null : fifth = null : null;
        fifth && second && fifth.name.toLowerCase() === second.name.toLowerCase() ? fifth.type === "user" && prioritizeUsername ? second = null : fifth = null : null;
        fifth && first && fifth.name.toLowerCase() === first.name.toLowerCase() ? fifth.type === "user" && prioritizeUsername ? first = null : fifth = null : null;
        fourth && third && fourth.name.toLowerCase() === third.name.toLowerCase() ? fourth.type === "user" && prioritizeUsername ? third = null : fourth = null : null;
        fourth && second && fourth.name.toLowerCase() === second.name.toLowerCase() ? fourth.type === "user" && prioritizeUsername ? second = null : fourth = null : null;
        fourth && first && fourth.name.toLowerCase() === first.name.toLowerCase() ? fourth.type === "user" && prioritizeUsername ? first = null : fourth = null : null;
        third && second && third.name.toLowerCase() === second.name.toLowerCase() ? third.type === "user" && prioritizeUsername ? second = null : third = null : null;
        third && first && third.name.toLowerCase() === first.name.toLowerCase() ? third.type === "user" && prioritizeUsername ? first = null : third = null : null;
        second && first && second.name.toLowerCase() === first.name.toLowerCase() ? second.type === "user" && prioritizeUsername ? first = null : second = null : null;
    }
    const remaining = [first, second, third, fourth, fifth].filter(Boolean);
    const [a, ...rest] = remaining;
    return a.name + rest.map(p => nameSeparator + p.prefix + p.name + p.suffix).join("");
}

const templates = [
    "{custom, friend, nick} [{display}] (@{user})",
    "{user}",
    "{nick, display, user}",
    "  {display}   <{user}>  ",
    "{custom,friend} {nick}",
    "{custom, friend,  nick} [{display}]",
    "[{display}] {user} {nick} {friend}",
    "a{user}",
    "{user} {display} {nick} {friend} {custom}",
    "{bogus}",
    "{user}{display}",
    "!!!{user}???",
    "{Display}",
    "",
];
const people = [
    { user: "benji", display: "Ben", nick: "Benny", friend: null, custom: null },
    { user: "ava", display: "Ava", nick: null, friend: null, custom: null },
    { user: "cleo.dev", display: "Cleo", nick: "cleo.dev", friend: "Cle", custom: "C" },
    { user: "dan", display: null, nick: null, friend: null, custom: null },
    { user: "Qordbot#1234", display: "Qordbot#1234", nick: null, friend: null, custom: null },
    { user: "x", display: "X", nick: "x", friend: "X", custom: "x" },
];
const cases = [];
for (const t of templates) {
    cases.push({
        template: t,
        split: splitTemplate(t),
        items: splitTemplate(t).map(parseTemplateItem),
        valid: validTemplate(t),
        composed: people.map(p => ({ names: p, dedupe: compose(p, t, " ", true), noDedupe: compose(p, t, " | ", false) })),
    });
}
console.log(JSON.stringify(cases, null, 1));
