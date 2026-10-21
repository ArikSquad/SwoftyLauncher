//! The News page: the Discord banner and the feed of posts and release notes.

use std::collections::HashMap;
use std::hash::{DefaultHasher, Hash, Hasher};
use std::path::Path;
use std::sync::Arc;
use std::time::{Duration, Instant};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::{Disableable, Icon, Sizable, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use serde::Deserialize;

use crate::pages::GUTTER;
use crate::shell::{Page, Shell};
use crate::theme::Palette;

pub const FEED_URL: &str = "https://f1.swofty.net/news.json";
const EMBEDDED: &str = include_str!(concat!(env!("OUT_DIR"), "/news.json"));
const CACHE_DIR: &str = "cache/news";
const FEED_FILE: &str = "feed.json";
const TIMEOUT: Duration = Duration::from_secs(10);
const STALE: Duration = Duration::from_secs(30 * 60);
const IMAGES: usize = 8;
const IMAGE_LIMIT: u64 = 8 * 1024 * 1024;

#[derive(Clone, Debug, PartialEq, Deserialize)]
pub struct Post {
    pub id: String,
    #[serde(default)]
    pub kind: String,
    pub title: String,
    #[serde(default)]
    pub date: String,
    #[serde(default)]
    pub summary: String,
    #[serde(default)]
    pub image: Option<String>,
    #[serde(default)]
    pub url: Option<String>,
}

impl Post {
    fn release(&self) -> bool {
        self.kind == "release"
    }
}

#[derive(Deserialize)]
struct Feed {
    posts: Vec<Post>,
}

pub fn parse(text: &str) -> Option<Vec<Post>> {
    serde_json::from_str::<Feed>(text).ok().map(|f| f.posts)
}

fn version(id: &str) -> Vec<u64> {
    let slug = id.rsplit('/').next().unwrap_or(id);
    slug.split('.').map(|n| n.parse().unwrap_or(0)).collect()
}

/// Every post of `base` and `newer` once, `newer`'s copy where both have it,
/// newest first.
pub fn merge(base: Vec<Post>, newer: Vec<Post>) -> Vec<Post> {
    let mut posts: Vec<Post> = base
        .into_iter()
        .filter(|b| newer.iter().all(|n| n.id != b.id))
        .collect();
    posts.extend(newer);
    posts.sort_by(|a, b| {
        b.date
            .cmp(&a.date)
            .then(a.release().cmp(&b.release()))
            .then_with(|| version(&b.id).cmp(&version(&a.id)))
    });
    posts
}

pub fn embedded() -> Vec<Post> {
    merge(parse(EMBEDDED).unwrap_or_default(), Vec::new())
}

pub fn format_date(date: &str) -> String {
    const MONTHS: [&str; 12] = [
        "January",
        "February",
        "March",
        "April",
        "May",
        "June",
        "July",
        "August",
        "September",
        "October",
        "November",
        "December",
    ];
    let mut parts = date.splitn(3, '-').map(|n| n.parse::<usize>().ok());
    match (
        parts.next().flatten(),
        parts.next().flatten(),
        parts.next().flatten(),
    ) {
        (Some(y), Some(m), Some(d)) if (1..=12).contains(&m) => format!("{d} {} {y}", MONTHS[m - 1]),
        _ => date.to_owned(),
    }
}

fn image_format(bytes: &[u8]) -> Option<ImageFormat> {
    match bytes {
        [0x89, b'P', b'N', b'G', ..] => Some(ImageFormat::Png),
        [0xff, 0xd8, ..] => Some(ImageFormat::Jpeg),
        [b'G', b'I', b'F', ..] => Some(ImageFormat::Gif),
        [b'R', b'I', b'F', b'F', _, _, _, _, b'W', b'E', b'B', b'P', ..] => Some(ImageFormat::Webp),
        _ => None,
    }
}

fn image(dir: &Path, url: &str) -> Option<Arc<Image>> {
    let mut hasher = DefaultHasher::new();
    url.hash(&mut hasher);
    let path = dir.join(format!("{:016x}", hasher.finish()));
    let bytes = match std::fs::read(&path) {
        Ok(bytes) => bytes,
        Err(_) => {
            let bytes = swofty_core::update::get(url, Some(TIMEOUT))
                .ok()?
                .with_config()
                .limit(IMAGE_LIMIT)
                .read_to_vec()
                .ok()?;
            std::fs::create_dir_all(dir).ok();
            std::fs::write(&path, &bytes).ok();
            bytes
        }
    };
    Some(Arc::new(Image::from_bytes(image_format(&bytes)?, bytes)))
}

struct Fetched {
    posts: Option<Vec<Post>>,
    images: Vec<(String, Arc<Image>)>,
    error: Option<String>,
}

fn fetch(root: &Path, shown: Vec<Post>) -> Fetched {
    let dir = root.join(CACHE_DIR);
    let remote = swofty_core::update::get_text(FEED_URL, TIMEOUT)
        .map_err(|e| e.to_string())
        .and_then(|text| {
            parse(&text)
                .map(|p| (text, p))
                .ok_or_else(|| format!("{FEED_URL}: not a news feed"))
        });
    let (posts, error) = match remote {
        Ok((text, posts)) => {
            std::fs::create_dir_all(&dir).ok();
            if let Err(e) = std::fs::write(dir.join(FEED_FILE), text) {
                eprintln!("swofty-launcher: could not save the news: {e}");
            }
            (Some(merge(embedded(), posts)), None)
        }
        Err(e) => (None, Some(e)),
    };
    let images = posts
        .as_ref()
        .unwrap_or(&shown)
        .iter()
        .take(IMAGES)
        .filter_map(|p| p.image.clone())
        .filter_map(|url| image(&dir, &url).map(|i| (url, i)))
        .collect();
    Fetched { posts, images, error }
}

pub struct News {
    pub posts: Vec<Post>,
    images: HashMap<String, Arc<Image>>,
    checked: Option<Instant>,
    live: bool,
    saved: bool,
    fetching: bool,
}

impl News {
    pub fn load(root: &Path) -> Self {
        let cached = std::fs::read_to_string(root.join(CACHE_DIR).join(FEED_FILE))
            .ok()
            .and_then(|t| parse(&t))
            .unwrap_or_default();
        Self {
            saved: !cached.is_empty(),
            posts: merge(embedded(), cached),
            images: HashMap::new(),
            checked: None,
            live: false,
            fetching: false,
        }
    }

    fn stale(&self) -> bool {
        !self.fetching && self.checked.is_none_or(|t| t.elapsed() >= STALE)
    }
}

fn kind_tag(post: &Post, p: Palette) -> Div {
    let release = post.release();
    div()
        .px(px(7.))
        .h(px(20.))
        .flex()
        .items_center()
        .rounded(px(4.))
        .border_1()
        .border_color(if release { p.border } else { p.accent })
        .text_size(px(11.))
        .font_weight(FontWeight::SEMIBOLD)
        .text_color(if release { p.text_muted } else { p.accent })
        .whitespace_nowrap()
        .child(if release { "Release notes" } else { "News" })
}

fn open(url: Option<String>) -> impl Fn(&ClickEvent, &mut Window, &mut App) + 'static {
    move |_, _, cx| {
        if let Some(url) = &url {
            cx.open_url(url);
        }
    }
}

impl Shell {
    pub(crate) fn fetch_news(&mut self, cx: &mut Context<Self>) {
        if self.news.fetching {
            return;
        }
        self.news.fetching = true;
        let root = self.store.root.clone();
        let shown = self.news.posts.clone();
        cx.spawn(async move |this, cx| {
            let fetched = cx
                .background_executor()
                .spawn(async move { fetch(&root, shown) })
                .await;
            this.update(cx, |s, cx| {
                let news = &mut s.news;
                news.fetching = false;
                news.checked = Some(Instant::now());
                if let Some(e) = &fetched.error {
                    eprintln!("swofty-launcher: could not check for news: {e}");
                }
                if let Some(posts) = fetched.posts {
                    news.posts = posts;
                    news.live = true;
                }
                news.images.extend(fetched.images);
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    pub(crate) fn news_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        if self.news.stale() {
            self.fetch_news(cx);
        }
        let subtitle = if self.news.fetching {
            "Checking for news…"
        } else if self.news.live {
            "From f1.swofty.net"
        } else if self.news.saved {
            "Offline · showing the news saved last time"
        } else {
            "Offline · showing the news that came with this version"
        };
        let refresh = Button::new("news-refresh")
            .ghost()
            .small()
            .icon(IconName::RefreshCw)
            .label("Refresh")
            .disabled(self.news.fetching)
            .on_click(cx.listener(|this, _, _, cx| this.fetch_news(cx)));
        let mut posts = self.news.posts.iter();
        let latest = posts.next().map(|post| self.latest_post(post, p));
        let earlier: Vec<AnyElement> = posts
            .enumerate()
            .map(|(i, post)| earlier_post(post, i == 0, p))
            .collect();
        v_flex()
            .size_full()
            .child(self.header(Page::News.label(), subtitle.to_owned(), refresh, p))
            .child(
                v_flex()
                    .id("news")
                    .flex_1()
                    .min_h_0()
                    .overflow_y_scroll()
                    .px(px(GUTTER))
                    .pt(px(4.))
                    .pb(px(28.))
                    .gap(px(20.))
                    .child(crate::discord::banner())
                    .children(latest)
                    .when(!earlier.is_empty(), |d| {
                        d.child(
                            v_flex()
                                .flex_shrink_0()
                                .gap(px(8.))
                                .child(crate::pages::label("EARLIER", p).px(px(2.)))
                                .child(
                                    v_flex()
                                        .rounded(px(8.))
                                        .border_1()
                                        .border_color(p.border)
                                        .overflow_hidden()
                                        .children(earlier),
                                ),
                        )
                    }),
            )
            .into_any_element()
    }

    fn latest_post(&self, post: &Post, p: Palette) -> AnyElement {
        let image = post.image.as_ref().and_then(|u| self.news.images.get(u)).cloned();
        v_flex()
            .flex_shrink_0()
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .overflow_hidden()
            .children(image.map(|i| {
                div()
                    .w_full()
                    .h(px(220.))
                    .overflow_hidden()
                    .child(img(i).size_full().object_fit(ObjectFit::Cover))
            }))
            .child(
                v_flex()
                    .p(px(20.))
                    .gap(px(10.))
                    .child(
                        h_flex()
                            .gap(px(10.))
                            .items_center()
                            .child(kind_tag(post, p))
                            .child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_placeholder)
                                    .child(format_date(&post.date)),
                            ),
                    )
                    .child(
                        div()
                            .text_size(px(20.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child(post.title.clone()),
                    )
                    .when(!post.summary.is_empty(), |d| {
                        d.child(
                            div()
                                .text_size(px(14.))
                                .line_height(px(21.))
                                .text_color(p.text_muted)
                                .child(post.summary.clone()),
                        )
                    })
                    .children(post.url.clone().map(|url| {
                        h_flex().pt(px(4.)).child(
                            Button::new("news-latest-open")
                                .primary()
                                .icon(IconName::ExternalLink)
                                .label(if post.release() {
                                    "Read the release notes"
                                } else {
                                    "Read more"
                                })
                                .on_click(open(Some(url))),
                        )
                    })),
            )
            .into_any_element()
    }
}

fn earlier_post(post: &Post, first: bool, p: Palette) -> AnyElement {
    h_flex()
        .id(SharedString::from(format!("news-{}", post.id)))
        .flex_shrink_0()
        .px(px(16.))
        .py(px(12.))
        .gap(px(16.))
        .items_start()
        .when(!first, |d| d.border_t_1().border_color(p.border_variant))
        .when(post.url.is_some(), |d| {
            d.cursor_pointer()
                .hover(move |s| s.bg(p.hover))
                .on_click(open(post.url.clone()))
        })
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .gap(px(3.))
                .child(
                    h_flex()
                        .gap(px(8.))
                        .items_center()
                        .child(
                            div()
                                .text_size(px(14.))
                                .font_weight(FontWeight::MEDIUM)
                                .child(post.title.clone()),
                        )
                        .when(!post.release(), |d| d.child(kind_tag(post, p))),
                )
                .when(!post.summary.is_empty(), |d| {
                    d.child(
                        div()
                            .text_size(px(13.))
                            .line_height(px(19.))
                            .text_color(p.text_muted)
                            .line_clamp(2)
                            .text_ellipsis()
                            .child(post.summary.clone()),
                    )
                }),
        )
        .child(
            h_flex()
                .flex_shrink_0()
                .gap(px(8.))
                .items_center()
                .text_size(px(12.))
                .text_color(p.text_placeholder)
                .child(format_date(&post.date))
                .when(post.url.is_some(), |d| {
                    d.child(Icon::new(IconName::ExternalLink).size(px(13.)))
                }),
        )
        .into_any_element()
}

#[cfg(test)]
mod tests {
    use super::{Post, embedded, format_date, image_format, merge, parse};

    fn post(id: &str, date: &str) -> Post {
        Post {
            id: id.into(),
            kind: id.split('/').next().unwrap_or_default().into(),
            title: id.into(),
            date: date.into(),
            summary: String::new(),
            image: None,
            url: None,
        }
    }

    #[test]
    fn embeds_the_release_notes_newest_first() {
        let posts = embedded();
        assert!(posts.iter().any(|p| p.id == "news/1.3.0"));
        assert!(posts.windows(2).all(|w| w[0].date >= w[1].date));
        let notes = posts.iter().find(|p| p.id == "release/1.2.5").unwrap();
        assert_eq!(notes.title, "Swofty Launcher 1.2.5");
        assert_eq!(notes.url.as_deref(), Some("https://f1.swofty.net/releases/1.2.5"));
        assert!(!notes.summary.is_empty() && !notes.date.is_empty());
    }

    #[test]
    fn merges_by_id_and_sorts_by_date_then_version() {
        let base = vec![
            post("release/1.2.9", "2026-10-21"),
            post("release/1.2.10", "2026-10-21"),
        ];
        let mut newer = vec![
            post("release/1.2.9", "2026-10-21"),
            post("news/hello", "2026-10-21"),
        ];
        newer[0].summary = "fresh".into();
        let merged = merge(base, newer);
        let ids: Vec<&str> = merged.iter().map(|p| p.id.as_str()).collect();
        assert_eq!(ids, ["news/hello", "release/1.2.10", "release/1.2.9"]);
        assert_eq!(merged[2].summary, "fresh");
    }

    #[test]
    fn reads_a_feed_and_ignores_unknown_fields() {
        let posts = parse(r#"{"posts":[{"id":"news/a","title":"A","extra":1}],"version":2}"#).unwrap();
        assert_eq!(posts[0].title, "A");
        assert!(parse("<html>").is_none());
    }

    #[test]
    fn formats_dates() {
        assert_eq!(format_date("2026-10-22"), "22 October 2026");
        assert_eq!(format_date("soon"), "soon");
    }

    #[test]
    fn knows_image_formats() {
        assert!(image_format(b"\x89PNG\r\n").is_some());
        assert!(image_format(b"<svg").is_none());
    }
}
