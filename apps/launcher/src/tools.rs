//! The Tools page: tools that make or edit Swofty addons, listed in
//! `assets/tools.json`.

use std::path::PathBuf;
use std::sync::LazyLock;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Icon, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use serde::Deserialize;

use crate::pages::{GUTTER, addon_icon};
use crate::shell::{Page, Shell};
use crate::theme::Palette;

#[derive(Clone, Debug, Deserialize)]
pub struct Link {
    pub label: String,
    pub url: String,
}

#[derive(Clone, Debug, Deserialize)]
pub struct Tool {
    pub name: String,
    pub maker: String,
    pub icon: String,
    pub about: String,
    #[serde(default)]
    pub open: Option<String>,
    #[serde(default)]
    pub get: Option<String>,
    #[serde(default)]
    pub more: Option<Link>,
}

#[derive(Deserialize)]
struct List {
    tools: Vec<Tool>,
}

pub fn tools() -> &'static [Tool] {
    static TOOLS: LazyLock<Vec<Tool>> = LazyLock::new(|| {
        serde_json::from_str::<List>(include_str!("../assets/tools.json"))
            .map(|l| l.tools)
            .unwrap_or_default()
    });
    &TOOLS
}

fn icon(name: &str) -> IconName {
    match name {
        "database" => IconName::Database,
        "folder-input" => IconName::FolderInput,
        "app-window" => IconName::AppWindow,
        "user-pen" => IconName::UserPen,
        "users" => IconName::Users,
        "package" => IconName::Package,
        "table" => IconName::Table,
        "map" => IconName::Map,
        "palette" => IconName::Palette,
        "code" => IconName::Code,
        _ => IconName::Wrench,
    }
}

fn program(name: &str) -> Option<PathBuf> {
    let path = std::env::current_exe()
        .ok()?
        .parent()?
        .join(format!("{name}{}", std::env::consts::EXE_SUFFIX));
    path.is_file().then_some(path)
}

enum Action {
    Go(Page),
    TurnOn(String),
    Run(PathBuf),
    Get(String),
    None,
}

impl Shell {
    fn tool_action(&self, tool: &Tool) -> Action {
        let get = || tool.get.clone().map_or(Action::None, Action::Get);
        let Some((kind, target)) = tool.open.as_deref().and_then(|o| o.split_once(':')) else {
            return get();
        };
        match kind {
            "page" => match target {
                "database" => Action::Go(Page::Database),
                "importer" => Action::Go(Page::Importer),
                "addons" => Action::Go(Page::Addons),
                _ => get(),
            },
            "addon" => match self.library.sections.iter().position(|s| s.addon == target) {
                Some(i) => Action::Go(Page::Section(i)),
                None if !self.vanilla() && self.library.addons.iter().any(|a| a.manifest.id == target) => {
                    Action::TurnOn(target.to_owned())
                }
                None => get(),
            },
            "program" => program(target).map_or_else(get, Action::Run),
            _ => get(),
        }
    }

    fn tool_card(
        &self,
        index: usize,
        tool: &Tool,
        width: f32,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let addon = tool
            .open
            .as_deref()
            .and_then(|o| o.strip_prefix("addon:"))
            .and_then(|id| self.library.addons.iter().find(|a| a.manifest.id == id))
            .and_then(|a| a.icon.clone());
        let art = match addon {
            Some(i) => addon_icon(&i, 40., p),
            None => div()
                .size(px(40.))
                .flex_shrink_0()
                .flex()
                .items_center()
                .justify_center()
                .rounded(px(9.))
                .bg(p.selected)
                .child(
                    Icon::new(icon(&tool.icon))
                        .size(px(20.))
                        .text_color(p.accent_hover),
                ),
        };
        let id = |what: &str| SharedString::from(format!("tool-{index}-{what}"));
        let action = self.tool_action(tool);
        let button = match action {
            Action::None => None,
            Action::Get(url) => Some(
                Button::new(id("get"))
                    .outline()
                    .icon(IconName::ExternalLink)
                    .label("Get")
                    .on_click(move |_, _, cx| cx.open_url(&url)),
            ),
            Action::Go(page) => Some(
                Button::new(id("open"))
                    .primary()
                    .label("Open")
                    .on_click(cx.listener(move |this, _, _, cx| this.go(page, cx))),
            ),
            Action::TurnOn(addon) => Some(
                Button::new(id("on"))
                    .primary()
                    .icon(IconName::Power)
                    .label("Turn on")
                    .on_click(cx.listener(move |this, _, window, cx| {
                        this.set_addon_enabled(&addon, true, window, cx)
                    })),
            ),
            Action::Run(path) => {
                let name = tool.name.clone();
                Some(
                    Button::new(id("run"))
                        .primary()
                        .label("Open")
                        .on_click(move |_, window, cx| {
                            let mut command = std::process::Command::new(&path);
                            if let Some(dir) = path.parent() {
                                command.current_dir(dir);
                            }
                            match command.spawn() {
                                Ok(_) => window.push_notification(
                                    Notification::info(format!("{name} opens in your browser")),
                                    cx,
                                ),
                                Err(e) => window.push_notification(
                                    Notification::error(e.to_string())
                                        .title(format!("Could not start {name}")),
                                    cx,
                                ),
                            }
                        }),
                )
            }
        };
        let more = tool.more.clone().map(|l| {
            Button::new(id("more"))
                .ghost()
                .label(l.label)
                .on_click(move |_, _, cx| cx.open_url(&l.url))
        });
        v_flex()
            .w(px(width))
            .flex_shrink_0()
            .p(px(16.))
            .gap(px(12.))
            .rounded(px(10.))
            .border_1()
            .border_color(p.border)
            .bg(p.surface)
            .child(
                h_flex().gap(px(12.)).items_center().child(art).child(
                    v_flex()
                        .flex_1()
                        .min_w_0()
                        .gap(px(1.))
                        .child(
                            div()
                                .text_size(px(15.))
                                .font_weight(FontWeight::SEMIBOLD)
                                .truncate()
                                .child(tool.name.clone()),
                        )
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(p.text_placeholder)
                                .truncate()
                                .child(format!("by {}", tool.maker)),
                        ),
                ),
            )
            .child(
                div()
                    .flex_1()
                    .min_h(px(38.))
                    .text_size(px(13.))
                    .line_height(px(19.))
                    .text_color(p.text_muted)
                    .child(tool.about.clone()),
            )
            .child(
                h_flex()
                    .gap(px(8.))
                    .items_center()
                    .children(button)
                    .children(more),
            )
            .into_any_element()
    }

    pub(crate) fn tools_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        const GAP: f32 = 14.;
        let width = self.content_width;
        let columns = if width >= 1080. {
            3.
        } else if width >= 600. {
            2.
        } else {
            1.
        };
        let card = ((width - GAP * (columns - 1.)) / columns).floor();
        let cards: Vec<AnyElement> = tools()
            .iter()
            .enumerate()
            .map(|(i, t)| self.tool_card(i, t, card, p, cx))
            .collect();
        v_flex()
            .size_full()
            .child(self.header(
                Page::Tools.label(),
                "Tools that make or edit Swofty addons".to_owned(),
                div(),
                p,
            ))
            .child(
                div().id("tools").flex_1().min_h_0().overflow_y_scroll().child(
                    h_flex()
                        .flex_wrap()
                        .items_start()
                        .gap(px(GAP))
                        .px(px(GUTTER))
                        .pt(px(4.))
                        .pb(px(28.))
                        .when(cards.is_empty(), |d| d.child("No tools listed"))
                        .children(cards),
                ),
            )
            .into_any_element()
    }
}

#[cfg(test)]
mod tests {
    use gpui_kit::assets::IconName;

    use super::{icon, tools};

    #[test]
    fn lists_the_tools() {
        let names: Vec<&str> = tools().iter().map(|t| t.name.as_str()).collect();
        assert!(names.starts_with(&["DB Editor", "Mod Importer"]));
        for t in tools() {
            assert!(t.open.is_some() || t.get.is_some(), "{} has no button", t.name);
            assert!(
                !matches!(icon(&t.icon), IconName::Wrench),
                "{} has an unknown icon",
                t.name
            );
            assert!(t.about.ends_with('.'), "{}", t.name);
        }
    }
}
