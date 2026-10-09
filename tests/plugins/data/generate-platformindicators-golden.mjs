// Golden data for PlatformIndicators, from upstream's source
// (ref/Equicord/src/plugins/platformIndicators/index.tsx):
//  - the icon table (path, viewBox, size) read from the file
//  - useEnsureOwnStatus()'s sort + reduce, copied verbatim and run here
//   node tests/plugins/data/generate-platformindicators-golden.mjs > tests/plugins/data/platformindicators-golden.json
import { readFileSync } from "node:fs";

const src = readFileSync(new URL("../../../ref/Equicord/src/plugins/platformIndicators/index.tsx", import.meta.url), "utf8");
const icons = {};
for (const m of src.matchAll(/^\s+(\w+): Icon\("([^"]+)"(?:, \{ viewBox: "([^"]+)"(?:, height: (\d+), width: (\d+))? \})?\)/gm)) {
    icons[m[1]] = { path: m[2], viewBox: m[3] ?? "0 0 24 24", height: Number(m[4] ?? 20), width: Number(m[5] ?? 20) };
}

function ownStatus(sessions) {
    const sortedSessions = Object.values(sessions).sort(({ status: a }, { status: b }) => {
        if (a === b) return 0;
        if (a === "online") return 1;
        if (b === "online") return -1;
        if (a === "idle") return 1;
        if (b === "idle") return -1;
        return 0;
    });

    const ownStatus = Object.values(sortedSessions).reduce((acc, curr) => {
        if (curr.clientInfo.client !== "unknown")
            acc[curr.clientInfo.client] = curr.status;
        return acc;
    }, {});
    return Object.entries(ownStatus);
}

const s = (client, status) => ({ clientInfo: { client }, status });
const sessionCases = [
    [s("desktop", "online")],
    [s("unknown", "online"), s("desktop", "idle")],
    [s("mobile", "online"), s("desktop", "idle")],
    [s("desktop", "online"), s("desktop", "idle"), s("web", "dnd")],
    [s("desktop", "dnd"), s("mobile", "idle"), s("desktop", "online"), s("web", "online")],
    [s("web", "idle"), s("web", "dnd"), s("mobile", "dnd"), s("mobile", "online")],
    [s("desktop", "invisible"), s("mobile", "dnd")],
    [s("embedded", "online"), s("vr", "idle"), s("desktop", "dnd"), s("unknown", "idle")],
];
const own = sessionCases.map(c => ({
    sessions: c.map(x => ({ client: x.clientInfo.client, status: x.status })),
    expected: ownStatus(c),
}));
console.log(JSON.stringify({ icons, own }, null, 1));
