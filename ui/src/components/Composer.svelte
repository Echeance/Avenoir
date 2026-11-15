<script lang="ts">
  let {
    busy,
    disabled,
    onsend,
    onstop,
    undoable = false,
    onundo,
  }: {
    busy: boolean;
    disabled: boolean;
    onsend: (line: string) => void;
    onstop: () => void;
    /// Vrai si la dernière requête annulable peut être défaite (`/annuler`).
    undoable?: boolean;
    onundo?: () => void;
  } = $props();

  let line = $state("");

  function submit(event: SubmitEvent): void {
    event.preventDefault();
    if (line.trim() === "" || busy) {
      return;
    }
    onsend(line);
    line = "";
  }
</script>

<form class="composer" onsubmit={submit}>
  <span class="mark">›</span>
  <input
    type="text"
    bind:value={line}
    placeholder={disabled ? "Avenoir ne répond pas…" : "Demandez à Avenoir"}
    {disabled}
    autocomplete="off"
    spellcheck="false"
    aria-label="Requête"
  />
  {#if busy}
    <button type="button" class="stop" onclick={onstop}>Arrêter</button>
  {:else}
    {#if undoable && onundo !== undefined && line.trim() === ""}
      <button type="button" class="undo" onclick={onundo} disabled={disabled} title="Défaire les actions de la dernière requête (/annuler)">↺ Annuler</button>
    {/if}
    <button type="submit" disabled={disabled || line.trim() === ""}>Envoyer</button>
  {/if}
</form>

<style>
  .composer {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px 12px;
    border-top: 1px solid var(--line);
  }
  .mark {
    color: var(--azure);
    font-weight: 700;
  }
  input {
    flex: 1;
    min-width: 0;
    background: transparent;
    border: 0;
    outline: 0;
    color: var(--text);
    font: inherit;
  }
  input::placeholder {
    color: var(--muted);
  }
  button {
    background: var(--navy);
    border: 0;
    border-radius: 6px;
    padding: 4px 12px;
  }
  button:disabled {
    opacity: 0.4;
    cursor: default;
  }
  .stop {
    background: transparent;
    border: 1px solid var(--red);
    color: var(--red);
  }
  .undo {
    background: transparent;
    border: 1px solid var(--line);
    color: var(--sky);
  }
</style>
