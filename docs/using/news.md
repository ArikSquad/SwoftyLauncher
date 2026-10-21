---
title: News
description: The News page, the first page the launcher opens, with the Swofty Developments Discord and the latest posts and release notes.
---

# News

**News** is the first page the launcher opens. It's at the top of the sidebar.

## The Discord banner

The banner at the top invites you to the Swofty Developments Discord server, where you get help from the developers and hear about new releases and addons. Select **Join the Discord**, or anywhere on the banner, to open the invite in your browser.

## Posts

Below the banner is a feed of posts, newest first. The newest post is shown large, with its date, a summary and a button that opens it on this site. The rest are listed under **EARLIER**; select one to open it in your browser.

There are two kinds of posts:

| Tag | What it is |
|---|---|
| **News** | An announcement, such as a new release |
| **Release notes** | The notes for one launcher release |

## Where the posts come from

The launcher reads the feed from `https://f1.swofty.net/news.json` when it starts, and again when you open **News** after 30 minutes. **Refresh**, at the top right, reads it again now. The line under the **News** title says where the posts on screen came from:

| Line | Meaning |
|---|---|
| **From f1.swofty.net** | The launcher read the feed this session |
| **Checking for news…** | The launcher is reading the feed |
| **Offline · showing the news saved last time** | The feed could not be read; the posts are the last feed the launcher read and the ones this version came with |
| **Offline · showing the news that came with this version** | The feed could not be read and none was saved |

The launcher keeps the last feed it read, and pictures from the newest posts, in `cache/news` in its data folder. Each launcher release also carries its own copy of the feed. Offline, **News** shows those two together.

## For the site

The docs build writes `news.json` from the front matter of `docs/news/*.md` (posts) and `docs/releases/*.md` (release notes):

| Field | Used as |
|---|---|
| `title` | The post's title. A release's title, such as `1.3.0`, becomes "Swofty Launcher 1.3.0" |
| `description` | The summary |
| `date` | The date, as `YYYY-MM-DD`. Without it, the date the file was added to git |
| `image` | A picture shown above the newest post, as a URL or a path on this site |
| `draft: true` | Leaves the file out of the feed |

`node docs/scripts/news.mjs` prints the feed.
