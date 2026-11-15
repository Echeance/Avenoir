<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, RISK_LABELS, str } from "./report.svelte";
  import Section from "./Section.svelte";

  const report = new Report("permissions/list");
  $effect(() => {
    if (session.connected) {
      void report.load();
    }
  });

  const POLICIES: Record<string, string> = { allow: "autoriser", confirm: "confirmer", deny: "refuser" };
  const LABELS: Record<string, string> = {
    network: "réseau",
    "files.read": "lecture de fichiers",
    "files.write": "écriture de fichiers",
    "ui.web": "onglet web",
  };
  const permissions = (value: unknown): string =>
    Array.isArray(value) && value.length > 0
      ? value.map((item) => LABELS[str(item)] ?? str(item)).join(", ")
      : "aucune";
</script>

<Section
  title="Permissions"
  subtitle="Politique par niveau de risque, règles « toujours / jamais » posées depuis une confirmation, permissions des modules."
  error={report.error}
  onrefresh={() => void report.load()}
>
  {#if report.data !== null}
    {@const policy = report.data["policy"]}
    <h2>Par niveau de risque</h2>
    {#if typeof policy === "object" && policy !== null && !Array.isArray(policy)}
      <table>
        <tbody>
          {#each Object.entries(policy) as [risk, value] (risk)}
            <tr><td>{RISK_LABELS[risk] ?? risk}</td><td><strong>{POLICIES[str(value)] ?? str(value)}</strong></td></tr>
          {/each}
        </tbody>
      </table>
      <p class="muted">Se changent dans Réglages (permissions.read_only, reversible, sensitive).</p>
    {/if}

    <h2>Actions toujours autorisées ou refusées</h2>
    <table>
      <tbody>
        {#each list(report.data["rules"]) as rule (str(rule["tool"]))}
          <tr>
            <td><code>{str(rule["tool"])}</code></td>
            <td class:allow={str(rule["policy"]) === "allow"}>
              {str(rule["description"])}
              {#if rule["active"] === false}<span class="muted"> (hors vigueur : autre version du module)</span>{/if}
            </td>
            <td><button class="action danger" onclick={() => void report.run(`/revoquer ${str(rule["tool"])}`)}>Révoquer</button></td>
          </tr>
        {:else}
          <tr><td class="muted">Aucune : répondez « Toujours » ou « Jamais » à une confirmation.</td></tr>
        {/each}
      </tbody>
    </table>
    {#if list(report.data["rules"]).length > 1}
      <p><button class="action danger" onclick={() => void report.run("/revoquer tout")}>Tout révoquer</button></p>
    {/if}

    <h2>Permissions des modules</h2>
    <table>
      <tbody>
        {#each list(report.data["modules"]) as module (str(module["id"]))}
          {@const missing = Array.isArray(module["missing"]) && module["missing"].length > 0}
          <tr>
            <td><strong>{str(module["id"])}</strong></td>
            <td>
              {permissions(module["permissions"])}
              <div class="muted">
                {module["bundled"] === true ? "fourni avec Avenoir" : missing ? `à accorder : ${permissions(module["missing"])}` : "installé par vous"}
              </div>
            </td>
            <td>
              {#if missing}
                <button class="action" onclick={() => void report.run(`/accorder ${str(module["id"])}`)}>Accorder</button>
              {:else}
                <button class="action danger" onclick={() => void report.run(`/revoquer ${str(module["id"])}`)}>Révoquer (désactive)</button>
              {/if}
            </td>
          </tr>
        {/each}
      </tbody>
    </table>
  {/if}
</Section>

<style>
  .allow {
    color: var(--yellow);
  }
</style>
