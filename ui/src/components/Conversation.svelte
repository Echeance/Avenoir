<script lang="ts">
  import type { Entry } from "../lib/state";

  let { entries }: { entries: Entry[] } = $props();

  let list: HTMLElement | undefined = $state();

  // Reste en bas quand la conversation avance.
  $effect(() => {
    void entries.length;
    void entries.at(-1);
    list?.scrollTo({ top: list.scrollHeight });
  });
</script>

<section class="conversation" bind:this={list} aria-live="polite">
  {#each entries as entry, index (index)}
    {#if entry.kind === "user"}
      <p class="user"><span class="mark">›</span>{entry.text}</p>
    {:else if entry.kind === "assistant"}
      <p class="assistant" class:streaming={entry.streaming}>{entry.text}</p>
    {:else if entry.kind === "tool"}
      <p class="tool" class:failed={entry.failed}>
        <span class="mark">»</span>{entry.name}
        {#if entry.summary !== ""}<span class="summary">{entry.summary}</span>{/if}
        <span class="state">{entry.done ? (entry.failed ? "✗" : "✓") : "…"}</span>
      </p>
    {:else}
      <p class="note {entry.level}">{entry.text}</p>
    {/if}
  {:else}
    <p class="empty">Posez une question, ou demandez une action.</p>
  {/each}
</section>

<style>
  .conversation {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    padding: 10px 14px;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  p {
    margin: 0;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
    line-height: 1.45;
  }
  .mark {
    color: var(--azure);
    margin-right: 6px;
    font-weight: 700;
  }
  .user {
    font-weight: 600;
  }
  .assistant {
    border-left: 2px solid var(--navy);
    padding-left: 10px;
  }
  .assistant.streaming {
    border-left-color: var(--azure);
  }
  .tool {
    color: var(--muted);
    font-family: ui-monospace, "JetBrains Mono", monospace;
    font-size: 12px;
  }
  .tool .summary {
    margin-left: 6px;
  }
  .tool .state {
    margin-left: 6px;
    color: var(--green);
  }
  .tool.failed .state {
    color: var(--red);
  }
  .note {
    font-size: 12px;
    color: var(--muted);
  }
  .note.success {
    color: var(--green);
  }
  .note.warning {
    color: var(--yellow);
  }
  .note.error {
    color: var(--red);
  }
  .empty {
    color: var(--muted);
    margin: auto;
  }
</style>
