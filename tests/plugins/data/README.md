# Plugin test data

| File | Source | License |
|---|---|---|
| `clearurls-data.min.json` | https://github.com/ClearURLs/Rules, `data.min.json` at commit `11086f40512774dcadef54079f1ba023bfacf940` (2026-03-25), SHA256 `b328d2a5b95b1b23bac2c91342a8deee3f9a3826a5950bf9e28e332ffd94cd2f` | LGPL-3.0 |
| `clearurls-inputs.json` | Qord | GPL-3.0 |
| `clearurls-golden.json` | Output of upstream Equicord ClearURLs logic on the inputs above, produced by `generate-clearurls-golden.mjs` | GPL-3.0 |

`generate-clearurls-golden.mjs` is a development aid only. It runs Equicord's
ClearURLs code under Node so the native port can be compared against the
original. Neither Node nor the script is used by Qord at build or run time.
