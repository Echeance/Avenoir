<script lang="ts">
  // Widget d'Avenoir : une barre en haut de l'écran, qui se déplie au survol
  // (ou tant qu'une requête ou une question est en cours).
  import ActivityTab from "./ActivityTab.svelte";
  import Composer from "./Composer.svelte";
  import SystemTab from "./SystemTab.svelte";
  import Conversation from "./Conversation.svelte";
  import QuestionCard from "./QuestionCard.svelte";
  import TabView from "./TabView.svelte";
  import WebTab from "./WebTab.svelte";
  import Wolf from "./Wolf.svelte";
  import { session } from "../lib/session.svelte";

  let hovered = $state(false);
  /// Onglets d'Avenoir, avant ceux des modules (décision 78).
  const BUILTIN = [
    { key: "conversation", label: "Conversation" },
    { key: "activite", label: "Activité" },
    { key: "systeme", label: "Système" },
  ];
  /// Onglet affiché : la conversation, l'activité, le système, ou « module/onglet ».
  let current = $state("conversation");
  // Onglets des modules : déclaratifs, et web (iframe isolée) là où la vue
  // web les sert.
  const declarative = $derived(
    session.tabs.filter((tab) => tab.kind === "declarative" || session.moduleUrl(tab.module) !== null),
  );
  const shown = $derived(declarative.find((tab) => `${tab.module}/${tab.id}` === current));
  // Débogage : « #ouvert/meteo » ouvre le premier onglet de ce module.
  const wantedModule = location.hash.startsWith("#ouvert/") ? location.hash.slice("#ouvert/".length) : "";
  let chosen = false;
  if (wantedModule === "activite" || wantedModule === "systeme") {
    current = wantedModule;
    chosen = true;
  }
  $effect(() => {
    const first = declarative.find((tab) => tab.module === wantedModule);
    if (!chosen && first !== undefined) {
      chosen = true;
      current = `${first.module}/${first.id}`;
    }
  });
  let focused = $state(false);

  const busy = $derived(session.running !== null);
  // « #ouvert » garde le widget déplié : captures d'écran, débogage
  // (AVENOIR_UI_PIN pour l'hôte, ou l'adresse en développement).
  const pinned = location.hash.startsWith("#ouvert");
  const open = $derived(pinned || hovered || focused || busy || session.question !== null);
  let widget: HTMLElement | undefined = $state();

  // La fenêtre de l'hôte suit la hauteur du widget (barre pliée ou dépliée).
  $effect(() => {
    if (widget === undefined) {
      return;
    }
    const observer = new ResizeObserver(() => {
      if (widget !== undefined) {
        session.resize(widget.getBoundingClientRect().height);
      }
    });
    observer.observe(widget);
    return () => observer.disconnect();
  });

  const mood = $derived(
    !session.connected
      ? "offline"
      : session.question !== null
        ? "asking"
        : session.view.thinking
          ? "thinking"
          : "idle",
  );
</script>

<main
  bind:this={widget}
  class="widget"
  class:open
  onmouseenter={() => (hovered = true)}
  onmouseleave={() => (hovered = false)}
  onfocusin={() => (focused = true)}
  onfocusout={() => (focused = false)}
>
  <header class="bar">
    <Wolf {mood} />
    <span class="title">Avenoir</span>
    <span class="status">
      {#if !session.connected}
        {session.status}
      {:else if session.view.thinking}
        {session.view.activity}…
      {:else}
        {session.view.llm}
      {/if}
    </span>
    <button class="launch" title="Ouvrir Avenoir (réglages, journal, permissions…)" onclick={() => session.openApp()}
      >⧉</button
    >
  </header>
  {#if open}
    <div class="panel">
      <div class="tabs" role="tablist">
        {#each BUILTIN as { key, label } (key)}
          <button role="tab" aria-selected={current === key} class:active={current === key} onclick={() => (current = key)}
            >{label}</button
          >
        {/each}
        {#each declarative as tab (`${tab.module}/${tab.id}`)}
          {@const key = `${tab.module}/${tab.id}`}
          <button role="tab" aria-selected={current === key} class:active={current === key} onclick={() => (current = key)}
            >{tab.title}</button
          >
        {/each}
      </div>
      {#if current === "activite"}
        <ActivityTab />
      {:else if current === "systeme"}
        <SystemTab />
      {:else if shown !== undefined && shown.kind === "web"}
        <WebTab module={shown.module} tab={shown.id} title={shown.title} />
      {:else if shown !== undefined}
        <TabView module={shown.module} tab={shown.id} onaction={() => (current = "conversation")} />
      {:else}
        <Conversation entries={session.view.entries} />
      {/if}
      {#if session.question !== null}
        <QuestionCard question={session.question} />
      {/if}
      <Composer
        {busy}
        disabled={!session.connected}
        onsend={(line) => void session.send(line)}
        onstop={() => session.stop()}
        undoable={session.undoable > 0}
        onundo={() => void session.send("/annuler")}
      />
    </div>
  {/if}
</main>

<style>
  .widget {
    width: 100%;
    display: flex;
    flex-direction: column;
    background: color-mix(in srgb, var(--panel) 94%, transparent);
    border: 1px solid var(--line);
    border-top: 0;
    border-radius: 0 0 14px 14px;
    overflow: hidden;
  }
  .bar {
    display: flex;
    align-items: center;
    gap: 10px;
    height: 36px;
    padding: 0 14px;
    flex: none;
  }
  .title {
    color: var(--ice);
    font-weight: 700;
    letter-spacing: 0.08em;
  }
  .status {
    margin-left: auto;
    color: var(--muted);
    font-size: 12px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .launch {
    flex: none;
    background: transparent;
    border: 0;
    color: var(--muted);
    font-size: 15px;
    padding: 0 2px;
  }
  .launch:hover {
    color: var(--ice);
  }
  .tabs {
    display: flex;
    gap: 2px;
    padding: 4px 10px 0;
    border-bottom: 1px solid var(--line);
    flex: none;
  }
  .tabs button {
    background: transparent;
    border: 0;
    border-bottom: 2px solid transparent;
    padding: 4px 10px;
    color: var(--muted);
    font-size: 12px;
  }
  .tabs button.active {
    color: var(--ice);
    border-bottom-color: var(--azure);
  }
  .panel {
    display: flex;
    flex-direction: column;
    height: 420px;
    min-height: 0;
    border-top: 1px solid var(--line);
  }
</style>
