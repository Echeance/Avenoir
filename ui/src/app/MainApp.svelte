<script lang="ts">
  // Application principale (décision 72) : une barre latérale et une vue par
  // sujet. Toute modification passe par les commandes d'Avenoir (/reglage,
  // /revoquer…), avec leurs confirmations et leur audit ; les données
  // affichées viennent des requêtes en lecture seule du démon.
  import Composer from "../components/Composer.svelte";
  import Conversation from "../components/Conversation.svelte";
  import QuestionCard from "../components/QuestionCard.svelte";
  import Wolf from "../components/Wolf.svelte";
  import { session } from "../lib/session.svelte";
  import Conversations from "./Conversations.svelte";
  import Journal from "./Journal.svelte";
  import Models from "./Models.svelte";
  import Modules from "./Modules.svelte";
  import Permissions from "./Permissions.svelte";
  import Settings from "./Settings.svelte";

  type View = "conversation" | "settings" | "journal" | "permissions" | "models" | "modules";
  const views: { id: View; label: string }[] = [
    { id: "conversation", label: "Conversation" },
    { id: "settings", label: "Réglages" },
    { id: "journal", label: "Journal" },
    { id: "permissions", label: "Permissions" },
    { id: "models", label: "Modèles" },
    { id: "modules", label: "Modules" },
  ];
  // Vue de départ : « #app/journal »… (AVENOIR_UI_VIEW pour l'hôte).
  const wanted = location.hash.slice("#app/".length);
  let current = $state<View>(views.find((view) => view.id === wanted)?.id ?? "conversation");

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

<div class="app">
  <nav>
    <div class="brand"><Wolf {mood} size={34} /><span>Avenoir</span></div>
    {#each views as view (view.id)}
      <button class:active={current === view.id} onclick={() => (current = view.id)}>{view.label}</button>
    {/each}
    <footer>
      <span class="model">{session.view.llm}</span>
      <span class="status" class:offline={!session.connected}>
        {session.connected ? (session.view.thinking ? session.view.activity + "…" : "connecté") : session.status}
      </span>
    </footer>
  </nav>
  <main>
    {#if current === "conversation"}
      <div class="chat">
        <Conversations />
        <div class="thread">
          <Conversation entries={session.view.entries} />
          <Composer
            busy={session.running !== null}
            disabled={!session.connected}
            onsend={(line) => void session.send(line)}
            onstop={() => session.stop()}
          />
        </div>
      </div>
    {:else if current === "settings"}
      <Settings />
    {:else if current === "journal"}
      <Journal />
    {:else if current === "permissions"}
      <Permissions />
    {:else if current === "models"}
      <Models />
    {:else}
      <Modules />
    {/if}
    {#if current !== "conversation" && session.lastNotes.length > 0}
      <aside class="notes" aria-live="polite">
        {#each session.lastNotes as note, index (index)}
          <p class={note.level}>{note.text}</p>
        {/each}
      </aside>
    {/if}
  </main>
  {#if session.question !== null}
    <div class="overlay">
      <div class="dialog"><QuestionCard question={session.question} /></div>
    </div>
  {/if}
</div>

<style>
  .app {
    display: grid;
    grid-template-columns: 200px 1fr;
    height: 100%;
  }
  nav {
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 12px 8px;
    background: var(--panel);
    border-right: 1px solid var(--line);
  }
  .brand {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 4px 8px 14px;
    color: var(--ice);
    font-weight: 700;
    letter-spacing: 0.08em;
  }
  nav button {
    text-align: left;
    background: transparent;
    border: 0;
    border-radius: 6px;
    padding: 7px 10px;
    color: var(--text);
  }
  nav button:hover {
    background: var(--line);
  }
  nav button.active {
    background: var(--navy);
  }
  footer {
    margin-top: auto;
    display: flex;
    flex-direction: column;
    gap: 2px;
    padding: 8px;
    font-size: 11px;
    color: var(--muted);
    overflow-wrap: anywhere;
  }
  .status {
    color: var(--green);
  }
  .status.offline {
    color: var(--red);
  }
  main {
    position: relative;
    min-width: 0;
    min-height: 0;
    overflow-y: auto;
  }
  .chat {
    display: grid;
    grid-template-columns: 240px 1fr;
    height: 100%;
  }
  .thread {
    display: flex;
    flex-direction: column;
    min-height: 0;
    min-width: 0;
  }
  .notes {
    position: sticky;
    bottom: 0;
    margin: 0 16px 16px;
    padding: 8px 12px;
    background: var(--panel);
    border: 1px solid var(--line);
    border-radius: 8px;
    font-size: 12px;
  }
  .notes p {
    margin: 0;
    white-space: pre-wrap;
  }
  .notes .success {
    color: var(--green);
  }
  .notes .warning {
    color: var(--yellow);
  }
  .notes .error {
    color: var(--red);
  }
  .overlay {
    position: fixed;
    inset: 0;
    background: rgb(0 0 0 / 50%);
    display: grid;
    place-items: center;
  }
  .dialog {
    width: min(560px, 90vw);
  }
</style>
