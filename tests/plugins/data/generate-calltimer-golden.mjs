// Golden data for CallTimer: Equicord's formatDurationMs (src/utils/text.ts),
// read from the pinned source and run here.
//   node tests/plugins/data/generate-calltimer-golden.mjs > tests/plugins/data/calltimer-golden.json
import { readFileSync } from "node:fs";

const src = readFileSync(new URL("../../../ref/Equicord/src/utils/text.ts", import.meta.url), "utf8");
const start = src.indexOf("export function formatDurationMs");
const end = src.indexOf("\n}\n", start) + 3;
const body = src.slice(start, end)
    .replace("export function", "function")
    .replace(/: number/g, "").replace(/: boolean/g, "").replace(/: string/g, "");
const formatDurationMs = new Function(`${body}; return formatDurationMs;`)();

const durations = [0, 999, 1000, 59000, 60000, 61000, 599000, 3599999, 3600000, 3661000, 36000000,
    86399000, 86400000, 90061000, 2678442000, 864000000 + 3723000];
const out = [];
for (const ms of durations)
    for (const human of [false, true])
        for (const seconds of [true, false])
            out.push({ ms, human, seconds, expected: formatDurationMs(ms, human, seconds) });
console.log(JSON.stringify(out, null, 1));
