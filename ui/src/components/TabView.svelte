<script lang="ts">
  // Onglet déclaratif d'un module (décision 73) : son contenu, validé par
  // l'hôte de modules, est dessiné ici par Avenoir ; aucun code du module ne
  // s'exécute dans la page. Un bouton ne lance qu'une commande du module.
  import { isObject, type JsonObject } from "../lib/protocol";
  import { session } from "../lib/session.svelte";

  let { module, tab, onaction }: { module: string; tab: string; onaction: () => void } = $props();

  let components = $state<JsonObject[]>([]);
  let error = $state("");
  let loading = $state(true);

  const str = (value: unknown): string => (typeof value === "string" ? value : "");
  const rows = (value: unknown): string[][] =>
    Array.isArray(value)
      ? value.map((row) => (Array.isArray(row) ? row.map((cell) => str(cell)) : []))
      : [];
  const items = (value: unknown): string[] => (Array.isArray(value) ? value.map((item) => str(item)) : []);

  async function load(): Promise<void> {
    loading = true;
    try {
      const result = await session.request("tabs/content", { module, tab });
      const list = result["components"];
      components = Array.isArray(list) ? list.filter(isObject) : [];
      error = "";
    } catch (failure) {
      error = failure instanceof Error ? failure.message : "onglet illisible";
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    void module;
    void tab;
    void load();
  });

  function run(component: JsonObject): void {
    const argument = str(component["argument"]);
    void session.send(`/${str(component["command"])}${argument === "" ? "" : " " + argument}`);
    onaction();
  }
</script>

<section class="tab">
  <div class="tools">
    <button class="refresh" onclick={() => void load()} title="Actualiser">↻</button>
  </div>
  {#if loading && components.length === 0}
    <p class="muted">Chargement…</p>
  {/if}
  {#if error !== ""}<p class="warning">{error}</p>{/if}
  {#each components as component, index (index)}
    {@const type = str(component["type"])}
    {#if type === "heading"}
      <h2>{str(component["text"])}</h2>
    {:else if type === "text"}
      <p class={str(component["tone"])}>{str(component["text"])}</p>
    {:else if type === "stat"}
      <p class="stat"><span class="label">{str(component["label"])}</span><span>{str(component["value"])}</span></p>
    {:else if type === "list"}
      <ul>{#each items(component["items"]) as item, row (row)}<li>{item}</li>{/each}</ul>
    {:else if type === "table"}
      <table>
        <thead><tr>{#each items(component["columns"]) as column, col (col)}<th>{column}</th>{/each}</tr></thead>
        <tbody>
          {#each rows(component["rows"]) as cells, row (row)}
            <tr>{#each cells as cell, col (col)}<td>{cell}</td>{/each}</tr>
          {/each}
        </tbody>
      </table>
    {:else if type === "button"}
      <button class="action" onclick={() => run(component)} disabled={session.running !== null}>
        {str(component["label"])}
      </button>
    {/if}
  {/each}
</section>

<style>
  .tab {
    flex: 1;
    min-height: 0;
    overflow-y: auto;
    padding: 10px 14px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    align-items: flex-start;
  }
  .tools {
    align-self: flex-end;
  }
  .refresh {
    background: transparent;
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 0 8px;
  }
  h2 {
    margin: 0;
    font-size: 15px;
    color: var(--ice);
  }
  p {
    margin: 0;
    white-space: pre-wrap;
    overflow-wrap: anywhere;
  }
  .muted {
    color: var(--muted);
    font-size: 12px;
  }
  .warning {
    color: var(--yellow);
  }
  .stat {
    display: flex;
    gap: 10px;
    width: 100%;
  }
  .label {
    color: var(--muted);
    min-width: 110px;
  }
  ul {
    margin: 0;
    padding-left: 18px;
  }
  table {
    border-collapse: collapse;
    font-size: 12px;
    width: 100%;
  }
  th,
  td {
    text-align: left;
    padding: 3px 8px;
    border-bottom: 1px solid var(--line);
  }
  th {
    color: var(--muted);
    font-weight: 500;
  }
  .action {
    background: var(--navy);
    border: 0;
    border-radius: 6px;
    padding: 4px 12px;
  }
  .action:disabled {
    opacity: 0.4;
  }
</style>
