// Golden data for ViewIcons: upstream's openImage() URL rewrite, run on the
// WHATWG URL API exactly as upstream does (src/plugins/viewIcons/index.tsx).
//   node tests/plugins/data/generate-viewicons-golden.mjs > tests/plugins/data/viewicons-golden.json
// The one Qord change: upstream treats URLs starting with "/" (Discord's
// bundled default avatars) as png; Qord's default avatars are the CDN's
// /embed/avatars/ images, so those count instead.
function openImage(url, format, imgSize) {
    const u = new URL(url, "https://discord.com/channels/@me");
    const isDefaultAsset = url.startsWith("/") || u.pathname.startsWith("/embed/avatars/");
    const fmt = isDefaultAsset
        ? "png"
        : u.searchParams.get("animated") === "true"
            ? "webp"
            : format;
    u.searchParams.set("size", imgSize);
    u.pathname = u.pathname.replace(/\.(png|jpe?g|webp)$/, `.${fmt}`);
    return u.toString();
}
const urls = [
    "https://cdn.discordapp.com/avatars/100000000000000002/0123456789abcdef0123456789abcdef.png?size=128",
    "https://cdn.discordapp.com/avatars/100000000000000002/a_0123456789abcdef0123456789abcdef.gif?size=128",
    "https://cdn.discordapp.com/avatars/100000000000000002/a_0123456789abcdef0123456789abcdef.webp?size=80&animated=true",
    "https://cdn.discordapp.com/avatars/100000000000000002/0123456789abcdef0123456789abcdef.webp",
    "https://cdn.discordapp.com/avatars/100000000000000002/0123456789abcdef0123456789abcdef.jpeg?size=64&size=32",
    "https://cdn.discordapp.com/embed/avatars/3.png?size=128",
    "https://cdn.discordapp.com/guilds/1/users/2/avatars/abcdef.png?size=128",
    "https://cdn.discordapp.com/icons/200000000000000001/fedcba9876543210fedcba9876543210.png?size=64",
    "https://cdn.discordapp.com/icons/200000000000000001/a_fedcba9876543210fedcba9876543210.gif?size=64",
    "https://cdn.discordapp.com/banners/200000000000000001/fedcba.png?size=480",
    "https://cdn.discordapp.com/channel-icons/300000000000000001/abc123.png?size=64",
    "https://cdn.discordapp.com/banners/100000000000000002/abc.jpg?keep=1&size=600",
];
const out = [];
for (const url of urls)
    for (const format of ["webp", "png", "jpg"])
        for (const size of ["128", "1024", "4096"])
            out.push({ url, format, size, expected: openImage(url, format, size) });
console.log(JSON.stringify(out, null, 1));
