<script lang="ts">
  // Widget d'Avenoir : une barre en haut de l'écran, qui se déplie au survol
  // (ou tant qu'une requête ou une question est en cours).
  import wolf from "./assets/wolf.svg";
  import Composer from "./components/Composer.svelte";
  import Conversation from "./components/Conversation.svelte";
  import QuestionCard from "./components/QuestionCard.svelte";
  import { session } from "./lib/session.svelte";

  let hovered = $state(false);
  let focused = $state(false);

  const busy = $derived(session.running !== null);
  // « #ouvert » garde le widget déplié : captures d'écran, débogage
  // (AVENOIR_UI_PIN pour l'hôte, ou l'adresse en développement).
  const pinned = location.hash === "#ouvert";
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
    session.question !== null ? "asking" : session.view.thinking ? "thinking" : "idle",
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
    <img class="wolf {mood}" src={wolf} alt="" />
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
  </header>
  {#if open}
    <div class="panel">
      <Conversation entries={session.view.entries} />
      {#if session.question !== null}
        <QuestionCard question={session.question} />
      {/if}
      <Composer
        {busy}
        disabled={!session.connected}
        onsend={(line) => void session.send(line)}
        onstop={() => session.stop()}
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
  .wolf {
    height: 26px;
    width: auto;
  }
  .wolf.thinking {
    animation: breathe 1.2s ease-in-out infinite;
  }
  .wolf.asking {
    animation: breathe 0.6s ease-in-out infinite;
  }
  @keyframes breathe {
    50% {
      opacity: 0.45;
    }
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
  .panel {
    display: flex;
    flex-direction: column;
    height: 420px;
    min-height: 0;
    border-top: 1px solid var(--line);
  }
  @media (prefers-reduced-motion: reduce) {
    .wolf {
      animation: none;
    }
  }
</style>
