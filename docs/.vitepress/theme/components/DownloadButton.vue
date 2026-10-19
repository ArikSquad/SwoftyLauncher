<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from "vue";
import Icon from "./Icon.vue";
import { useLinks } from "../links";

type Os = "windows" | "linux" | "other";

const props = defineProps<{
  windows: string;
  linux: string;
  both: string;
  manual: string;
}>();

const { to } = useLinks();

const menuId = "lp-download-menu";
const noteId = "lp-download-note";

const os = ref<Os | null>(null);
const open = ref(false);
const root = ref<HTMLElement>();
const menu = ref<HTMLElement>();
let opener: HTMLElement | null = null;

const labels = [
  { id: "none", icon: "download", text: "Download" },
  { id: "windows", icon: "windows", text: "Download for Windows" },
  { id: "linux", icon: "linux", text: "Download for Linux" },
] as const;

const current = computed(() => (os.value === "windows" || os.value === "linux" ? os.value : "none"));
const direct = computed(() => (current.value === "none" ? null : props[current.value]));

const fileName = (link: string) => link.split("/").pop();

const choices = computed(() => [
  { id: "windows", icon: "windows", text: "Windows", sub: fileName(props.windows), link: props.windows },
  { id: "linux", icon: "linux", text: "Linux & Steam Deck", sub: fileName(props.linux), link: props.linux },
  { id: "both", icon: "box", text: "Both (zip)", sub: fileName(props.both), link: props.both },
]);

const MOBILE = /android|iphone|ipad|ipod|cros/i;
const OTHER = /mac|darwin|\bios\b|chrome ?os/i;

function detect(): Os {
  const nav = navigator as Navigator & { userAgentData?: { platform?: string } };
  const ua = nav.userAgent ?? "";
  if (MOBILE.test(ua)) return "other";
  for (const hint of [nav.userAgentData?.platform, nav.platform, ua]) {
    if (!hint) continue;
    if (MOBILE.test(hint) || OTHER.test(hint)) return "other";
    if (/win/i.test(hint)) return "windows";
    if (/linux|x11|steamos/i.test(hint)) return "linux";
  }
  return "other";
}

function items() {
  return Array.from(menu.value?.querySelectorAll<HTMLElement>('[role="menuitem"]') ?? []);
}

async function show(from: HTMLElement, focus: "first" | "last") {
  opener = from;
  open.value = true;
  await nextTick();
  const list = items();
  (focus === "first" ? list[0] : list[list.length - 1])?.focus();
}

function close(restore = false) {
  if (!open.value) return;
  open.value = false;
  if (restore) opener?.focus();
}

function toggle(event: MouseEvent) {
  if (open.value) close();
  else show(event.currentTarget as HTMLElement, "first");
}

function onMain(event: MouseEvent) {
  if (!direct.value) toggle(event);
}

function onTriggerKey(event: KeyboardEvent) {
  const target = event.currentTarget as HTMLElement;
  if (target.tagName === "A") return;
  if (event.key === "ArrowDown") {
    event.preventDefault();
    show(target, "first");
  } else if (event.key === "ArrowUp") {
    event.preventDefault();
    show(target, "last");
  }
}

function onMenuKey(event: KeyboardEvent) {
  const list = items();
  const at = list.indexOf(document.activeElement as HTMLElement);
  const move = (i: number) => {
    event.preventDefault();
    list[(i + list.length) % list.length]?.focus();
  };
  switch (event.key) {
    case "ArrowDown":
      return move(at + 1);
    case "ArrowUp":
      return move(at - 1);
    case "Home":
      return move(0);
    case "End":
      return move(list.length - 1);
    case " ":
      event.preventDefault();
      return list[at]?.click();
    case "Tab":
      return close();
  }
}

function onEscape() {
  close(true);
}

function onOutside(event: Event) {
  if (open.value && !root.value?.contains(event.target as Node)) close();
}

onMounted(() => {
  os.value = detect();
  document.addEventListener("pointerdown", onOutside);
  document.addEventListener("focusin", onOutside);
});

onBeforeUnmount(() => {
  document.removeEventListener("pointerdown", onOutside);
  document.removeEventListener("focusin", onOutside);
});
</script>

<template>
  <div ref="root" class="dl" :class="{ 'dl--open': open }" @keydown.esc="onEscape">
    <div class="dl-split">
      <component
        :is="direct ? 'a' : 'button'"
        class="lp-btn lp-btn--ink lp-btn--big dl-main"
        :href="direct ?? undefined"
        :rel="direct ? 'noopener' : undefined"
        :type="direct ? undefined : 'button'"
        :aria-haspopup="direct ? undefined : 'menu'"
        :aria-expanded="direct ? undefined : open"
        :aria-controls="direct ? undefined : menuId"
        @click="onMain"
        @keydown="onTriggerKey"
      >
        <span class="dl-labels">
          <span v-for="l in labels" :key="l.id" class="dl-label" :class="{ 'dl-label--on': l.id === current }">
            <Icon :name="l.icon" /><span>{{ l.text }}</span>
          </span>
        </span>
      </component>
      <button
        type="button"
        class="lp-btn lp-btn--ink lp-btn--big dl-chevron"
        aria-label="Choose a download"
        aria-haspopup="menu"
        :aria-expanded="open"
        :aria-controls="menuId"
        @click="toggle"
        @keydown="onTriggerKey"
      >
        <Icon name="chevron" />
      </button>
    </div>

    <Transition name="dl-pop">
      <div v-show="open" class="dl-pop">
        <p v-if="!direct" :id="noteId" class="dl-note">
          <Icon name="alert" /><span>Swofty Launcher runs on Windows and Linux.</span>
        </p>
        <ul
          :id="menuId"
          ref="menu"
          class="dl-menu"
          role="menu"
          aria-label="Downloads"
          :aria-describedby="direct ? undefined : noteId"
          @keydown="onMenuKey"
        >
          <li v-for="c in choices" :key="c.id" role="none">
            <a class="dl-item" role="menuitem" tabindex="-1" :href="c.link" rel="noopener" @click="close()">
              <span class="dl-item-icon"><Icon :name="c.icon" /></span>
              <span class="dl-item-text">
                <span class="dl-item-title">
                  {{ c.text }}<span v-if="c.id === os" class="dl-badge">Detected</span>
                </span>
                <span class="dl-item-sub">{{ c.sub }}</span>
              </span>
              <Icon name="download" class="dl-item-go" />
            </a>
          </li>
          <li class="dl-sep" role="separator" />
          <li role="none">
            <a class="dl-item" role="menuitem" tabindex="-1" :href="to(manual)" @click="close()">
              <span class="dl-item-icon"><Icon name="book" /></span>
              <span class="dl-item-text">
                <span class="dl-item-title">Manual install</span>
                <span class="dl-item-sub dl-item-sub--plain">Release archives and setup steps</span>
              </span>
              <Icon name="arrow" class="dl-item-go" />
            </a>
          </li>
        </ul>
      </div>
    </Transition>
  </div>
</template>
