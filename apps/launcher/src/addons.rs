//! The Addons page: install cards, filters, the list in load order and the
//! selected addon's details and conflicts.

use std::collections::{BTreeSet, HashMap, HashSet};
use std::path::PathBuf;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::input::{Input, InputEvent, InputState};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::conflicts::{self, Pair, Report};
use swofty_core::import::{self, Route};
use swofty_core::manifest::{Category, Kind, MANIFEST_FILE};
use swofty_core::profile::VANILLA;
use swofty_core::updater::Part;

use crate::library::{AddonItem, Changes, Source, plural};
use crate::pages::{GUTTER, addon_icon, badge, chevron, fact, label, logo, tag};
use crate::shell::{Page, Shell};
use crate::theme::{Palette, palette};

const FADE: Duration = Duration::from_secs(4);
const FILTERS_W: f32 = 226.;
const DETAIL_W: f32 = 380.;
const SPLIT_AT: f32 = 1000.;
const ITEMS_SHOWN: usize = 40;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
enum Type {
    Database,
    Archives,
    Text,
    Files,
    Code,
}

impl Type {
    const ALL: [Type; 5] = [
        Self::Database,
        Self::Archives,
        Self::Text,
        Self::Files,
        Self::Code,
    ];

    fn of(kind: Kind) -> Self {
        match kind {
            Kind::Database => Self::Database,
            Kind::Erp => Self::Archives,
            Kind::Ui => Self::Text,
            Kind::Files => Self::Files,
            Kind::Native => Self::Code,
        }
    }

    fn label(self) -> &'static str {
        match self {
            Self::Database => "Database",
            Self::Archives => "Archives",
            Self::Text => "Text & UI",
            Self::Files => "Files",
            Self::Code => "Code",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
enum State {
    On,
    Off,
    Conflicts,
    Update,
}

impl State {
    const ALL: [State; 4] = [Self::On, Self::Off, Self::Conflicts, Self::Update];

    fn label(self) -> &'static str {
        match self {
            Self::On => "On",
            Self::Off => "Off",
            Self::Conflicts => "Has conflicts",
            Self::Update => "Update available",
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Group {
    Category,
    Type,
    Source,
    State,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord, Hash)]
enum Facet {
    Category(Option<Category>),
    Type(Type),
    Source(Source),
    State(State),
}

impl Facet {
    fn group(self) -> Group {
        match self {
            Self::Category(_) => Group::Category,
            Self::Type(_) => Group::Type,
            Self::Source(_) => Group::Source,
            Self::State(_) => Group::State,
        }
    }

    fn label(self) -> &'static str {
        match self {
            Self::Category(Some(c)) => c.label(),
            Self::Category(None) => "Uncategorised",
            Self::Type(t) => t.label(),
            Self::Source(s) => s.label(),
            Self::State(s) => s.label(),
        }
    }
}

#[derive(Clone)]
struct DraggedAddon {
    id: String,
    name: SharedString,
}

impl Render for DraggedAddon {
    fn render(&mut self, _: &mut Window, cx: &mut Context<Self>) -> impl IntoElement {
        let p = palette(cx);
        h_flex()
            .px(px(10.))
            .py(px(6.))
            .gap(px(6.))
            .rounded(px(6.))
            .bg(p.elevated)
            .border_1()
            .border_color(p.accent)
            .text_size(px(13.))
            .text_color(p.text)
            .child(
                Icon::new(IconName::GripVertical)
                    .size(px(13.))
                    .text_color(p.text_muted),
            )
            .child(self.name.clone())
    }
}

pub(crate) struct AddonsPage {
    search: Entity<InputState>,
    window: AnyWindowHandle,
    facets: BTreeSet<Facet>,
    scroll: ScrollHandle,
    seen: Option<Instant>,
    scroll_pending: bool,
    animating: bool,
    report: Option<Report>,
    read_at: Option<Instant>,
    checking: bool,
    open_pairs: HashSet<(String, String)>,
    changes: HashMap<String, Changes>,
    _search: Subscription,
}

impl AddonsPage {
    pub(crate) fn new(window: &mut Window, cx: &mut Context<Shell>) -> Self {
        let search = cx.new(|cx| InputState::new(window, cx).placeholder("Search addons"));
        let changed = cx.subscribe(&search, |_, _, e: &InputEvent, cx| {
            if matches!(e, InputEvent::Change) {
                cx.notify();
            }
        });
        Self {
            search,
            window: window.window_handle(),
            facets: BTreeSet::new(),
            scroll: ScrollHandle::new(),
            seen: None,
            scroll_pending: false,
            animating: false,
            report: None,
            read_at: None,
            checking: false,
            open_pairs: HashSet::new(),
            changes: HashMap::new(),
            _search: changed,
        }
    }

    fn clear_search(&self, cx: &mut App) {
        let (input, window) = (self.search.clone(), self.window);
        cx.defer(move |cx| {
            window
                .update(cx, |_, window, cx| {
                    input.update(cx, |s, cx| s.set_value("", window, cx))
                })
                .ok();
        });
    }
}

fn matches_query(m: &AddonItem, query: &str) -> bool {
    if query.is_empty() {
        return true;
    }
    let man = &m.manifest;
    [
        Some(&man.name),
        Some(&man.id),
        Some(&man.author),
        man.description.as_ref(),
    ]
    .into_iter()
    .flatten()
    .any(|s| s.to_lowercase().contains(query))
}

fn pair_counts(pair: &Pair) -> String {
    let strings = pair.items.iter().filter(|i| i.starts_with("text ")).count();
    let parts: Vec<String> = [
        (pair.database, "database value", "database values"),
        (pair.resources, "archive resource", "archive resources"),
        (pair.files, "file", "files"),
        (strings, "text string", "text strings"),
    ]
    .into_iter()
    .filter(|(n, ..)| *n > 0)
    .map(|(n, one, many)| plural(n, one, many))
    .collect();
    match parts.as_slice() {
        [] => plural(pair.items.len(), "item", "items"),
        [one] => one.clone(),
        [rest @ .., last] => format!("{} and {last}", rest.join(", ")),
    }
}

fn ago(seconds: f64) -> String {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0., |d| d.as_secs_f64());
    let mins = ((now - seconds).max(0.) / 60.) as u64;
    match mins {
        0 => "just now".to_owned(),
        1..60 => format!("{} ago", plural(mins as usize, "minute", "minutes")),
        60..1440 => format!("{} ago", plural((mins / 60) as usize, "hour", "hours")),
        _ => format!("{} ago", plural((mins / 1440) as usize, "day", "days")),
    }
}

fn checkbox(on: bool, p: Palette) -> Div {
    div()
        .size(px(14.))
        .flex_shrink_0()
        .rounded(px(3.))
        .border_1()
        .flex()
        .items_center()
        .justify_center()
        .map(|d| {
            if on {
                d.bg(p.accent)
                    .border_color(p.accent)
                    .child(Icon::new(IconName::Check).size(px(11.)).text_color(p.on_accent))
            } else {
                d.border_color(p.border)
            }
        })
}

fn pill(icon: IconName, text: impl Into<SharedString>, color: Hsla) -> Div {
    h_flex()
        .h(px(20.))
        .px(px(6.))
        .gap(px(4.))
        .flex_shrink_0()
        .rounded(px(4.))
        .bg(color.opacity(0.14))
        .text_color(color)
        .text_size(px(11.))
        .font_weight(FontWeight::MEDIUM)
        .whitespace_nowrap()
        .child(Icon::new(icon).size(px(11.)))
        .child(text.into())
}

impl Shell {
    fn addon_on(&self, m: &AddonItem) -> bool {
        !self.vanilla() && (m.enabled || m.locked)
    }

    fn update_ready(&self, m: &AddonItem) -> bool {
        m.manifest.id == swofty_core::managed::ID
            && self
                .updates
                .pending()
                .iter()
                .any(|(part, _)| *part == Part::Addon)
    }

    fn pairs_of<'a>(&'a self, id: &'a str) -> impl Iterator<Item = &'a Pair> + 'a {
        self.addons
            .report
            .iter()
            .flat_map(|r| r.pairs.iter())
            .filter(move |p| p.winner == id || p.loser == id)
    }

    fn changed_since(&self, id: &str, built_at: f64) -> bool {
        std::fs::metadata(self.store.addons_dir().join(id).join(MANIFEST_FILE))
            .and_then(|m| m.modified())
            .ok()
            .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
            .is_some_and(|t| t.as_secs_f64() > built_at)
    }

    fn addon_name(&self, id: &str) -> String {
        self.library
            .addons
            .iter()
            .find(|a| a.manifest.id == id)
            .map_or_else(|| id.to_owned(), |a| a.manifest.name.clone())
    }

    fn matches_facet(&self, m: &AddonItem, f: Facet) -> bool {
        match f {
            Facet::Category(c) => m.manifest.category == c,
            Facet::Type(t) => m.manifest.kind.iter().any(|k| Type::of(*k) == t),
            Facet::Source(s) => m.source == s,
            Facet::State(State::On) => self.addon_on(m),
            Facet::State(State::Off) => !self.addon_on(m),
            Facet::State(State::Conflicts) => self.pairs_of(&m.manifest.id).next().is_some(),
            Facet::State(State::Update) => self.update_ready(m),
        }
    }

    fn passes(&self, m: &AddonItem, query: &str, except: Option<Group>) -> bool {
        let groups = [Group::Category, Group::Type, Group::Source, Group::State];
        matches_query(m, query)
            && groups.into_iter().filter(|g| Some(*g) != except).all(|g| {
                let mut chosen = self.addons.facets.iter().filter(|f| f.group() == g).peekable();
                chosen.peek().is_none() || chosen.any(|f| self.matches_facet(m, *f))
            })
    }

    /// The profile's enabled addons in load order, then the rest; and how many
    /// are in the load order.
    fn ordered(&self) -> (Vec<&AddonItem>, usize) {
        let find = |id: &str| self.library.listed().find(|a| a.manifest.id == id);
        if self.vanilla() {
            return (self.library.listed().collect(), 0);
        }
        let mut out: Vec<&AddonItem> = self.library.profile.enabled().filter_map(find).collect();
        let loaded = out.len();
        for m in self.library.listed() {
            if !out.iter().any(|o| o.manifest.id == m.manifest.id) {
                out.push(m);
            }
        }
        (out, loaded)
    }

    fn sync_addons(&mut self) {
        let at = self.library.loaded;
        if self.addons.read_at == Some(at) {
            return;
        }
        self.addons.read_at = Some(at);
        self.addons.changes.clear();
        let profile = self.library.profile.name.clone();
        let last = (!self.vanilla())
            .then(|| conflicts::last(&self.store, &profile))
            .flatten();
        let checked = self.addons.report.take().filter(|r| r.profile == profile);
        self.addons.report = match (last, checked) {
            (Some(l), Some(c)) => Some(if c.built_at > l.built_at { c } else { l }),
            (l, c) => l.or(c),
        };
    }

    fn sync_highlight(&mut self, query: &mut String, cx: &mut Context<Self>) -> Option<(String, f32)> {
        let (id, at) = self.highlight.clone()?;
        let t = at.elapsed();
        if t >= FADE {
            self.highlight = None;
            return None;
        }
        if self.addons.seen != Some(at) {
            let item = self.library.listed().find(|a| a.manifest.id == id);
            if let Some(hidden) = item.map(|m| !self.passes(m, query, None)) {
                self.addons.seen = Some(at);
                self.addons.scroll_pending = true;
                self.open_addon = Some(id.clone());
                if hidden {
                    self.addons.facets.clear();
                    if !query.is_empty() {
                        query.clear();
                        self.addons.clear_search(cx);
                    }
                }
            }
            self.animate_highlight(cx);
        }
        Some((id, 1. - t.as_secs_f32() / FADE.as_secs_f32()))
    }

    fn animate_highlight(&mut self, cx: &mut Context<Self>) {
        if self.addons.animating {
            return;
        }
        self.addons.animating = true;
        cx.spawn(async move |this, cx| {
            loop {
                cx.background_executor().timer(Duration::from_millis(33)).await;
                let done = this
                    .update(cx, |s, cx| {
                        let done = s.highlight.as_ref().is_none_or(|(_, at)| at.elapsed() >= FADE);
                        if done {
                            s.highlight = None;
                            s.addons.animating = false;
                        }
                        cx.notify();
                        done
                    })
                    .unwrap_or(true);
                if done {
                    break;
                }
            }
        })
        .detach();
    }

    fn pick_addons(&mut self, folder: bool, window: &mut Window, cx: &mut Context<Self>) {
        let rx = cx.prompt_for_paths(PathPromptOptions {
            files: !folder,
            directories: folder,
            multiple: true,
            prompt: Some("Install".into()),
        });
        cx.spawn_in(window, async move |this, cx| {
            if let Ok(Ok(Some(paths))) = rx.await
                && !paths.is_empty()
            {
                this.update_in(cx, |s, window, cx| s.install_picked(paths, window, cx))
                    .ok();
            }
        })
        .detach();
    }

    fn install_picked(&mut self, paths: Vec<PathBuf>, window: &mut Window, cx: &mut Context<Self>) {
        let registry = self.library.registry.clone();
        let (addons, other): (Vec<PathBuf>, Vec<PathBuf>) = paths
            .into_iter()
            .partition(|p| import::route(&registry, p) == Some(Route::Addon));
        if !other.is_empty() {
            self.drop_paths(other, window, cx);
        }
        if addons.is_empty() {
            return;
        }
        let store = self.store.clone();
        cx.spawn_in(window, async move |this, cx| {
            let results = cx
                .background_executor()
                .spawn(async move {
                    addons
                        .iter()
                        .map(|path| {
                            store.install_addon(path).map_err(|e| {
                                let name = path.file_name().map_or_else(
                                    || path.display().to_string(),
                                    |n| n.to_string_lossy().into_owned(),
                                );
                                format!("{name}: {e}")
                            })
                        })
                        .collect::<Vec<_>>()
                })
                .await;
            this.update_in(cx, |s, window, cx| {
                let mut last = None;
                for r in results {
                    match r {
                        Ok(m) => {
                            let text = format!("Installed {} {}", m.name, m.version);
                            window.push_notification(Notification::success(text), cx);
                            last = Some(m.id);
                        }
                        Err(e) => window.push_notification(Notification::error(e), cx),
                    }
                }
                s.reload(cx);
                if let Some(id) = last {
                    s.show_addon(&id, cx);
                }
            })
            .ok();
        })
        .detach();
    }

    fn move_onto(&mut self, id: &str, target: &str, window: &mut Window, cx: &mut Context<Self>) {
        let addons = &self.library.profile.addons;
        let at = |x: &str| addons.iter().position(|a| a.id == x);
        let (Some(a), Some(b)) = (at(id), at(target)) else {
            return;
        };
        if a != b {
            let (id, delta) = (id.to_owned(), b as isize - a as isize);
            self.update_profile(window, cx, |p| p.move_addon(&id, delta));
        }
    }

    fn move_step(&mut self, id: &str, up: bool, window: &mut Window, cx: &mut Context<Self>) {
        let (order, loaded) = self.ordered();
        let order: Vec<String> = order[..loaded].iter().map(|m| m.manifest.id.clone()).collect();
        let Some(i) = order.iter().position(|x| x == id) else {
            return;
        };
        let j = if up { i.checked_sub(1) } else { Some(i + 1) };
        if let Some(target) = j.and_then(|j| order.get(j)) {
            self.move_onto(id, target, window, cx);
        }
    }

    fn movable(&self, m: &AddonItem) -> bool {
        !self.vanilla() && m.enabled && !m.manifest.is_builtin()
    }

    /// Turns off every addon except those that are always on and those whose
    /// turning off warns about saves, which keep their own switch.
    fn turn_off_all(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let registry = self.library.registry.clone();
        let profile = &self.library.profile;
        let (mut off, mut kept) = (Vec::new(), Vec::new());
        for m in self.library.listed().filter(|m| m.enabled && !m.locked) {
            match registry.turn_off_warning(profile, &m.manifest.id) {
                Some(_) => kept.push(m.manifest.name.clone()),
                None => off.push(m.manifest.id.clone()),
            }
        }
        self.update_profile(window, cx, |p| {
            off.iter().try_for_each(|id| registry.set_enabled(p, id, false))
        });
        if !kept.is_empty() {
            window.push_notification(
                Notification::info(format!(
                    "Left on: {}. Turning it off warns about your saves, so use its own switch.",
                    kept.join(", ")
                )),
                cx,
            );
        }
    }

    fn check_conflicts(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.addons.checking || self.vanilla() {
            return;
        }
        self.addons.checking = true;
        let (store, install) = (self.store.clone(), self.install.clone());
        let profile = self.library.profile.name.clone();
        cx.spawn_in(window, async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move {
                    let game = crate::sections::game(&store, install.as_ref()).ok_or_else(|| {
                        "Locate F1 25 first. Checking conflicts reads its files.".to_owned()
                    })?;
                    conflicts::check(&store, &profile, &game).map_err(|e| e.to_string())
                })
                .await;
            this.update_in(cx, |s, window, cx| {
                s.addons.checking = false;
                match result {
                    Ok(report) => {
                        let text = match report.pairs.len() {
                            0 => "No conflicts between addons".to_owned(),
                            n => format!("Found {}", plural(n, "conflict", "conflicts")),
                        };
                        window.push_notification(Notification::success(text), cx);
                        if report.profile == s.library.profile.name {
                            s.addons.report = Some(report);
                        }
                    }
                    Err(e) => window
                        .push_notification(Notification::error(e).title("Could not check conflicts"), cx),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    pub(crate) fn addons_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        self.sync_addons();
        let mut query = self.addons.search.read(cx).value().trim().to_lowercase();
        let highlight = self.sync_highlight(&mut query, cx);
        let installed = plural(
            self.library.listed().count(),
            "addon installed",
            "addons installed",
        );
        let subtitle = if self.vanilla() {
            format!("{installed} · {VANILLA} plays the game without them")
        } else {
            let on = self.library.listed().filter(|m| self.addon_on(m)).count();
            format!("{installed} · {on} on in {}", self.library.profile.name)
        };
        let actions = (!self.vanilla()).then(|| {
            Button::new("turn-off-all")
                .outline()
                .icon(IconName::PowerOff)
                .label("Turn off all")
                .on_click(cx.listener(|this, _, window, cx| this.turn_off_all(window, cx)))
        });
        let pending = (!self.vanilla() && !self.library.rebuild.is_empty()).then(|| {
            h_flex()
                .px(px(GUTTER))
                .pb(px(12.))
                .gap(px(8.))
                .flex_shrink_0()
                .text_size(px(13.))
                .text_color(p.text_muted)
                .child(
                    Icon::new(IconName::CircleAlert)
                        .size(px(14.))
                        .text_color(p.warning),
                )
                .child(format!(
                    "{} changed since the last build · the next launch rebuilds {}",
                    plural(
                        self.library.rebuild.len(),
                        "database setting",
                        "database settings"
                    ),
                    self.library.profile.name
                ))
        });
        let hidden = self.library.addons.len() - self.library.listed().count();
        let tracks = self.library.tracks_page.filter(|_| hidden > 0).map(|i| {
            h_flex()
                .px(px(GUTTER))
                .pb(px(12.))
                .gap(px(8.))
                .flex_shrink_0()
                .items_center()
                .text_size(px(13.))
                .text_color(p.text_muted)
                .child(Icon::new(IconName::Map).size(px(14.)))
                .child(format!(
                    "{} {} on the Tracks page",
                    plural(hidden, "track addon", "track addons"),
                    if hidden == 1 { "is" } else { "are" }
                ))
                .child(
                    Button::new("open-tracks")
                        .ghost()
                        .small()
                        .label("Open Tracks")
                        .on_click(cx.listener(move |this, _, _, cx| this.open_page(Page::Section(i), cx))),
                )
        });
        let split = self.content_width >= SPLIT_AT;
        let selected = self
            .open_addon
            .clone()
            .filter(|id| self.library.listed().any(|a| &a.manifest.id == id));
        let list = self.addon_list(&query, highlight.as_ref(), split, p, cx);
        let detail = selected
            .filter(|_| split)
            .map(|id| self.addon_detail(&id, false, p, cx));
        v_flex()
            .size_full()
            .child(self.header(Page::Addons.label(), subtitle, div().children(actions), p))
            .children(pending)
            .children(tracks)
            .child(self.install_cards(p, cx))
            .child(
                h_flex()
                    .flex_1()
                    .min_h_0()
                    .items_start()
                    .px(px(GUTTER))
                    .pb(px(20.))
                    .gap(px(20.))
                    .child(self.filters(&query, p, cx))
                    .child(list)
                    .children(detail),
            )
            .into_any_element()
    }

    fn install_cards(&self, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        let roomy = self.content_width >= 900.;
        let card =
            |id: &'static str, icon: IconName, color: Hsla, title: &'static str, body: &'static str| {
                let text = v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(2.))
                    .child(
                        div()
                            .text_size(px(14.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .text_color(p.text)
                            .child(title),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .line_height(px(18.))
                            .text_color(p.text_muted)
                            .child(body),
                    );
                h_flex()
                    .id(id)
                    .flex_1()
                    .min_w_0()
                    .min_h(px(78.))
                    .px(px(16.))
                    .py(px(12.))
                    .gap(px(14.))
                    .rounded(px(8.))
                    .border_1()
                    .bg(p.surface)
                    .when(roomy, |d| {
                        d.child(
                            div()
                                .size(px(40.))
                                .flex_shrink_0()
                                .rounded(px(8.))
                                .bg(color.opacity(0.14))
                                .flex()
                                .items_center()
                                .justify_center()
                                .child(Icon::new(icon).size(px(19.)).text_color(color)),
                        )
                    })
                    .child(text)
            };
        let folder = div()
            .id("install-folder")
            .flex_shrink_0()
            .text_size(px(12.))
            .text_color(p.text_muted)
            .cursor_pointer()
            .hover(move |s| s.text_color(p.text))
            .child("Choose a folder…")
            .on_click(cx.listener(|this, _, window, cx| {
                cx.stop_propagation();
                this.pick_addons(true, window, cx)
            }));
        let addon = card(
            "install-addon",
            IconName::Upload,
            p.accent,
            "Installing a Swofty Addon?",
            "Drag and drop it here or click to choose a file",
        )
        .border_dashed()
        .border_color(p.border)
        .cursor_pointer()
        .hover(move |s| s.border_color(p.accent).bg(p.hover))
        .child(folder)
        .on_click(cx.listener(|this, _, window, cx| this.pick_addons(false, window, cx)));
        let importer = card(
            "install-mod",
            IconName::FolderInput,
            p.success,
            "Installing a mod?",
            "Mods convert into Swofty addons in the Mod Importer.",
        )
        .border_color(p.border_variant)
        .child(
            Button::new("go-importer")
                .outline()
                .small()
                .label(if roomy {
                    "Go to Mod Importer"
                } else {
                    "Mod Importer"
                })
                .on_click(cx.listener(|this, _, _, cx| this.open_page(Page::Importer, cx))),
        );
        h_flex()
            .px(px(GUTTER))
            .pb(px(18.))
            .gap(px(14.))
            .flex_shrink_0()
            .child(addon)
            .child(importer)
    }

    fn filters(&self, query: &str, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        let items: Vec<&AddonItem> = self.library.listed().collect();
        let active = !self.addons.facets.is_empty() || !query.is_empty();
        let group = |title: &'static str, facets: Vec<Facet>, cx: &mut Context<Self>| {
            let rows: Vec<AnyElement> = facets
                .into_iter()
                .filter(|f| {
                    f.group() != Group::Category
                        || self.addons.facets.contains(f)
                        || items.iter().any(|m| self.matches_facet(m, *f))
                })
                .map(|f| {
                    let n = items
                        .iter()
                        .filter(|m| self.passes(m, query, Some(f.group())) && self.matches_facet(m, f))
                        .count();
                    self.filter_row(f, n, p, cx)
                })
                .collect();
            v_flex()
                .gap(px(1.))
                .child(div().pb(px(4.)).child(label(title, p)))
                .children(rows)
        };
        let categories = Category::ALL
            .into_iter()
            .map(|c| Facet::Category(Some(c)))
            .chain([Facet::Category(None)])
            .collect();
        let types = Type::ALL.into_iter().map(Facet::Type).collect();
        let sources = Source::ALL.into_iter().map(Facet::Source).collect();
        let states = State::ALL.into_iter().map(Facet::State).collect();
        let clear = active.then(|| {
            div()
                .id("filters-clear")
                .text_size(px(12.))
                .text_color(p.accent)
                .cursor_pointer()
                .hover(move |s| s.text_color(p.accent_hover))
                .child("Clear")
                .on_click(cx.listener(|this, _, _, cx| {
                    this.addons.facets.clear();
                    this.addons.clear_search(cx);
                    cx.notify();
                }))
        });
        v_flex()
            .w(px(FILTERS_W))
            .flex_shrink_0()
            .h_full()
            .gap(px(14.))
            .child(
                Input::new(&self.addons.search).cleanable(true).prefix(
                    Icon::new(IconName::Search)
                        .size(px(14.))
                        .text_color(p.text_placeholder),
                ),
            )
            .child(
                h_flex()
                    .h(px(16.))
                    .justify_between()
                    .child(
                        h_flex()
                            .gap(px(6.))
                            .text_color(p.text_muted)
                            .child(Icon::new(IconName::Funnel).size(px(13.)))
                            .child(
                                div()
                                    .text_size(px(13.))
                                    .font_weight(FontWeight::MEDIUM)
                                    .child("Filters"),
                            ),
                    )
                    .children(clear),
            )
            .child(
                v_flex()
                    .id("addon-filters")
                    .flex_1()
                    .min_h_0()
                    .overflow_y_scroll()
                    .gap(px(16.))
                    .pb(px(8.))
                    .child(group("CATEGORY", categories, cx))
                    .child(group("TYPE", types, cx))
                    .child(group("SOURCE", sources, cx))
                    .child(group("STATE", states, cx)),
            )
    }

    fn filter_row(&self, f: Facet, count: usize, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let on = self.addons.facets.contains(&f);
        let dim = count == 0 && !on;
        h_flex()
            .id(SharedString::from(format!("filter-{f:?}")))
            .h(px(26.))
            .px(px(6.))
            .gap(px(8.))
            .rounded(px(5.))
            .cursor_pointer()
            .hover(move |s| s.bg(p.hover))
            .child(checkbox(on, p))
            .child(
                div()
                    .flex_1()
                    .min_w_0()
                    .truncate()
                    .text_size(px(13.))
                    .text_color(if dim { p.text_placeholder } else { p.text })
                    .child(f.label()),
            )
            .child(
                div()
                    .text_size(px(12.))
                    .text_color(if on { p.text_muted } else { p.text_placeholder })
                    .child(count.to_string()),
            )
            .on_click(cx.listener(move |this, _, _, cx| {
                if !this.addons.facets.remove(&f) {
                    this.addons.facets.insert(f);
                }
                cx.notify();
            }))
            .into_any_element()
    }

    fn addon_list(
        &mut self,
        query: &str,
        highlight: Option<&(String, f32)>,
        split: bool,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let (order, loaded) = self.ordered();
        let total = order.len();
        let shown: Vec<(usize, String)> = order
            .iter()
            .enumerate()
            .filter(|(_, m)| self.passes(m, query, None))
            .map(|(i, m)| (i, m.manifest.id.clone()))
            .collect();
        let vanilla = self.vanilla();
        let mut children: Vec<AnyElement> = Vec::new();
        let mut scroll_to = None;
        let mut last_group = None;
        for (i, id) in &shown {
            let group = (!vanilla).then_some(*i < loaded);
            if group != last_group {
                let title = match group {
                    Some(true) => format!("ON IN {} · LOAD ORDER", self.library.profile.name.to_uppercase()),
                    Some(false) => "OFF".to_owned(),
                    None => "INSTALLED".to_owned(),
                };
                children.push(
                    div()
                        .px(px(12.))
                        .pt(px(if children.is_empty() { 8. } else { 14. }))
                        .pb(px(6.))
                        .flex_shrink_0()
                        .child(label(title, p))
                        .into_any_element(),
                );
                last_group = group;
            }
            let fade = highlight.filter(|(h, _)| h == id).map(|(_, k)| *k);
            if fade.is_some() && self.addons.scroll_pending {
                scroll_to = Some(children.len());
            }
            let position = (*i < loaded && !vanilla).then_some(*i + 1);
            if let Some(m) = self.library.listed().find(|a| &a.manifest.id == id) {
                children.push(self.list_row(m, position, fade, p, cx));
            }
            if !split && self.open_addon.as_ref() == Some(id) {
                children.push(self.addon_detail(id, true, p, cx));
            }
        }
        if let Some(ix) = scroll_to {
            self.addons.scroll_pending = false;
            if split {
                self.addons.scroll.scroll_to_item(ix);
            } else {
                self.addons.scroll.scroll_to_top_of_item(ix);
            }
        }
        let header = h_flex()
            .h(px(28.))
            .gap(px(8.))
            .flex_shrink_0()
            .text_size(px(12.))
            .text_color(p.text_muted)
            .child(Icon::new(IconName::Layers).size(px(13.)))
            .child(div().flex_1().min_w_0().truncate().child(if vanilla {
                format!("Switch away from {VANILLA} to use addons")
            } else {
                "Lower in the list wins when two addons change the same thing".to_owned()
            }))
            .when(shown.len() != total, |d| {
                d.child(
                    div()
                        .flex_shrink_0()
                        .text_color(p.text_placeholder)
                        .child(format!("{} of {total}", shown.len())),
                )
            });
        let body = if shown.is_empty() {
            v_flex()
                .flex_1()
                .items_center()
                .justify_center()
                .gap(px(8.))
                .py(px(40.))
                .rounded(px(8.))
                .border_1()
                .border_color(p.border_variant)
                .child(badge(IconName::Search, 36., p))
                .child(
                    div()
                        .pt(px(4.))
                        .text_size(px(14.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .child("No addons match"),
                )
                .child(
                    Button::new("empty-clear")
                        .ghost()
                        .small()
                        .label("Clear filters")
                        .on_click(cx.listener(|this, _, _, cx| {
                            this.addons.facets.clear();
                            this.addons.clear_search(cx);
                            cx.notify();
                        })),
                )
                .into_any_element()
        } else {
            div()
                .min_h_0()
                .flex()
                .flex_col()
                .rounded(px(8.))
                .border_1()
                .border_color(p.border)
                .overflow_hidden()
                .child(
                    v_flex()
                        .id("addon-list")
                        .flex_1()
                        .min_h_0()
                        .overflow_y_scroll()
                        .track_scroll(&self.addons.scroll)
                        .pb(px(6.))
                        .children(children),
                )
                .into_any_element()
        };
        v_flex()
            .flex_1()
            .min_w_0()
            .h_full()
            .gap(px(6.))
            .child(header)
            .child(body)
            .into_any_element()
    }

    fn list_row(
        &self,
        m: &AddonItem,
        position: Option<usize>,
        fade: Option<f32>,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let man = &m.manifest;
        let id = man.id.clone();
        let selected = self.open_addon.as_ref() == Some(&id);
        let movable = self.movable(m);
        let pairs: Vec<&Pair> = self.pairs_of(&id).collect();
        let loses = pairs.iter().any(|pr| pr.loser == id);
        let trailing = if m.locked {
            h_flex()
                .gap(px(5.))
                .text_size(px(12.))
                .text_color(p.text_placeholder)
                .whitespace_nowrap()
                .child(Icon::new(IconName::Lock).size(px(12.)))
                .child(if self.vanilla() {
                    format!("Off in {VANILLA}")
                } else {
                    "Always on".to_owned()
                })
                .into_any_element()
        } else {
            let id = id.clone();
            self.toggle(
                format!("toggle-{id}"),
                m.enabled && !self.vanilla(),
                cx,
                move |this, on, window, cx| this.set_addon_enabled(&id, on, window, cx),
            )
        };
        let icon = match &m.icon {
            _ if man.is_builtin() => logo(28.),
            Some(icon) => addon_icon(icon, 28., p),
            None => badge(IconName::Package, 28., p),
        };
        let grip = div().w(px(14.)).flex_shrink_0().when(movable, |d| {
            d.child(
                Icon::new(IconName::GripVertical)
                    .size(px(14.))
                    .text_color(p.text_placeholder),
            )
        });
        let category = man.category.map_or("Uncategorised", Category::label);
        let row = h_flex()
            .id(SharedString::from(format!("addon-{id}")))
            .relative()
            .h(px(52.))
            .pl(px(6.))
            .pr(px(12.))
            .gap(px(10.))
            .flex_shrink_0()
            .border_t_1()
            .border_color(p.border_variant)
            .cursor_pointer()
            .map(|d| match (fade, selected) {
                (Some(k), _) => d.bg(p.warning.opacity(0.06 + 0.3 * k)),
                (None, true) => d.bg(p.selected),
                (None, false) => d.hover(move |s| s.bg(p.hover)),
            })
            .when_some(fade, |d, k| {
                d.child(
                    div()
                        .absolute()
                        .left_0()
                        .top_0()
                        .bottom_0()
                        .w(px(3.))
                        .bg(p.warning.opacity(k)),
                )
            })
            .when(selected && fade.is_none(), |d| {
                d.child(
                    div()
                        .absolute()
                        .left_0()
                        .top_0()
                        .bottom_0()
                        .w(px(3.))
                        .bg(p.accent),
                )
            })
            .child(grip)
            .child(
                div()
                    .w(px(14.))
                    .flex_shrink_0()
                    .text_size(px(11.))
                    .text_color(p.text_placeholder)
                    .children(position.map(|n| n.to_string())),
            )
            .child(icon)
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(2.))
                    .child(
                        h_flex()
                            .gap(px(8.))
                            .items_baseline()
                            .child(
                                div()
                                    .min_w_0()
                                    .text_size(px(14.))
                                    .font_weight(FontWeight::MEDIUM)
                                    .truncate()
                                    .child(man.name.clone()),
                            )
                            .child(
                                div()
                                    .flex_shrink_0()
                                    .text_size(px(12.))
                                    .text_color(p.text_placeholder)
                                    .child(man.version.to_string()),
                            ),
                    )
                    .child(
                        h_flex()
                            .gap(px(6.))
                            .text_size(px(12.))
                            .child(
                                div()
                                    .flex_shrink_0()
                                    .text_color(if man.category.is_some() {
                                        p.text_muted
                                    } else {
                                        p.text_placeholder
                                    })
                                    .child(category),
                            )
                            .when(m.source != Source::Installed, |d| {
                                d.child(
                                    div()
                                        .h(px(16.))
                                        .px(px(5.))
                                        .flex()
                                        .items_center()
                                        .flex_shrink_0()
                                        .rounded(px(3.))
                                        .border_1()
                                        .border_color(p.border)
                                        .text_size(px(10.5))
                                        .text_color(p.text_muted)
                                        .child(m.source.tag()),
                                )
                            }),
                    ),
            )
            .child(
                h_flex()
                    .w(px(40.))
                    .flex_shrink_0()
                    .justify_end()
                    .when(self.update_ready(m), |d| {
                        d.child(pill(IconName::CircleArrowUp, "", p.accent))
                    })
                    .when(!pairs.is_empty(), |d| {
                        d.child(pill(
                            IconName::TriangleAlert,
                            pairs.len().to_string(),
                            if loses { p.warning } else { p.text_muted },
                        ))
                    }),
            )
            .child(
                div()
                    .w(px(84.))
                    .flex()
                    .justify_end()
                    .flex_shrink_0()
                    .child(trailing),
            )
            .on_click(cx.listener({
                let id = id.clone();
                move |this, _, _, cx| {
                    this.open_addon = (this.open_addon.as_ref() != Some(&id)).then(|| id.clone());
                    cx.notify();
                }
            }));
        if !movable {
            return row.into_any_element();
        }
        let dragged = DraggedAddon {
            id: id.clone(),
            name: man.name.clone().into(),
        };
        row.on_drag(dragged, |d, _, _, cx| cx.new(|_| d.clone()))
            .drag_over::<DraggedAddon>(move |s, _, _, _| s.bg(p.hover).border_color(p.accent))
            .on_drop(
                cx.listener(move |this, d: &DraggedAddon, window, cx| this.move_onto(&d.id, &id, window, cx)),
            )
            .into_any_element()
    }

    fn changes_of(&mut self, id: &str) -> Changes {
        if let Some(c) = self.addons.changes.get(id) {
            return c.clone();
        }
        let Some(item) = self.library.addons.iter().find(|a| a.manifest.id == id) else {
            return Changes::default();
        };
        let c = Changes::read(&self.store, item);
        self.addons.changes.insert(id.to_owned(), c.clone());
        c
    }

    fn addon_detail(&mut self, id: &str, inline: bool, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let changes = self.changes_of(id);
        let Some(m) = self.library.addons.iter().find(|a| a.manifest.id == id) else {
            return div().into_any_element();
        };
        let man = &m.manifest;
        let builds = if man.compat.game_builds.is_empty() {
            "Any game build".to_owned()
        } else {
            man.compat
                .game_builds
                .iter()
                .map(ToString::to_string)
                .collect::<Vec<_>>()
                .join(", ")
        };
        let (compat, compat_color) = match self.install.as_ref().and_then(|i| i.build_id) {
            Some(b) if man.compat.accepts_build(b) => (format!("{builds} · matches {b}"), p.success),
            Some(b) => (format!("{builds} · not tested on {b}"), p.warning),
            None => (builds, p.text),
        };
        let on = self.addon_on(m);
        let (status, status_color) = if self.vanilla() {
            (format!("Switch away from {VANILLA} to use addons"), p.warning)
        } else if on {
            ("On".to_owned(), p.success)
        } else {
            ("Off".to_owned(), p.text_muted)
        };
        let (order, loaded) = self.ordered();
        let position = order[..loaded].iter().position(|a| a.manifest.id == id);
        let movable = self.movable(m);
        let source = match (&man.origin, m.source) {
            (Some(o), Source::Importer) => match &o.file {
                Some(f) => format!("{} · from {f}", m.source.label()),
                None => m.source.label().to_owned(),
            },
            _ => m.source.label().to_owned(),
        };
        let close = (!inline).then(|| {
            Button::new("detail-close")
                .ghost()
                .small()
                .icon(IconName::X)
                .tooltip("Close")
                .on_click(cx.listener(|this, _, _, cx| {
                    this.open_addon = None;
                    cx.notify();
                }))
        });
        let head = h_flex()
            .gap(px(12.))
            .items_start()
            .child(match &m.icon {
                _ if man.is_builtin() => logo(40.),
                Some(icon) => addon_icon(icon, 40., p),
                None => badge(IconName::Package, 40., p),
            })
            .child(
                v_flex()
                    .flex_1()
                    .min_w_0()
                    .gap(px(2.))
                    .child(
                        div()
                            .text_size(px(16.))
                            .font_weight(FontWeight::SEMIBOLD)
                            .child(man.name.clone()),
                    )
                    .child(
                        div()
                            .text_size(px(13.))
                            .text_color(p.text_muted)
                            .child(format!("{} · by {}", man.version, man.author)),
                    ),
            )
            .children(close);
        let order_line = position.map(|i| {
            let (up, down) = (id.to_owned(), id.to_owned());
            h_flex()
                .gap(px(4.))
                .child(div().flex_1().child(format!("{} of {loaded}", i + 1)))
                .when(movable, |d| {
                    d.child(
                        Button::new("detail-up")
                            .ghost()
                            .xsmall()
                            .icon(IconName::ArrowUp)
                            .tooltip("Move up")
                            .disabled(i <= 1)
                            .on_click(
                                cx.listener(move |this, _, window, cx| this.move_step(&up, true, window, cx)),
                            ),
                    )
                    .child(
                        Button::new("detail-down")
                            .ghost()
                            .xsmall()
                            .icon(IconName::ArrowDown)
                            .tooltip("Move down")
                            .disabled(i + 1 >= loaded)
                            .on_click(cx.listener(move |this, _, window, cx| {
                                this.move_step(&down, false, window, cx)
                            })),
                    )
                })
        });
        let facts = v_flex()
            .child(fact("Status", div().text_color(status_color).child(status), p))
            .when_some(order_line, |d, line| d.child(fact("Load order", line, p)))
            .child(fact(
                "Category",
                man.category.map_or("Uncategorised", Category::label),
                p,
            ))
            .child(fact("Source", source, p))
            .child(fact(
                "Game builds",
                div().text_color(compat_color).child(compat),
                p,
            ))
            .when(!man.requires.is_empty(), |d| {
                d.child(fact(
                    "Requires",
                    man.requires
                        .iter()
                        .map(|r| format!("{} {}", r.id, r.version))
                        .collect::<Vec<_>>()
                        .join(", "),
                    p,
                ))
            })
            .when_some(man.license.clone(), |d, l| d.child(fact("License", l, p)))
            .when_some(man.homepage.clone(), |d, h| d.child(fact("Homepage", h, p)))
            .child(fact(
                "ID",
                div().text_color(p.text_muted).child(man.id.clone()),
                p,
            ));
        let parts = changes.parts();
        let what = v_flex()
            .gap(px(8.))
            .child(label("WHAT IT CHANGES", p))
            .child(if parts.is_empty() {
                div()
                    .text_size(px(13.))
                    .text_color(p.text_placeholder)
                    .child(if man.launcher.is_some() {
                        "Adds pages and tools to the launcher"
                    } else {
                        "Nothing listed in its manifest"
                    })
                    .into_any_element()
            } else {
                h_flex()
                    .flex_wrap()
                    .gap(px(6.))
                    .children(parts.into_iter().map(|t| tag(t, p).text_color(p.text)))
                    .into_any_element()
            })
            .when(!changes.unreadable.is_empty(), |d| {
                d.child(
                    div()
                        .text_size(px(12.))
                        .text_color(p.warning)
                        .child(format!("Could not read {}", changes.unreadable.join(", "))),
                )
            });
        let mut actions: Vec<AnyElement> = Vec::new();
        if m.source == Source::DbEditor {
            let id = id.to_owned();
            actions.push(
                Button::new("detail-edit-db")
                    .outline()
                    .small()
                    .icon(IconName::Database)
                    .label("Edit in DB Editor")
                    .on_click(cx.listener(move |this, _, _, cx| this.db_edit_addon(&id, cx)))
                    .into_any_element(),
            );
        }
        if m.source == Source::Importer {
            actions.push(
                Button::new("detail-importer")
                    .outline()
                    .small()
                    .icon(IconName::FolderInput)
                    .label("Open in Mod Importer")
                    .on_click(cx.listener(|this, _, _, cx| this.open_page(Page::Importer, cx)))
                    .into_any_element(),
            );
        }
        let remove = (m.source != Source::Builtin && !m.locked).then(|| {
            let (id, name) = (man.id.clone(), man.name.clone());
            Button::new("detail-remove")
                .ghost()
                .small()
                .icon(IconName::Trash)
                .label("Remove")
                .text_color(p.error)
                .on_click(cx.listener(move |this, _, window, cx| this.remove_addon(&id, &name, window, cx)))
        });
        let settings = if man.options.is_empty() {
            None
        } else {
            Some(self.settings(man, p, cx).into_any_element())
        };
        let conflicts = self.conflicts_block(id, p, cx).into_any_element();
        v_flex()
            .id(SharedString::from(format!("detail-{id}")))
            .gap(px(18.))
            .map(|d| {
                if inline {
                    d.flex_shrink_0()
                        .px(px(20.))
                        .pt(px(14.))
                        .pb(px(18.))
                        .bg(p.surface)
                        .border_t_1()
                        .border_color(p.border_variant)
                } else {
                    d.w(px(DETAIL_W))
                        .flex_shrink_0()
                        .h_full()
                        .overflow_y_scroll()
                        .p(px(18.))
                        .rounded(px(8.))
                        .border_1()
                        .border_color(p.border)
                        .bg(p.surface)
                }
            })
            .when(!inline, |d| d.child(head))
            .when_some(man.description.clone(), |d, text| {
                d.child(
                    div()
                        .text_size(px(13.))
                        .line_height(px(20.))
                        .text_color(p.text_muted)
                        .child(text),
                )
            })
            .when(!actions.is_empty() || remove.is_some(), |d| {
                d.child(
                    h_flex()
                        .flex_wrap()
                        .gap(px(8.))
                        .when(!actions.is_empty(), |d| d.children(actions).child(div().flex_1()))
                        .children(remove),
                )
            })
            .child(facts)
            .child(what)
            .child(conflicts)
            .children(settings)
            .into_any_element()
    }

    fn conflicts_block(&self, id: &str, p: Palette, cx: &mut Context<Self>) -> impl IntoElement {
        let report = self.addons.report.as_ref();
        let check = Button::new("check-conflicts")
            .outline()
            .xsmall()
            .icon(IconName::RefreshCw)
            .label(if self.addons.checking {
                "Checking…"
            } else {
                "Check conflicts"
            })
            .loading(self.addons.checking)
            .disabled(self.vanilla() || self.addons.checking)
            .on_click(cx.listener(|this, _, window, cx| this.check_conflicts(window, cx)));
        let note = match report {
            _ if self.vanilla() => format!("{VANILLA} has no addons to conflict"),
            None => "Not checked yet. Checking plans this profile's build without installing it.".to_owned(),
            Some(r) if self.changed_since(id, r.built_at) => format!(
                "Checked {}, before this addon last changed. Check again to include it.",
                ago(r.built_at)
            ),
            Some(r) => format!("Checked {}", ago(r.built_at)),
        };
        let stale = report.is_some_and(|r| self.changed_since(id, r.built_at));
        let pairs: Vec<&Pair> = self.pairs_of(id).collect();
        let rows: Vec<AnyElement> = pairs
            .iter()
            .enumerate()
            .map(|(i, pair)| self.pair_row(id, pair, i == 0, p, cx))
            .collect();
        v_flex()
            .gap(px(8.))
            .child(
                h_flex()
                    .justify_between()
                    .items_center()
                    .child(label("CONFLICTS", p))
                    .child(check),
            )
            .child(
                div()
                    .text_size(px(12.))
                    .text_color(p.text_placeholder)
                    .child(note),
            )
            .when(report.is_some() && !stale && pairs.is_empty(), |d| {
                d.child(
                    h_flex()
                        .gap(px(6.))
                        .text_size(px(13.))
                        .text_color(p.text_muted)
                        .child(
                            Icon::new(IconName::CircleCheck)
                                .size(px(14.))
                                .text_color(p.success),
                        )
                        .child("No conflicts with other addons"),
                )
            })
            .when(!rows.is_empty(), |d| {
                d.child(
                    v_flex()
                        .rounded(px(6.))
                        .border_1()
                        .border_color(p.border_variant)
                        .overflow_hidden()
                        .children(rows),
                )
            })
    }

    fn pair_row(&self, id: &str, pair: &Pair, first: bool, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let wins = pair.winner == id;
        let other = if wins { &pair.loser } else { &pair.winner };
        let key = (pair.winner.clone(), pair.loser.clone());
        let open = self.addons.open_pairs.contains(&key);
        let text = if wins {
            format!("Overrides {} on {}", self.addon_name(other), pair_counts(pair))
        } else {
            format!(
                "Overridden by {} on {}",
                self.addon_name(other),
                pair_counts(pair)
            )
        };
        let items = open.then(|| {
            let more = pair.items.len().saturating_sub(ITEMS_SHOWN);
            v_flex()
                .px(px(12.))
                .pb(px(10.))
                .pl(px(34.))
                .gap(px(2.))
                .children(pair.items.iter().take(ITEMS_SHOWN).map(|item| {
                    div()
                        .text_size(px(12.))
                        .line_height(px(17.))
                        .text_color(p.text_muted)
                        .child(item.clone())
                }))
                .when(more > 0, |d| {
                    d.child(
                        div()
                            .text_size(px(12.))
                            .text_color(p.text_placeholder)
                            .child(format!("and {more} more")),
                    )
                })
        });
        v_flex()
            .when(!first, |d| d.border_t_1().border_color(p.border_variant))
            .child(
                h_flex()
                    .id(SharedString::from(format!("pair-{}-{}", pair.winner, pair.loser)))
                    .px(px(12.))
                    .py(px(8.))
                    .gap(px(10.))
                    .items_start()
                    .cursor_pointer()
                    .hover(move |s| s.bg(p.hover))
                    .child(
                        Icon::new(if wins {
                            IconName::ArrowUp
                        } else {
                            IconName::ArrowDown
                        })
                        .size(px(14.))
                        .mt(px(2.))
                        .text_color(if wins { p.success } else { p.warning }),
                    )
                    .child(
                        div()
                            .flex_1()
                            .min_w_0()
                            .text_size(px(13.))
                            .line_height(px(19.))
                            .child(text),
                    )
                    .child(chevron(open, p))
                    .on_click(cx.listener(move |this, _, _, cx| {
                        if !this.addons.open_pairs.remove(&key) {
                            this.addons.open_pairs.insert(key.clone());
                        }
                        cx.notify();
                    })),
            )
            .children(items)
            .into_any_element()
    }
}
