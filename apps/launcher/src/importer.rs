//! The Mod Importer: the managed Simplified for Swofty addon's section, shown
//! as a big section of its own.

use std::time::Instant;

use gpui_kit::*;
use swofty_core::managed;

use crate::shell::{Page, Shell};
use crate::theme::Palette;

impl Shell {
    /// The index of the Mod Importer's section, when its addon is on.
    pub(crate) fn importer_section(&self) -> Option<usize> {
        self.library.sections.iter().position(|s| s.addon == managed::ID)
    }

    pub(crate) fn importer_page(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        match self.importer_section() {
            Some(i) => self.section_page(i, p, cx).into_any_element(),
            None => div()
                .size_full()
                .child(self.header(Page::Importer.label(), String::new(), div(), p))
                .into_any_element(),
        }
    }

    /// Switches to Addons and highlights `id` there.
    pub(crate) fn show_addon(&mut self, id: &str, cx: &mut Context<Self>) {
        self.highlight = Some((id.to_owned(), Instant::now()));
        self.go(Page::Addons, cx);
    }
}
