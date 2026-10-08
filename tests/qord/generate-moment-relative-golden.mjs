// Golden data for <t:...:R>: what moment.js (Discord's formatter) prints for
// each distance, with Math.round and with Math.floor (DontRoundMyTimestamps).
// Development aid only:
//   npm install moment@2.30.1 && node generate-moment-relative-golden.mjs > moment-relative-golden.json
import moment from "moment";
const deltas = [0, 10e3, 44e3, 44.6e3, 45e3, 89e3, 90e3, 2.4*6e4*60/60, 30*6e4, 44*6e4, 44.6*6e4, 45*6e4, 89*6e4, 90*6e4, 5*36e5,
  21.4*36e5, 21.6*36e5, 30*36e5, 36*36e5, 2.5*864e5, 25.4*864e5, 25.6*864e5, 40*864e5, 45*864e5, 100*864e5,
  319*864e5, 330*864e5, 400*864e5, 546*864e5, 548*864e5, 7.6*365.25*864e5];
const now = 1700000000000;
const out = [];
for (const floor of [false, true]) {
  moment.relativeTimeRounding(floor ? Math.floor : Math.round);
  for (const d of deltas) for (const sign of [1, -1]) {
    // The timestamp is fixed (whole seconds, like <t:...>); "now" moves
    const t = now;
    out.push({ floor, deltaMs: sign * d, text: moment(t).from(moment(t - sign * d)) });
  }
}
moment.relativeTimeRounding(Math.round);
console.log(JSON.stringify(out, null, 1));
