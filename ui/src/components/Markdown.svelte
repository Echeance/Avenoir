<script lang="ts">
  import { parseMarkdown } from "../lib/markdown";
  import InlineText from "./InlineText.svelte";

  let { text }: { text: string } = $props();

  const blocks = $derived(parseMarkdown(text));
</script>

<div class="markdown">
  {#each blocks as block, index (index)}
    {#if block.kind === "paragraph"}
      <p><InlineText parts={block.inline} /></p>
    {:else if block.kind === "heading"}
      <p class="heading level-{Math.min(block.level, 3)}"><InlineText parts={block.inline} /></p>
    {:else if block.kind === "list"}
      {#if block.ordered}
        <ol>
          {#each block.items as item, row (row)}<li><InlineText parts={item} /></li>{/each}
        </ol>
      {:else}
        <ul>
          {#each block.items as item, row (row)}<li><InlineText parts={item} /></li>{/each}
        </ul>
      {/if}
    {:else if block.kind === "quote"}
      <blockquote><InlineText parts={block.inline} /></blockquote>
    {:else if block.kind === "code"}
      <pre><code>{block.text}</code></pre>
    {:else if block.kind === "rule"}
      <hr />
    {:else}
      <table>
        <thead>
          <tr>{#each block.header as cell, column (column)}<th><InlineText parts={cell} /></th>{/each}</tr>
        </thead>
        <tbody>
          {#each block.rows as cells, row (row)}
            <tr>{#each cells as cell, column (column)}<td><InlineText parts={cell} /></td>{/each}</tr>
          {/each}
        </tbody>
      </table>
    {/if}
  {/each}
</div>

<style>
  .markdown {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }
  p,
  ul,
  ol,
  blockquote,
  pre {
    margin: 0;
  }
  p {
    white-space: pre-wrap;
  }
  .heading {
    font-weight: 700;
    color: var(--ice);
  }
  .level-1 {
    font-size: 1.15em;
  }
  ul,
  ol {
    padding-left: 20px;
  }
  blockquote {
    border-left: 2px solid var(--line);
    padding-left: 8px;
    color: var(--muted);
    white-space: pre-wrap;
  }
  pre {
    background: var(--night);
    border-radius: 6px;
    padding: 8px 10px;
    overflow-x: auto;
    font-size: 12px;
  }
  pre code {
    font-family: ui-monospace, "JetBrains Mono", monospace;
  }
  hr {
    border: 0;
    border-top: 1px solid var(--line);
    width: 100%;
  }
  table {
    border-collapse: collapse;
    font-size: 12px;
  }
  th,
  td {
    border: 1px solid var(--line);
    padding: 3px 8px;
    text-align: left;
  }
  th {
    color: var(--ice);
  }
</style>
