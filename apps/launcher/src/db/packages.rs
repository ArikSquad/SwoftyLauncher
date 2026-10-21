use std::cell::Cell;
use std::path::PathBuf;
use std::rc::Rc;

use gpui_kit::assets::IconName;
use gpui_kit::component::button::{Button, ButtonVariant, ButtonVariants};
use gpui_kit::component::input::{Input, InputState};
use gpui_kit::component::menu::{DropdownMenu, PopupMenuItem};
use gpui_kit::component::notification::Notification;
use gpui_kit::component::switch::Switch;
use gpui_kit::component::{Disableable, Icon, Sizable, WindowExt, h_flex, v_flex};
use gpui_kit::prelude::FluentBuilder;
use gpui_kit::*;
use swofty_core::dbedit::package::{self, Package, PackageInfo};
use swofty_core::store::Store;

use super::page::section_head;
use super::{FIRST_PACKAGE, Nav, guarded, text};
use crate::library::plural;
use crate::pages::label;
use crate::shell::{Page, Shell};
use crate::theme::{Palette, palette};

const EXPLAINER: &str = "A package is a set of database changes. Turn it on in a profile and it runs as an addon, \
                         listed on Addons as Built from DB Editor. When two packages change the same value, the \
                         lower one wins. Export a package as a .zip to share it; Import… adds one from a .zip, a DB \
                         Editor export or an EGO GameDatabases Editor export.";

/// The author last typed into a package, so a shared zip never carries the
/// OS user name unasked.
pub(crate) fn default_author(store: &Store) -> String {
    package::list(store)
        .unwrap_or_default()
        .into_iter()
        .map(|p| p.author)
        .find(|a| a != package::DEFAULT_AUTHOR && !a.trim().is_empty())
        .unwrap_or_else(|| package::DEFAULT_AUTHOR.into())
}

fn field(title: &'static str, input: &Entity<InputState>, p: Palette) -> Div {
    v_flex()
        .gap(px(6.))
        .child(
            div()
                .text_size(px(12.))
                .font_weight(FontWeight::MEDIUM)
                .text_color(p.text_muted)
                .child(title),
        )
        .child(Input::new(input).small())
}

#[derive(Clone)]
struct Form {
    name: Entity<InputState>,
    version: Option<Entity<InputState>>,
    author: Entity<InputState>,
    description: Entity<InputState>,
}

impl Form {
    fn read(&self, cx: &App) -> [String; 4] {
        let v = |i: &Entity<InputState>| i.read(cx).value().trim().to_owned();
        [
            v(&self.name),
            self.version.as_ref().map(v).unwrap_or_default(),
            v(&self.author),
            v(&self.description),
        ]
    }
}

impl Shell {
    pub(crate) fn db_picker(&self, disabled: bool, cx: &mut Context<Self>) -> impl IntoElement {
        let shown = match (self.db_editing(), &self.db.editing) {
            (Some(info), _) => info.name.clone(),
            (None, Some(id)) => id.clone(),
            (None, None) => "No package".into(),
        };
        let current = self.db.editing.clone();
        let list: Vec<(String, String, bool)> = self
            .db
            .packages
            .iter()
            .map(|p| (p.id.clone(), p.name.clone(), self.db_enabled(&p.id)))
            .collect();
        let shell = cx.entity().downgrade();
        Button::new("db-picker")
            .outline()
            .small()
            .min_w(px(180.))
            .icon(IconName::Package)
            .label(shown)
            .disabled(disabled)
            .dropdown_caret(true)
            .dropdown_menu_with_anchor(Anchor::TopLeft, move |mut menu, _, _| {
                menu = menu.min_w(px(260.)).max_h(px(380.)).scrollable(true);
                for (id, name, on) in &list {
                    let (shell, id2) = (shell.clone(), id.clone());
                    let name = if *on {
                        name.clone()
                    } else {
                        format!("{name} (off)")
                    };
                    menu = menu.item(
                        PopupMenuItem::new(name)
                            .checked(current.as_ref() == Some(id))
                            .on_click(move |_, _, cx| {
                                shell.update(cx, |s, cx| s.db_select(Some(id2.clone()), cx)).ok();
                            }),
                    );
                }
                if !list.is_empty() {
                    menu = menu.separator();
                }
                let shell = shell.clone();
                menu.item(PopupMenuItem::new("New package…").icon(IconName::Plus).on_click(
                    move |_, window, cx| {
                        shell.update(cx, |s, cx| s.db_new_package(window, cx)).ok();
                    },
                ))
            })
    }

    pub(crate) fn db_packages(&mut self, p: Palette, cx: &mut Context<Self>) -> AnyElement {
        let profile = self.library.profile.name.clone();
        let stack = self.db_stack();
        let total = self.db.packages.len();
        let subtitle = if self.vanilla() {
            format!("{} · Vanilla stays stock", plural(total, "package", "packages"))
        } else {
            format!(
                "{} · {} on in {profile}",
                plural(total, "package", "packages"),
                stack.len()
            )
        };
        let buttons = h_flex()
            .gap(px(8.))
            .child(
                Button::new("db-pk-import")
                    .outline()
                    .small()
                    .icon(IconName::FileDown)
                    .label("Import…")
                    .loading(self.db.importing)
                    .disabled(self.db.importing)
                    .on_click(cx.listener(|this, _, window, cx| this.db_import(window, cx))),
            )
            .child(
                Button::new("db-pk-new")
                    .primary()
                    .small()
                    .icon(IconName::Plus)
                    .label("New package")
                    .on_click(cx.listener(|this, _, window, cx| this.db_new_package(window, cx))),
            );
        let head = h_flex()
            .gap(px(12.))
            .flex_shrink_0()
            .child(section_head(IconName::Package, "Packages", &subtitle, p))
            .child(buttons);
        let explainer = h_flex()
            .flex_shrink_0()
            .items_start()
            .gap(px(12.))
            .px(px(14.))
            .py(px(12.))
            .rounded(px(8.))
            .border_1()
            .border_color(p.border_variant)
            .bg(p.surface)
            .child(
                Icon::new(IconName::Info)
                    .size(px(15.))
                    .mt(px(2.))
                    .text_color(p.accent),
            )
            .child(
                div()
                    .flex_1()
                    .min_w_0()
                    .text_size(px(13.))
                    .line_height(px(20.))
                    .text_color(p.text_muted)
                    .child(EXPLAINER),
            );
        let mut body: Vec<AnyElement> = Vec::new();
        if total == 0 {
            body.push(self.empty(
                IconName::Package,
                "No packages yet",
                format!(
                    "Make one with New package, or change any value and the editor makes {FIRST_PACKAGE} for you."
                ),
                Button::new("db-pk-empty-new")
                    .outline()
                    .icon(IconName::Plus)
                    .label("New package")
                    .on_click(cx.listener(|this, _, window, cx| this.db_new_package(window, cx))),
                p,
            ));
        } else {
            if !stack.is_empty() {
                body.push(
                    h_flex()
                        .pt(px(4.))
                        .gap(px(10.))
                        .items_center()
                        .child(label(format!("ON IN {} · LOAD ORDER", profile.to_uppercase()), p))
                        .child(
                            h_flex()
                                .flex_1()
                                .min_w_0()
                                .gap(px(6.))
                                .text_size(px(12.))
                                .text_color(p.text_muted)
                                .child(div().w(px(1.)).h(px(12.)).bg(p.border))
                                .child(Icon::new(IconName::Layers).size(px(13.)))
                                .child(
                                    div()
                                        .truncate()
                                        .child("When two packages change the same value, the lower one wins"),
                                ),
                        )
                        .child(
                            Button::new("db-pk-all-off")
                                .ghost()
                                .xsmall()
                                .icon(IconName::Power)
                                .label("Turn off all packages")
                                .on_click(
                                    cx.listener(|this, _, window, cx| this.db_turn_off_all(window, cx)),
                                ),
                        )
                        .into_any_element(),
                );
                let n = stack.len();
                for (i, id) in stack.iter().enumerate() {
                    if let Some(info) = self.db_package(id).cloned() {
                        body.push(self.db_package_card(&info, Some((i, n)), p, cx));
                    }
                }
            }
            let rest: Vec<PackageInfo> = self
                .db
                .packages
                .iter()
                .filter(|pk| !stack.contains(&pk.id))
                .cloned()
                .collect();
            if !rest.is_empty() {
                let title = if self.vanilla() {
                    "ALL PACKAGES".to_owned()
                } else {
                    format!("OFF IN {}", profile.to_uppercase())
                };
                body.push(div().pt(px(10.)).child(label(title, p)).into_any_element());
                for info in &rest {
                    body.push(self.db_package_card(info, None, p, cx));
                }
            }
        }
        v_flex()
            .size_full()
            .gap(px(14.))
            .child(head)
            .child(
                v_flex()
                    .id("db-pk-scroll")
                    .flex_1()
                    .min_h_0()
                    .overflow_y_scroll()
                    .gap(px(10.))
                    .pb(px(12.))
                    .child(explainer)
                    .children(body),
            )
            .into_any_element()
    }

    fn db_package_card(
        &self,
        info: &PackageInfo,
        order: Option<(usize, usize)>,
        p: Palette,
        cx: &mut Context<Self>,
    ) -> AnyElement {
        let id = info.id.clone();
        let editing = self.db.editing.as_deref() == Some(id.as_str());
        let on = order.is_some();
        let vanilla = self.vanilla();
        let button = |name: &str| SharedString::from(format!("db-pk-{name}-{id}"));
        let switch = {
            let id = id.clone();
            Switch::new(button("switch"))
                .checked(on)
                .small()
                .disabled(vanilla)
                .on_change(cx.listener(move |this, on: &bool, window, cx| {
                    this.set_addon_enabled(&id, *on, window, cx)
                }))
        };
        let state = if vanilla {
            ("Vanilla stays stock".to_owned(), p.text_placeholder)
        } else if on {
            (format!("On in {}", self.library.profile.name), p.success)
        } else {
            ("Off".to_owned(), p.text_muted)
        };
        let moves = order.map(|(i, n)| {
            let (up, down) = (id.clone(), id.clone());
            h_flex()
                .gap(px(2.))
                .child(
                    Button::new(button("up"))
                        .ghost()
                        .xsmall()
                        .icon(IconName::ArrowUp)
                        .tooltip("Load earlier")
                        .disabled(i == 0)
                        .on_click(cx.listener(move |this, _, window, cx| {
                            this.db_move_package(&up, true, window, cx)
                        })),
                )
                .child(
                    Button::new(button("down"))
                        .ghost()
                        .xsmall()
                        .icon(IconName::ArrowDown)
                        .tooltip("Load later")
                        .disabled(i + 1 == n)
                        .on_click(cx.listener(move |this, _, window, cx| {
                            this.db_move_package(&down, false, window, cx)
                        })),
                )
        });
        let place = order.map(|(i, _)| {
            div()
                .size(px(24.))
                .flex_shrink_0()
                .rounded_full()
                .flex()
                .items_center()
                .justify_center()
                .bg(p.hover)
                .text_size(px(12.))
                .font_weight(FontWeight::SEMIBOLD)
                .text_color(p.text_muted)
                .child((i + 1).to_string())
        });
        let badge = editing.then(|| {
            h_flex()
                .h(px(20.))
                .px(px(7.))
                .gap(px(4.))
                .rounded(px(4.))
                .bg(p.accent.opacity(0.14))
                .text_size(px(11.))
                .font_weight(FontWeight::SEMIBOLD)
                .text_color(p.accent)
                .child(Icon::new(IconName::Pencil).size(px(11.)))
                .child("Editing")
        });
        let damaged = self.db.damaged.get(&id).cloned();
        let changes = if damaged.is_some() {
            None
        } else {
            Some(plural(info.changes, "change", "changes"))
        };
        let by = (!info.author.is_empty()).then(|| format!("by {}", info.author));
        let author = by.into_iter().chain(changes).collect::<Vec<_>>().join(" · ");
        let head = h_flex()
            .px(px(16.))
            .pt(px(14.))
            .pb(px(12.))
            .gap(px(14.))
            .items_start()
            .children(place)
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
                                    .text_size(px(15.))
                                    .font_weight(FontWeight::SEMIBOLD)
                                    .truncate()
                                    .child(info.name.clone()),
                            )
                            .child(
                                div()
                                    .text_size(px(12.))
                                    .text_color(p.text_placeholder)
                                    .child(info.version.clone()),
                            )
                            .children(badge),
                    )
                    .child(div().text_size(px(12.)).text_color(p.text_muted).child(author))
                    .when_some(damaged, |d, e| {
                        d.child(
                            h_flex()
                                .pt(px(4.))
                                .gap(px(6.))
                                .items_start()
                                .text_size(px(12.))
                                .line_height(px(17.))
                                .text_color(p.error)
                                .child(Icon::new(IconName::CircleAlert).size(px(12.)).mt(px(2.)))
                                .child(div().flex_1().min_w_0().child(format!("Could not be read: {e}"))),
                        )
                    })
                    .when(!info.description.is_empty(), |d| {
                        d.child(
                            div()
                                .pt(px(4.))
                                .text_size(px(13.))
                                .line_height(px(19.))
                                .text_color(p.text)
                                .child(info.description.clone()),
                        )
                    }),
            )
            .children(moves)
            .child(
                h_flex()
                    .gap(px(8.))
                    .child(
                        div()
                            .text_size(px(12.))
                            .text_color(state.1)
                            .whitespace_nowrap()
                            .child(state.0),
                    )
                    .child(switch),
            );
        let (edit, details, dup, export, delete) =
            (id.clone(), id.clone(), id.clone(), id.clone(), id.clone());
        let actions = h_flex()
            .px(px(12.))
            .py(px(8.))
            .gap(px(4.))
            .border_t_1()
            .border_color(p.border_variant)
            .child(
                Button::new(button("edit"))
                    .ghost()
                    .small()
                    .icon(IconName::Pencil)
                    .label(if editing { "Editing" } else { "Edit" })
                    .disabled(editing)
                    .on_click(cx.listener(move |this, _, _, cx| {
                        this.db_select(Some(edit.clone()), cx);
                        if let Some(m) = this.db.modules.first() {
                            let to = Nav::Module(m.id.clone());
                            this.db_go(to, cx);
                        }
                    })),
            )
            .child(
                Button::new(button("details"))
                    .ghost()
                    .small()
                    .icon(IconName::SquarePen)
                    .label("Details…")
                    .on_click(cx.listener(move |this, _, window, cx| this.db_details(&details, window, cx))),
            )
            .child(
                Button::new(button("dup"))
                    .ghost()
                    .small()
                    .icon(IconName::Copy)
                    .label("Duplicate")
                    .on_click(cx.listener(move |this, _, window, cx| this.db_duplicate(&dup, window, cx))),
            )
            .child(self.db_export_menu(button("export"), Some(export), false, cx))
            .child(div().flex_1())
            .child(
                Button::new(button("delete"))
                    .ghost()
                    .small()
                    .icon(IconName::Trash)
                    .label("Delete")
                    .on_click(cx.listener(move |this, _, window, cx| this.db_delete(&delete, window, cx))),
            );
        v_flex()
            .id(SharedString::from(format!("db-pk-{id}")))
            .flex_shrink_0()
            .rounded(px(8.))
            .border_1()
            .border_color(if editing { p.accent } else { p.border })
            .bg(p.surface)
            .child(head)
            .child(actions)
            .into_any_element()
    }

    /// "Share package (.zip)" and "Export game files (.zip)" for `id`, or for the editing package.
    pub(crate) fn db_export_menu(
        &self,
        name: SharedString,
        id: Option<String>,
        header: bool,
        cx: &mut Context<Self>,
    ) -> impl IntoElement {
        let shell = cx.entity().downgrade();
        let share = id.clone().or_else(|| self.db.editing.clone());
        let anything = share.is_some() || !self.db_stack().is_empty();
        Button::new(name)
            .map(|b| if header { b.outline() } else { b.ghost().small() })
            .icon(if header {
                IconName::FileUp
            } else {
                IconName::FileArchive
            })
            .label("Export")
            .dropdown_caret(true)
            .disabled(!anything || self.db.catalog().is_none())
            .dropdown_menu_with_anchor(
                if header { Anchor::TopRight } else { Anchor::TopLeft },
                move |menu, _, _| {
                    let (s1, s2, share, game) = (shell.clone(), shell.clone(), share.clone(), id.clone());
                    menu.min_w(px(240.))
                        .item(
                            PopupMenuItem::new("Share package (.zip)…")
                                .icon(IconName::Package)
                                .disabled(share.is_none())
                                .on_click(move |_, window, cx| {
                                    if let Some(id) = share.clone() {
                                        s1.update(cx, |s, cx| s.db_export_package(id, window, cx)).ok();
                                    }
                                }),
                        )
                        .item(
                            PopupMenuItem::new("Export game files (.zip)…")
                                .icon(IconName::FileArchive)
                                .on_click(move |_, window, cx| {
                                    s2.update(cx, |s, cx| s.db_game_export(game.clone(), window, cx))
                                        .ok();
                                }),
                        )
                },
            )
    }

    fn db_turn_off_all(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let profile = self.library.profile.name.clone();
        match guarded(|| package::turn_off_all(&self.store, &profile).map_err(text)) {
            Ok(n) => window.push_notification(
                Notification::success(format!(
                    "Turned off {} in {profile}",
                    plural(n, "package", "packages")
                )),
                cx,
            ),
            Err(e) => window.push_notification(Notification::error(e).title("Could not turn them off"), cx),
        }
        self.reload(cx);
    }

    pub(crate) fn db_move_package(
        &mut self,
        id: &str,
        up: bool,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let stack = self.db_stack();
        let Some(i) = stack.iter().position(|s| s == id) else {
            return;
        };
        let Some(other) = (if up { i.checked_sub(1) } else { Some(i + 1) }).and_then(|j| stack.get(j)) else {
            return;
        };
        let addons = &self.library.profile.addons;
        let at = |x: &str| addons.iter().position(|a| a.id == x);
        let (Some(a), Some(b)) = (at(id), at(other)) else {
            return;
        };
        let (id, delta) = (id.to_owned(), b as isize - a as isize);
        self.update_profile(window, cx, |p| p.move_addon(&id, delta));
    }

    pub(crate) fn db_edit_package(&mut self, id: &str, cx: &mut Context<Self>) {
        self.db_select(Some(id.to_owned()), cx);
        self.go(Page::Database, cx);
    }

    fn db_form(&self, info: Option<&PackageInfo>, window: &mut Window, cx: &mut Context<Self>) -> Form {
        let mut input = |value: String, placeholder: &'static str, cx: &mut Context<Self>| {
            cx.new(|cx| {
                InputState::new(window, cx)
                    .placeholder(placeholder)
                    .default_value(value)
            })
        };
        Form {
            name: input(
                info.map(|i| i.name.clone()).unwrap_or_default(),
                "Faster pit stops",
                cx,
            ),
            version: info.map(|i| input(i.version.clone(), "1.0.0", cx)),
            author: input(
                info.map_or_else(|| default_author(&self.store), |i| i.author.clone()),
                "Your name",
                cx,
            ),
            description: input(
                info.map(|i| i.description.clone()).unwrap_or_default(),
                "What the package changes",
                cx,
            ),
        }
    }

    pub(crate) fn db_new_package(&mut self, window: &mut Window, cx: &mut Context<Self>) {
        let form = self.db_form(None, window, cx);
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let focus = Cell::new(true);
        let on_in = (!self.vanilla()).then(|| self.library.profile.name.clone());
        window.open_alert_dialog(cx, move |alert, window, cx| {
            if focus.replace(false) {
                form.name.update(cx, |s, cx| s.focus(window, cx));
            }
            let note = match &on_in {
                Some(profile) => format!("It is turned on in {profile} and opened for editing."),
                None => "It opens for editing. Turn it on in a profile to use it in the game.".to_owned(),
            };
            let (shell, fields) = (shell.clone(), form.clone());
            alert
                .width(px(520.))
                .title("New package")
                .child(
                    v_flex()
                        .gap(px(14.))
                        .child(div().text_size(px(13.)).text_color(p.text_muted).child(note))
                        .child(field("NAME", &form.name, p))
                        .child(field("AUTHOR", &form.author, p))
                        .child(field("DESCRIPTION", &form.description, p)),
                )
                .ok_text("Create package")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let [name, _, author, description] = fields.read(cx);
                    if name.is_empty() {
                        window.push_notification(Notification::error("Give the package a name."), cx);
                        return false;
                    }
                    shell
                        .update(cx, |s, cx| s.db_create(&name, &author, &description, window, cx))
                        .unwrap_or(false)
                })
        });
    }

    fn db_create(
        &mut self,
        name: &str,
        author: &str,
        description: &str,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) -> bool {
        let Some(catalog) = self.db.catalog().cloned() else {
            return false;
        };
        let store = self.store.clone();
        let made = guarded(|| {
            let mut pkg = Package::create(&store, name).map_err(text)?;
            let version = pkg.manifest.version.to_string();
            pkg.set_details(name, &version, author, description)
                .map_err(text)?;
            pkg.save(&store, &catalog).map_err(text)?;
            Ok(pkg)
        });
        let pkg = match made {
            Ok(p) => p,
            Err(e) => {
                window.push_notification(Notification::error(e).title("Could not create the package"), cx);
                return false;
            }
        };
        let profile = self.library.profile.name.clone();
        let on = !self.vanilla() && self.db_turn_on(&pkg.id).is_ok();
        window.push_notification(
            Notification::success(if on {
                format!("Created {name} and turned it on in {profile}")
            } else {
                format!("Created {name}")
            }),
            cx,
        );
        self.db_select(Some(pkg.id), cx);
        self.reload(cx);
        true
    }

    pub(crate) fn db_details(&mut self, id: &str, window: &mut Window, cx: &mut Context<Self>) {
        let Some(info) = self.db_package(id).cloned() else {
            return;
        };
        let form = self.db_form(Some(&info), window, cx);
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        let id = id.to_owned();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, fields, id) = (shell.clone(), form.clone(), id.clone());
            alert
                .width(px(520.))
                .title(format!("{} details", info.name))
                .child(
                    v_flex()
                        .gap(px(14.))
                        .child(field("NAME", &form.name, p))
                        .child(
                            h_flex()
                                .gap(px(12.))
                                .child(
                                    div()
                                        .w(px(140.))
                                        .children(form.version.as_ref().map(|v| field("VERSION", v, p))),
                                )
                                .child(div().flex_1().child(field("AUTHOR", &form.author, p))),
                        )
                        .child(field("DESCRIPTION", &form.description, p))
                        .child(
                            div()
                                .text_size(px(12.))
                                .text_color(p.text_placeholder)
                                .child(format!("ID {id} · versions look like 1.2.0")),
                        ),
                )
                .ok_text("Save")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    let [name, version, author, description] = fields.read(cx);
                    let result = shell
                        .update(cx, |s, cx| {
                            let r = s.db_update_package(&id, |pkg| {
                                pkg.set_details(&name, &version, &author, &description)
                            });
                            if r.is_ok() {
                                s.reload(cx);
                            }
                            r
                        })
                        .unwrap_or_else(|_| Err("The launcher is closing".into()));
                    match result {
                        Ok(()) => {
                            window.push_notification(Notification::success(format!("Saved {name}")), cx);
                            true
                        }
                        Err(e) => {
                            window.push_notification(
                                Notification::error(e).title("Could not save the details"),
                                cx,
                            );
                            false
                        }
                    }
                })
        });
    }

    pub(crate) fn db_duplicate(&mut self, id: &str, window: &mut Window, cx: &mut Context<Self>) {
        let Some(info) = self.db_package(id).cloned() else {
            return;
        };
        if self.db.editing.as_deref() == Some(id) {
            self.db_save();
        }
        let store = self.store.clone();
        let name = format!("{} copy", info.name);
        match guarded(|| Package::duplicate(&store, id, &name).map_err(text)) {
            Ok(pkg) => {
                window.push_notification(
                    Notification::success(format!("Duplicated {} as {}", info.name, pkg.manifest.name)),
                    cx,
                );
                self.reload(cx);
            }
            Err(e) => window.push_notification(Notification::error(e).title("Could not duplicate"), cx),
        }
    }

    pub(crate) fn db_delete(&mut self, id: &str, window: &mut Window, cx: &mut Context<Self>) {
        let Some(info) = self.db_package(id).cloned() else {
            return;
        };
        let shell = cx.entity().downgrade();
        let store = self.store.clone();
        let id = id.to_owned();
        window.open_alert_dialog(cx, move |alert, _, _| {
            let (shell, store, id, name) = (shell.clone(), store.clone(), id.clone(), info.name.clone());
            alert
                .title(format!("Delete {name}?"))
                .description(format!(
                    "Its {} are deleted and it is taken out of every profile. This can't be undone; export it \
                     first to keep a copy.",
                    plural(info.changes, "change", "changes")
                ))
                .ok_text("Delete package")
                .ok_variant(ButtonVariant::Danger)
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    match guarded(|| Package::delete(&store, &id).map_err(text)) {
                        Ok(()) => {
                            window.push_notification(Notification::success(format!("Deleted {name}")), cx);
                            shell
                                .update(cx, |s, cx| {
                                    if s.open_addon.as_deref() == Some(id.as_str()) {
                                        s.open_addon = None;
                                    }
                                    s.reload(cx);
                                })
                                .ok();
                        }
                        Err(e) => {
                            window.push_notification(Notification::error(e).title("Could not delete"), cx)
                        }
                    }
                    true
                })
        });
    }
}

const GAME_FILES_NOTE: &str = "Players extract this .zip into the F1 25 folder. Steam's Verify integrity of game files \
                               undoes it, and Swofty Launcher won't start modded sessions while that file is \
                               replaced. Players who use Swofty Launcher should get the package .zip instead.";

fn choice_row(
    id: &'static str,
    on: bool,
    title: String,
    detail: String,
    enabled: bool,
    p: Palette,
) -> Stateful<Div> {
    h_flex()
        .id(id)
        .gap(px(10.))
        .px(px(12.))
        .py(px(9.))
        .rounded(px(6.))
        .border_1()
        .border_color(if on { p.accent } else { p.border_variant })
        .when(on, |d| d.bg(p.accent.opacity(0.08)))
        .when(enabled, |d| d.cursor_pointer())
        .when(!enabled, |d| d.opacity(0.5))
        .child(
            Icon::new(if on {
                IconName::CircleCheck
            } else {
                IconName::Circle
            })
            .size(px(15.))
            .text_color(if on { p.accent } else { p.text_placeholder }),
        )
        .child(
            v_flex()
                .flex_1()
                .min_w_0()
                .child(
                    div()
                        .text_size(px(13.))
                        .font_weight(FontWeight::MEDIUM)
                        .child(title),
                )
                .child(div().text_size(px(12.)).text_color(p.text_muted).child(detail)),
        )
}

impl Shell {
    /// Builds a game database with the chosen packages applied and saves it as a .zip of game files.
    pub(crate) fn db_game_export(&mut self, id: Option<String>, window: &mut Window, cx: &mut Context<Self>) {
        let one = id
            .or_else(|| self.db.editing.clone())
            .and_then(|id| self.db_package(&id).cloned());
        let profile = self.library.profile.name.clone();
        let stack = guarded(|| package::enabled_packages(&self.store, &profile).map_err(text))
            .unwrap_or_else(|_| self.db_stack());
        let every = Rc::new(Cell::new(one.is_none()));
        let shell = cx.entity().downgrade();
        let p = palette(cx);
        window.open_alert_dialog(cx, move |alert, _, _| {
            let all = every.get();
            let (shell, stack2, one2, every2) = (shell.clone(), stack.clone(), one.clone(), every.clone());
            let single = one.as_ref().map(|info| {
                let every = every.clone();
                choice_row(
                    "db-ge-one",
                    !all,
                    format!("Only {}", info.name),
                    plural(info.changes, "change", "changes"),
                    true,
                    p,
                )
                .on_click(move |_, window, _| {
                    every.set(false);
                    window.refresh();
                })
            });
            let stacked = {
                let every = every.clone();
                let enabled = !stack.is_empty();
                choice_row(
                    "db-ge-all",
                    all,
                    format!("Every package on in {profile}"),
                    if enabled {
                        format!("{} in load order", plural(stack.len(), "package", "packages"))
                    } else {
                        format!("No packages are on in {profile}")
                    },
                    enabled,
                    p,
                )
                .when(enabled, |d| {
                    d.on_click(move |_, window, _| {
                        every.set(true);
                        window.refresh();
                    })
                })
            };
            let ready = if all { !stack.is_empty() } else { one.is_some() };
            alert
                .width(px(540.))
                .title("Export game files")
                .child(
                    v_flex()
                        .gap(px(10.))
                        .child(div().text_size(px(13.)).text_color(p.text_muted).child(
                            "Saves a .zip with the game's database file, changed by the packages you pick.",
                        ))
                        .children(single)
                        .child(stacked)
                        .child(
                            h_flex()
                                .mt(px(4.))
                                .gap(px(8.))
                                .items_start()
                                .px(px(10.))
                                .py(px(8.))
                                .rounded(px(6.))
                                .bg(p.warning.opacity(0.1))
                                .text_size(px(12.))
                                .line_height(px(18.))
                                .child(
                                    Icon::new(IconName::TriangleAlert)
                                        .size(px(13.))
                                        .mt(px(2.))
                                        .text_color(p.warning),
                                )
                                .child(div().flex_1().min_w_0().child(GAME_FILES_NOTE)),
                        ),
                )
                .ok_text("Export…")
                .show_cancel(true)
                .on_ok(move |_, window, cx| {
                    if !ready {
                        return false;
                    }
                    let ids: Vec<String> = if every2.get() {
                        stack2.clone()
                    } else {
                        one2.iter().map(|i| i.id.clone()).collect()
                    };
                    let file = match (every2.get(), &one2) {
                        (false, Some(info)) => {
                            format!("{}-game-files.zip", info.id.trim_start_matches("dbedit."))
                        }
                        _ => "packages-game-files.zip".to_owned(),
                    };
                    shell
                        .update(cx, |s, cx| s.db_game_export_to(ids, file, window, cx))
                        .ok();
                    true
                })
        });
    }

    fn db_game_export_to(
        &mut self,
        ids: Vec<String>,
        file: String,
        window: &mut Window,
        cx: &mut Context<Self>,
    ) {
        let store = self.store.clone();
        let Some(game) = crate::sections::game(&store, self.install.as_ref()) else {
            window.push_notification(
                Notification::error("F1 25 was not found.").title("Export failed"),
                cx,
            );
            return;
        };
        let run = move |out: PathBuf, window: &mut Window, cx: &mut Context<Self>| {
            window.push_notification(Notification::info("Building the game files…"), cx);
            let (store, game, ids) = (store.clone(), game.clone(), ids.clone());
            cx.spawn_in(window, async move |_, cx| {
                let target = out.clone();
                let result = cx
                    .background_executor()
                    .spawn(async move {
                        guarded(|| package::export_game_files(&store, &game, &ids, &target).map_err(text))
                    })
                    .await;
                cx.update(|window, cx| match result {
                    Ok(done) => {
                        let mut body = format!(
                            "{} · {:.1} MB · saved to {}",
                            plural(done.changes, "change", "changes"),
                            done.bytes as f64 / 1_048_576.,
                            out.display()
                        );
                        if !done.skipped.is_empty() {
                            body.push_str(&format!("\n{} skipped:", done.skipped.len()));
                            for line in done.skipped.iter().take(4) {
                                body.push_str(&format!("\n{line}"));
                            }
                        }
                        window
                            .push_notification(Notification::success(body).title("Exported game files"), cx);
                    }
                    Err(e) => window.push_notification(Notification::error(e).title("Export failed"), cx),
                })
                .ok();
            })
            .detach();
        };
        if let Some(dir) = super::debug_path("SWOFTY_DEBUG_EXPORT") {
            run(dir.join(&file), window, cx);
            return;
        }
        let rx = cx.prompt_for_new_path(&super::home(&self.store), Some(&file));
        cx.spawn_in(window, async move |this, cx| {
            if let Ok(Ok(Some(path))) = rx.await {
                this.update_in(cx, |_, window, cx| run(path, window, cx)).ok();
            }
        })
        .detach();
    }
}
