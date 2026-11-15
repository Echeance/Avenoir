<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, str } from "./report.svelte";
  import Section from "./Section.svelte";

  let everything = $state(false);
  const report = new Report("journal/list", () => ({ all: everything, limit: 200 }));
  // Requêtes encore annulables (depuis le lancement d'Avenoir), la plus
  // récente d'abord : on ne défait que dans l'ordre inverse, comme /annuler.
  const undo = new Report("undo/list");

  $effect(() => {
    void everything;
    if (session.connected) {
      void report.load();
      void undo.load();
    }
  });

  async function undoLast(): Promise<void> {
    await undo.run("/annuler"); // demande confirmation, puis défait
    await report.load();
  }

  const DECISIONS: Record<string, string> = {
    allowed: "autorisée",
    allowed_by_rule: "toujours autorisée",
    confirmed: "confirmée",
    rejected: "refusée",
    denied_by_policy: "refusée par la politique",
    denied_by_rule: "toujours refusée",
    refused_by_review: "refusée d'office",
    unknown_tool: "outil inconnu",
  };
</script>

<Section
  title="Journal des actions"
  subtitle={`Chaque action demandée, sa décision et son résultat ; gardé ${str(report.data?.["retention_days"]) || "90"} jours.`}
  error={report.error}
  onrefresh={() => void report.load()}
>
  {#if list(undo.data?.["requests"]).length > 0}
    <h2>Encore annulables</h2>
    <p class="muted">
      Les actions sont défaites de la plus récente à la plus ancienne, requête par requête, après confirmation.
    </p>
    {#each list(undo.data?.["requests"]) as request, index (index)}
      <div class="undo">
        <div>
          <strong>« {str(request["request"])} »</strong>
          <ul>
            {#each list(request["actions"]) as action, at (at)}
              {#if str(action["undo"]) !== ""}
                <li>↺ {str(action["undo"])}</li>
              {:else}
                <li class="muted">✗ {str(action["summary"])} (non annulable)</li>
              {/if}
            {/each}
          </ul>
        </div>
        {#if index === 0}
          <button class="action" disabled={session.running !== null} onclick={() => void undoLast()}>Annuler</button>
        {:else}
          <span class="muted next">ensuite</span>
        {/if}
      </div>
    {/each}
    <h2>Toutes les actions</h2>
  {/if}
  {#if undo.error !== ""}<p class="refused">{undo.error}</p>{/if}
  <label class="muted"><input type="checkbox" bind:checked={everything} /> toutes les conversations</label>
  <table>
    <thead><tr><th>Heure</th><th></th><th>Action</th><th>Décision</th><th>Résultat</th></tr></thead>
    <tbody>
      {#each list(report.data?.["actions"]) as action (str(action["id"]))}
        <tr>
          <td class="muted nowrap">{str(action["at"])}</td>
          <td>{action["executed"] === true ? (action["succeeded"] === true ? "✓" : "✗") : "–"}</td>
          <td>
            <code>{str(action["tool"])}</code>
            {#if str(action["agent"]) !== ""}<span class="muted"> · {str(action["agent"])}</span>{/if}
            <div class="muted args">{JSON.stringify(action["arguments"] ?? {})}</div>
          </td>
          <td class:refused={action["executed"] !== true}>
            {DECISIONS[str(action["decision"])] ?? str(action["decision"])}
            {#if str(action["reason"]) !== ""}<div class="reason">▲ {str(action["reason"])}</div>{/if}
          </td>
          <td class="muted result">{str(action["result"])}</td>
        </tr>
      {:else}
        <tr><td colspan="5" class="muted">Aucune action.</td></tr>
      {/each}
    </tbody>
  </table>
</Section>

<style>
  .nowrap {
    white-space: nowrap;
  }
  .args,
  .result {
    font-size: 12px;
    overflow-wrap: anywhere;
    max-width: 280px;
  }
  .refused {
    color: var(--yellow);
  }
  h2 {
    font-size: 14px;
    color: var(--ice);
    margin: 14px 0 6px;
  }
  .undo {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 12px;
    border: 1px solid var(--line);
    border-radius: 8px;
    padding: 8px 12px;
    margin-bottom: 6px;
  }
  .undo ul {
    margin: 4px 0 0;
    padding-left: 18px;
    font-size: 13px;
  }
  .next {
    font-size: 12px;
  }
  .reason {
    font-size: 12px;
    color: var(--yellow);
  }
</style>
