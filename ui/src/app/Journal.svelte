<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, str } from "./report.svelte";
  import Section from "./Section.svelte";

  let everything = $state(false);
  const report = new Report("journal/list", () => ({ all: everything, limit: 200 }));

  $effect(() => {
    void everything;
    if (session.connected) {
      void report.load();
    }
  });

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
  .reason {
    font-size: 12px;
    color: var(--yellow);
  }
</style>
