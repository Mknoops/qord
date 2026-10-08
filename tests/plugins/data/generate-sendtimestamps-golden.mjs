// Golden data for SendTimestamps: what upstream's parseTime() reads from
// `HH:MM` shortcuts, using V8's own Date parser.
//   node tests/plugins/data/generate-sendtimestamps-golden.mjs > tests/plugins/data/sendtimestamps-golden.json
// Recorded as seconds after local midnight of the day (null = Invalid Date),
// so the result does not depend on the machine's time zone or the date.
const inputs = ["12:00", "3:51", "17:59", "24:00", "12:00 AM", "0:13PM", "12:00 PM", "13:00 PM", "00:00",
    "24:30", "25:00", "9:60", "99:99", "1:00am", "11:59 pm", "0:00 AM", "12:30AM", "7:05 PM", "23:59",
    "00:59 AM", "9:61", "10:75", "24:01", "24:00 AM", "12:60 PM", "0:60", "9:60 PM", "13:00 AM", "0:13AM",
    "24:00 PM", "23:60", "01:05"];
const base = "Thu Oct 08 2026";
const midnight = new Date(base).getTime();
const out = inputs.map(input => {
    // upstream: time.slice(1, -1).replace(/(\d)(AM|PM)$/i, "$1 $2") on the `...` match
    const clean = input.replace(/(\d)(AM|PM)$/i, "$1 $2");
    const ms = new Date(`${base} ${clean}`).getTime();
    return { input, secondsAfterMidnight: isNaN(ms) ? null : Math.round((ms - midnight) / 1000) };
});
console.log(JSON.stringify(out, null, 1));
