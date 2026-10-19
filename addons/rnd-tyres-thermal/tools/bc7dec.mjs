import { readFileSync, writeFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
const src = readFileSync(join(here, '..', '..', '..', 'tools', 'swofty-ui', 'static', 'bc7.js'), 'utf8');
const decodeBC7 = new Function(src + '\nreturn decodeBC7;')();
const [inp, w, h, out] = process.argv.slice(2);
const rgba = decodeBC7(new Uint8Array(readFileSync(inp)), +w, +h);
writeFileSync(out, Buffer.from(rgba.buffer, rgba.byteOffset, rgba.byteLength));
