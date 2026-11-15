<script lang="ts">
  import type { Inline } from "../lib/markdown";
  import InlineText from "./InlineText.svelte";

  let { parts }: { parts: Inline[] } = $props();
</script>

{#each parts as part, index (index)}{#if part.kind === "text"}{part.text}{:else if part.kind === "strong"}<strong
      ><InlineText parts={part.children} /></strong
    >{:else if part.kind === "em"}<em><InlineText parts={part.children} /></em
    >{:else if part.kind === "code"}<code>{part.text}</code>{:else}<span class="link"
      ><InlineText parts={part.children} /><span class="url">{part.url}</span></span
    >{/if}{/each}

<style>
  code {
    font-family: ui-monospace, "JetBrains Mono", monospace;
    font-size: 0.92em;
    background: var(--night);
    border-radius: 4px;
    padding: 0 4px;
  }
  .link {
    color: var(--sky);
    text-decoration: underline;
    text-decoration-color: var(--line);
  }
  .url {
    color: var(--muted);
    font-size: 0.85em;
    margin-left: 4px;
    text-decoration: none;
  }
  strong {
    color: var(--ice);
  }
</style>
