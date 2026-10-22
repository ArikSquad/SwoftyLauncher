// Builds the launcher's News feed from docs/news/*.md and docs/releases/*.md.
//
//   node docs/scripts/news.mjs            print the feed
//   node docs/scripts/news.mjs <file>     write it to <file>
//
// The docs build writes it to news.json at the site root. apps/launcher/build.rs builds the same
// feed for the launcher's offline copy.
//
// Each post: id ("news/<slug>" or "release/<slug>"), kind ("news" or "release"), title, date
// (YYYY-MM-DD), summary (front matter description), image (URL or null) and url. News posts also
// carry body: the Markdown after the front matter and the leading "# " heading, which the
// launcher renders in full.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const docsDir = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const site = "https://f1.swofty.net";

function frontMatter(text) {
  const lines = text.split(/\r?\n/);
  if (lines[0]?.trim() !== "---") return {};
  const fm = {};
  for (const line of lines.slice(1)) {
    if (line.trim() === "---") break;
    const at = line.indexOf(":");
    if (at < 0) continue;
    let value = line.slice(at + 1).trim();
    const quoted = /^(["']).*\1$/.test(value);
    if (quoted) value = value.slice(1, -1).replaceAll('\\"', '"');
    fm[line.slice(0, at).trim()] = value;
  }
  return fm;
}

function body(text) {
  let lines = text.split(/\r?\n/);
  if (lines[0]?.trim() === "---") {
    const end = lines.slice(1).findIndex((l) => l.trim() === "---");
    lines = end < 0 ? [] : lines.slice(end + 2);
  }
  const start = lines.findIndex((l) => l.trim() !== "");
  lines = start < 0 ? [] : lines.slice(start);
  if (lines[0]?.startsWith("# ")) lines = lines.slice(1);
  return lines.join("\n").trim();
}

function gitDate(file) {
  try {
    const out = execFileSync("git", ["log", "--diff-filter=A", "--format=%cs", "--", file], {
      cwd: docsDir,
      encoding: "utf8",
    });
    return out.trim().split("\n").pop() ?? "";
  } catch {
    return "";
  }
}

function versionKey(slug) {
  return slug.split(".").map((n) => n.padStart(6, "0")).join(".");
}

function posts(dir, kind) {
  const full = path.join(docsDir, dir);
  if (!fs.existsSync(full)) return [];
  return fs
    .readdirSync(full)
    .filter((f) => f.endsWith(".md") && f !== "index.md")
    .flatMap((f) => {
      const slug = f.slice(0, -3);
      const text = fs.readFileSync(path.join(full, f), "utf8");
      const fm = frontMatter(text);
      if (fm.draft === "true") return [];
      const title = fm.title || slug;
      const image = fm.image ? (fm.image.startsWith("/") ? site + fm.image : fm.image) : null;
      return [
        {
          id: `${kind}/${slug}`,
          kind,
          title: kind === "release" && /^\d/.test(title) ? `Swofty Launcher ${title}` : title,
          date: fm.date || gitDate(path.join(dir, f)),
          summary: fm.description || "",
          image,
          url: `${site}/${dir}/${slug}`,
          ...(kind === "news" ? { body: body(text) } : {}),
        },
      ];
    });
}

export function newsFeed() {
  const all = [...posts("news", "news"), ...posts("releases", "release")];
  all.sort(
    (a, b) =>
      b.date.localeCompare(a.date) ||
      (a.kind === b.kind ? 0 : a.kind === "news" ? -1 : 1) ||
      versionKey(b.id.split("/")[1]).localeCompare(versionKey(a.id.split("/")[1])),
  );
  return { posts: all };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const json = `${JSON.stringify(newsFeed(), null, 2)}\n`;
  if (process.argv[2]) fs.writeFileSync(process.argv[2], json);
  else process.stdout.write(json);
}
