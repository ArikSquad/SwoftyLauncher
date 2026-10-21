//! The Mod Importer: the managed Simplified for Swofty addon's section, shown
//! as a big section of its own.

use std::time::{Duration, Instant};

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariants};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::{Disableable, WindowExt, v_flex};
use gpui_kit::*;
use swofty_core::managed;
use swofty_core::profile::VANILLA;

use crate::sections::{Reporter, Shared};
use crate::shell::{Page, Shell};
use crate::theme::Palette;

const FETCH_TIMEOUT: Duration = Duration::from_secs(20);

impl Shell {
    /// The index of the Mod Importer's section, when its addon is on.
    pub(crate) fn importer_section(&self) -> Option<usize> {
        self.library.sections.iter().position(|s| s.addon == managed::ID)
    }

    /// The section the current page shows: an addon page or the Mod Importer.
    pub(crate) fn open_section(&self) -> Option<usize> {
        match self.page {
            Page::Section(i) => Some(i),
            Page::Importer => self.importer_section(),
            _ => None,
        }
    }

    pub(crate) fn importer_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        if let Some(i) = self.importer_section() {
            return self.section_page(i, p, cx).into_any_element();
        }
        let installed = self.library.addons.iter().any(|a| a.manifest.id == managed::ID);
        let adding = self.updates.adding;
        let (title, body, button) = if !installed {
            (
                "Install the Mod Importer".to_owned(),
                format!(
                    "The Mod Importer is {}, an addon by Team Simplified. It turns mods made for the game \
                     folder into Swofty addons. The launcher downloads it from GitHub.",
                    managed::NAME
                ),
                Button::new("importer-install")
                    .primary()
                    .icon(IconName::Download)
                    .label(if adding { "Installing…" } else { "Install" })
                    .loading(adding)
                    .disabled(adding)
                    .on_click(cx.listener(|this, _, window, cx| this.install_importer(window, cx))),
            )
        } else if self.vanilla() {
            (
                format!("The Mod Importer is off in {VANILLA}"),
                format!("{VANILLA} plays the game without addons. Switch to another profile to import mods."),
                Button::new("importer-profiles")
                    .outline()
                    .icon(IconName::Layers)
                    .label("Profiles")
                    .on_click(cx.listener(|this, _, _, cx| this.go(Page::Profiles, cx))),
            )
        } else {
            (
                "Turn on the Mod Importer".to_owned(),
                format!(
                    "The Mod Importer is {}, which is off in {}.",
                    managed::NAME,
                    self.library.profile.name
                ),
                Button::new("importer-on")
                    .primary()
                    .icon(IconName::Power)
                    .label("Turn on")
                    .on_click(cx.listener(|this, _, window, cx| {
                        this.set_addon_enabled(managed::ID, true, window, cx)
                    })),
            )
        };
        v_flex()
            .size_full()
            .child(self.header(Page::Importer.label(), "Team Simplified".to_owned(), div(), p))
            .child(self.empty(IconName::FolderInput, title, body, button, p))
            .into_any_element()
    }

    fn install_importer(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        if self.updates.adding {
            return;
        }
        self.updates.adding = true;
        let store = self.store.clone();
        let offered = self.updates.addon.offer(None).cloned();
        cx.spawn_in(window, async move |this, cx| {
            let result = cx
                .background_executor()
                .spawn(async move {
                    let release = match offered {
                        Some(r) => r,
                        None => managed::fetch_latest(FETCH_TIMEOUT).map_err(|e| e.to_string())?,
                    };
                    managed::install_release(&store, &release, &Reporter(Shared::default()))
                        .map_err(|e| e.to_string())
                })
                .await;
            this.update_in(cx, |s, window, cx| {
                s.updates.adding = false;
                match result {
                    Ok(version) => {
                        s.updates.addon_installed = Some(version.clone());
                        s.reload(cx);
                        if s.importer_section().is_none() && !s.vanilla() {
                            s.set_addon_enabled(managed::ID, true, window, cx);
                        }
                        window.push_notification(
                            Notification::success(format!("Installed {} {version}", managed::NAME)),
                            cx,
                        );
                    }
                    Err(e) => window.push_notification(
                        Notification::error(e).title(format!("Could not install {}", managed::NAME)),
                        cx,
                    ),
                }
                cx.notify();
            })
            .ok();
        })
        .detach();
        cx.notify();
    }

    /// Switches to Addons and highlights `id` there.
    pub(crate) fn show_addon(&mut self, id: &str, cx: &mut Context<Self>) {
        self.highlight = Some((id.to_owned(), Instant::now()));
        self.go(Page::Addons, cx);
    }
}
