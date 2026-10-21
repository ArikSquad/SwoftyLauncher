---
title: In-game text
description: Show your own names and descriptions from a data addon, with stock tags, literal text and lang operations that add strings while the game boots.
---

# In-game text

In-game text is keyed by tags such as `lng_team_tor_hashtag`. Database fields such as `name_loc_tag` and `desc_loc_tag` tell the game which tag to show. The text itself lives in the game's language files (`.lng`), one per language.

You have two ways to change that text from a [data addon](./): point a field at a tag the game already has, or add your own string with a `lang` operation.

## 1. Point a field at a stock tag

If the wording you want is already in the game, set a `_loc_tag` field to its tag. You get that text in every language without adding a string:

```json
{ "op": "set",
  "select": "customisation.custom_se_liveries.se_liveries.se_livery[_metadata_=toro_rosso_rm_alphatauri_2026]",
  "field": "name_loc_tag", "value": "lng_team_tor_hashtag" }
```

To offer a choice of tags, use a `choice` setting. The [livery example](./livery#_1-the-manifest) does this with `picker_name`.

## 2. Add your own string

Put your `lang` operations in an `operations.assets` file. swofty_core adds the strings while the game boots, and the game's language files stay stock.

| Op | Fields | Change |
|---|---|---|
| `lang.set` | `language` (a code, or `*` for every language), `key`, `value` | Set one string |
| `lang.table` | `language`, `source` (a `.lng` file in the addon) | Set every string in a table |
| `lang.default` | `language` (a code, or `*`), `key`, `value` | Set one string unless an addon sets it |

```json [assets/text.json]
{
  "schema_version": 1,
  "ops": [
    { "op": "lang.set", "language": "*", "key": "lng_my_track_title", "value": "Test Ring" },
    { "op": "lang.set", "language": "eng", "key": "lng_my_track_desc", "value": "A private test circuit." }
  ]
}
```

List the file in the manifest:

```json [swofty.json]
"operations": {
  "assets": ["assets/text.json"]
}
```

A key the game doesn't have gets added. Point a `_loc_tag` field at it to show your text. When two addons set the same key, the one later in the load order wins and the build prints the conflict.

`lang.default` applies before every addon's `lang.set` and `lang.table`, whatever the load order. A `lang.set` or `lang.table` entry for the same language and key wins over it, including one from an addon earlier in the load order. When two addons give a default for the same language and key, the later one in the load order wins. Defaults never count as conflicts.

```json
{ "op": "lang.default", "language": "*", "key": "lng_my_track_title", "value": "Test Ring" }
```

`.serps` mods with language files add strings the same way: the importer turns every changed or new key into a `lang.set` ([`.serps` packages](/reference/mods/serps#what-the-importer-applies)).

## 3. Name a track

Menus read a track's name from `TrackNameData_0` on its `TrackData` row in each dataset, not from the track row. Set that field to your key ([Rename a track](./database#_4-rename-a-track)), then give the key a string with `lang.set`. For a track your addon ships, see [A track with its own name](./track-names).

## 4. Build and check

```sh
swofty build
```

`lang` is a runtime-only kind: swofty_core adds the strings when the game loads a language file. The build merges every addon's strings into one table per language under `swofty/assets/lang/` and prints `runtime: swofty/assets.json with ... <n> language table(s)`. Without the in-game loader, the build lists your `lang` operations as waiting and changes no text.

## Reference

- [Operations](./operations#asset-op-files): every prefixed op an `assets` file takes.
- [Changes at runtime](/reference/addons/runtime-assets#add-or-change-strings): how swofty_core adds the strings.
- [Deliveries](/reference/addons/asset-api#deliveries): which route each kind takes.
