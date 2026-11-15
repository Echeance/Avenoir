<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, str } from "./report.svelte";

  const report = new Report("conversations/list");
  $effect(() => {
    // Relue au changement de conversation (titre affiché par le démon).
    void session.view.conversation;
    if (session.connected) {
      void report.load();
    }
  });
</script>

<aside>
  <button class="new" onclick={() => void report.run("/nouvelle")}>+ Nouvelle conversation</button>
  <ul>
    {#each list(report.data?.["conversations"]) as conversation (str(conversation["id"]))}
      {@const id = str(conversation["id"])}
      <li>
        <button class:current={str(report.data?.["current"]) === id} onclick={() => void report.run(`/ouvrir ${id}`)}>
          <span class="title">{str(conversation["title"]) || "Sans titre"}</span>
          <span class="date">{str(conversation["updated_at"])} · {str(conversation["messages"])} messages</span>
        </button>
      </li>
    {/each}
  </ul>
</aside>

<style>
  aside {
    border-right: 1px solid var(--line);
    overflow-y: auto;
    padding: 10px 8px;
    min-height: 0;
  }
  .new {
    width: 100%;
    background: var(--navy);
    border: 0;
    border-radius: 6px;
    padding: 6px;
    margin-bottom: 8px;
  }
  ul {
    list-style: none;
    margin: 0;
    padding: 0;
  }
  li button {
    width: 100%;
    text-align: left;
    background: transparent;
    border: 0;
    border-radius: 6px;
    padding: 6px 8px;
    display: flex;
    flex-direction: column;
  }
  li button:hover {
    background: var(--panel);
  }
  li button.current {
    background: var(--line);
  }
  .title {
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .date {
    font-size: 11px;
    color: var(--muted);
  }
</style>
