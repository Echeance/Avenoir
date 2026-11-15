<script lang="ts">
  import type { Snippet } from "svelte";

  let {
    title,
    subtitle = "",
    error = "",
    onrefresh,
    children,
  }: { title: string; subtitle?: string; error?: string; onrefresh?: () => void; children: Snippet } = $props();
</script>

<section>
  <header>
    <h1>{title}</h1>
    {#if onrefresh}<button onclick={onrefresh} title="Relire">↻</button>{/if}
  </header>
  {#if subtitle !== ""}<p class="subtitle">{subtitle}</p>{/if}
  {#if error !== ""}<p class="error">{error}</p>{/if}
  {@render children()}
</section>

<style>
  section {
    padding: 18px 24px 24px;
    max-width: 920px;
  }
  header {
    display: flex;
    align-items: center;
    gap: 10px;
  }
  h1 {
    margin: 0;
    font-size: 18px;
    color: var(--ice);
  }
  header button {
    background: transparent;
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 0 8px;
  }
  .subtitle {
    color: var(--muted);
    margin: 4px 0 14px;
    font-size: 13px;
  }
  .error {
    color: var(--red);
  }
  section :global(table) {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
  }
  section :global(th) {
    text-align: left;
    color: var(--muted);
    font-weight: 500;
    border-bottom: 1px solid var(--line);
    padding: 6px 8px;
  }
  section :global(td) {
    border-bottom: 1px solid var(--line);
    padding: 6px 8px;
    vertical-align: top;
  }
  section :global(.action) {
    background: transparent;
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 2px 10px;
    font-size: 12px;
    white-space: nowrap;
  }
  section :global(.action:hover) {
    border-color: var(--azure);
  }
  section :global(.action.danger:hover) {
    border-color: var(--red);
  }
  section :global(.muted) {
    color: var(--muted);
  }
  section :global(h2) {
    font-size: 14px;
    color: var(--sky);
    margin: 22px 0 8px;
  }
</style>
