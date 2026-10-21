//! The News page: the Discord banner and the release feed.

use gpui_kit::*;

use crate::shell::{Page, Shell};
use crate::theme::Palette;

impl Shell {
    pub(crate) fn news_page(&mut self, p: Palette, _cx: &mut Context<Self>) -> AnyElement {
        div()
            .size_full()
            .child(self.header(Page::News.label(), String::new(), div(), p))
            .into_any_element()
    }
}
