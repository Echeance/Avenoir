<script lang="ts">
  import { session } from "../lib/session.svelte";
  import { list, Report, RISK_LABELS, str } from "./report.svelte";
  import Section from "./Section.svelte";

  const report = new Report("modules/list");
  $effect(() => {
    if (session.connected) {
      void report.load();
    }
  });

  const STATES: Record<string, string> = {
    running: "actif",
    disabled: "désactivé",
    awaiting_grant: "permissions à accorder",
    failed: "en échec",
  };
  const LEVELS: Record<string, string> = { read_only: "lecture", reversible: "réversible", sensitive: "sensible" };

  async function changeRisk(tool: string, level: string): Promise<void> {
    await report.run(`/risque ${tool} ${level}`);
  }

  // Installer ou mettre à jour depuis un dossier (/installer : Avenoir montre
  // le module et ses permissions, demande confirmation, puis redémarre).
  let folder = $state("");
  async function install(event: SubmitEvent): Promise<void> {
    event.preventDefault();
    const path = folder.trim().replace(/[\u0000-\u001f]/g, "");
    if (path === "") {
      return;
    }
    folder = "";
    await report.run(`/installer "${path.replaceAll('"', "")}"`);
  }
</script>

<Section
  title="Modules"
  subtitle="Programmes séparés qui ajoutent des outils ; un module installé par vous reste désactivé tant que vous ne lui accordez pas ses permissions."
  error={report.error}
  onrefresh={() => void report.load()}
>
  <form class="install" onsubmit={(event) => void install(event)}>
    <input bind:value={folder} placeholder="Dossier d'un module à installer ou mettre à jour (avec son module.toml)" autocomplete="off" spellcheck="false" />
    <button class="action" type="submit" disabled={folder.trim() === "" || session.running !== null}>Installer</button>
  </form>
  {#each list(report.data?.["modules"]) as module (str(module["id"]))}
    {@const state = str(module["state"])}
    <h2>
      {str(module["name"])} <span class="muted">· {str(module["id"])} · v{str(module["version"])} · {module["bundled"] === true ? "fourni avec Avenoir" : "installé par vous"}</span>
    </h2>
    <p class="state {state}">
      {STATES[state] ?? state}{#if str(module["message"]) !== ""} — {str(module["message"])}{/if}
      {#if state === "disabled" || state === "awaiting_grant"}
        <button class="action" onclick={() => void report.run(`/accorder ${str(module["id"])}`)}>Accorder et activer</button>
      {:else}
        <button class="action danger" onclick={() => void report.run(`/revoquer ${str(module["id"])}`)}>Désactiver</button>
      {/if}
      {#if module["bundled"] !== true}
        <button class="action danger" onclick={() => void report.run(`/desinstaller ${str(module["id"])}`)}>Désinstaller</button>
      {/if}
    </p>
    {#if list(module["tools"]).length > 0}
      <table>
        <thead><tr><th>Outil</th><th>Risque</th><th>Manifeste</th></tr></thead>
        <tbody>
          {#each list(module["tools"]) as tool (str(tool["name"]))}
            <tr>
              <td><code>{str(tool["name"])}</code></td>
              <td>
                <select
                  value={str(tool["risk"])}
                  onchange={(event) => void changeRisk(str(tool["name"]), LEVELS[event.currentTarget.value] ?? "sensible")}
                  aria-label="Risque de {str(tool['name'])}"
                >
                  {#each Object.entries(RISK_LABELS) as [risk, label] (risk)}
                    <option value={risk}>{label}</option>
                  {/each}
                </select>
              </td>
              <td class="muted">{RISK_LABELS[str(tool["declared"])] ?? str(tool["declared"])}</td>
            </tr>
          {/each}
        </tbody>
      </table>
      <p class="muted">Abaisser un risque sous celui du manifeste demande « Je sais ce que je fais ».</p>
    {/if}
  {:else}
    <p class="muted">Aucun module installé.</p>
  {/each}
</Section>

<style>
  .install {
    display: flex;
    gap: 8px;
    margin: 4px 0 10px;
  }
  .install input {
    flex: 1;
    background: var(--night);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 4px 8px;
  }
  .state.running {
    color: var(--green);
  }
  .state.failed {
    color: var(--red);
  }
  .state.awaiting_grant,
  .state.disabled {
    color: var(--yellow);
  }
  select {
    background: var(--panel);
    color: var(--text);
    border: 1px solid var(--line);
    border-radius: 6px;
    padding: 2px 6px;
    font: inherit;
  }
</style>
