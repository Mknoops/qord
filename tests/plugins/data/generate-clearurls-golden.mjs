// Dev-only: regenerates clearurls-golden.json from clearurls-inputs.json with
// node generate-clearurls-golden.mjs clearurls-inputs.json > clearurls-golden.json
// Upstream Equicord ClearURLs logic (src/plugins/clearURLs/index.ts), verbatim
// apart from types, run against the test fixture to produce golden outputs.
import { readFileSync } from "fs";
const res = JSON.parse(readFileSync(new URL("./clearurls-data.min.json", import.meta.url), "utf8"));
const rules = [];
for (const [name, provider] of Object.entries(res.providers)) {
    const urlPattern = new RegExp(provider.urlPattern, "i");
    const r = provider.rules?.map(rule => new RegExp(rule, "i"));
    const rawRules = provider.rawRules?.map(rule => new RegExp(rule, "i"));
    const exceptions = provider.exceptions?.map(ex => new RegExp(ex, "i"));
    rules.push({ name, urlPattern, rules: r, rawRules, exceptions });
}
function replacer(match) {
    try { var url = new URL(match); } catch (error) { return match; }
    if (url.searchParams.entries().next().done) return match;
    rules.forEach(({ urlPattern, exceptions, rawRules, rules }) => {
        if (!urlPattern.test(url.href) || exceptions?.some(ex => ex.test(url.href))) return;
        const toDelete = [];
        if (rules) {
            url.searchParams.forEach((_, param) => {
                if (rules.some(rule => rule.test(param))) toDelete.push(param);
            });
        }
        toDelete.forEach(param => url.searchParams.delete(param));
        let cleanedUrl = url.href;
        rawRules?.forEach(rawRule => { cleanedUrl = cleanedUrl.replace(rawRule, ""); });
        url = new URL(cleanedUrl);
    });
    return url.toString();
}
function cleanMessage(content) {
    if (/http(s)?:\/\//.test(content)) {
        return content.replace(/(https?:\/\/[^\s<]+[^<.,:;"'>)|\]\s])/g, match => replacer(match));
    }
    return content;
}
const inputs = JSON.parse(readFileSync(process.argv[2], "utf8"));
const out = inputs.map(i => { try { return [i, cleanMessage(i)]; } catch (e) { return [i, "<throws: " + e.message + ">"]; } });
console.log(JSON.stringify(out, null, 1));
