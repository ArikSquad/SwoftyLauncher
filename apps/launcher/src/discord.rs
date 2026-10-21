use std::sync::{Arc, LazyLock};

use gpui_kit::component::{h_flex, v_flex};
use gpui_kit::*;

pub const INVITE: &str = "https://discord.gg/STWsgyJKZS";
pub const LABEL: &str = "Join the Discord";

const BODY: &str = "Get help from the developers and hear about new releases and addons first.";
const LOGO: &[u8] = include_bytes!("../assets/discord.svg");
const BLURPLE: u32 = 0x5865f2;
const BLURPLE_HOVER: u32 = 0x4752c4;

fn server_icon(size: f32) -> Div {
    static ICON: LazyLock<Arc<Image>> = LazyLock::new(|| {
        Arc::new(Image::from_bytes(
            ImageFormat::Png,
            include_bytes!("../assets/discord-server.png").to_vec(),
        ))
    });
    div()
        .size(px(size))
        .flex_shrink_0()
        .rounded(px(size * 0.28))
        .overflow_hidden()
        .child(img(ICON.clone()).size_full())
}

/// The Discord invite at the top of the News page.
pub(crate) fn banner() -> Stateful<Div> {
    let white = |alpha: f32| hsla(0., 0., 1., alpha);
    h_flex()
        .id("discord-banner")
        .relative()
        .flex_shrink_0()
        .w_full()
        .min_h(px(116.))
        .px(px(24.))
        .py(px(20.))
        .gap(px(18.))
        .items_center()
        .rounded(px(10.))
        .overflow_hidden()
        .bg(rgb(BLURPLE))
        .cursor_pointer()
        .hover(|s| s.bg(rgb(BLURPLE_HOVER)))
        .on_click(|_, _, cx| cx.open_url(INVITE))
        .child(
            svg()
                .data(LOGO)
                .absolute()
                .top(px(-18.))
                .right(px(150.))
                .size(px(150.))
                .text_color(white(0.08)),
        )
        .child(server_icon(68.))
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .gap(px(3.))
                .text_color(white(1.))
                .child(
                    h_flex()
                        .gap(px(6.))
                        .items_center()
                        .text_size(px(11.))
                        .font_weight(FontWeight::SEMIBOLD)
                        .text_color(white(0.8))
                        .child(svg().data(LOGO).size(px(13.)).text_color(white(0.8)))
                        .child("DISCORD SERVER"),
                )
                .child(
                    div()
                        .text_size(px(20.))
                        .font_weight(FontWeight::BOLD)
                        .child("Swofty Developments"),
                )
                .child(
                    div()
                        .text_size(px(13.))
                        .line_height(px(18.))
                        .text_color(white(0.82))
                        .child(BODY),
                ),
        )
        .child(
            h_flex()
                .id("discord-join")
                .flex_shrink_0()
                .h(px(38.))
                .px(px(16.))
                .gap(px(8.))
                .items_center()
                .rounded(px(8.))
                .bg(white(1.))
                .hover(|s| s.bg(white(0.9)))
                .text_size(px(14.))
                .font_weight(FontWeight::SEMIBOLD)
                .text_color(rgb(BLURPLE))
                .child(svg().data(LOGO).size(px(16.)).text_color(rgb(BLURPLE)))
                .child(LABEL)
                .on_click(|_, _, cx| {
                    cx.stop_propagation();
                    cx.open_url(INVITE);
                }),
        )
}
