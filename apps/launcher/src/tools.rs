//! The Tools page: tools that make or edit Swofty addons.

use gpui_kit::*;

use crate::shell::{Page, Shell};
use crate::theme::Palette;

impl Shell {
    pub(crate) fn tools_page(&mut self, p: Palette, _cx: &mut Context<Self>) -> AnyElement {
        div()
            .size_full()
            .child(self.header(Page::Tools.label(), String::new(), div(), p))
            .into_any_element()
    }
}
