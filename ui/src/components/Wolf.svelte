<script lang="ts">
  // Le loup d'Avenoir, ses points braille dessinés un à un (exportés de
  // libs/terminal/src/mascot.cpp par docs/readme/wolf.py) et animés selon
  // l'état : respiration au repos, vague de lumière pendant la réflexion,
  // pulsation dorée quand Avenoir attend une réponse.
  import art from "../assets/wolf-dots.json";

  export type Mood = "idle" | "thinking" | "asking" | "offline";

  let { mood = "idle", size = 26 }: { mood?: Mood; size?: number } = $props();

  // Points typés : [x, y] en unités de points braille.
  const points = art.dots.map(([x = 0, y = 0]) => ({ x, y }));
  const width = art.width * 2;
  const height = art.height * 2;
</script>

<svg
  class="wolf {mood}"
  viewBox="0 0 {width} {height}"
  height={size}
  width={(size * width) / height}
  role="img"
  aria-label="Avenoir"
>
  {#each points as point, index (index)}
    <circle cx={point.x * 2 + 1} cy={point.y * 2 + 1} r="0.85" style:--row={point.y} />
  {/each}
</svg>

<style>
  .wolf {
    flex: none;
    overflow: visible;
  }
  circle {
    fill: var(--sky);
  }
  .idle {
    animation: breathe 4s ease-in-out infinite;
  }
  .thinking circle {
    animation: wave 1.4s ease-in-out infinite;
    animation-delay: calc(var(--row) * 25ms);
  }
  .asking circle {
    fill: var(--yellow);
    animation: pulse 0.9s ease-in-out infinite;
  }
  .offline circle {
    fill: var(--muted);
  }
  @keyframes breathe {
    50% {
      opacity: 0.75;
    }
  }
  @keyframes wave {
    0%,
    100% {
      fill: var(--sky);
    }
    40% {
      fill: var(--ice);
    }
    60% {
      fill: var(--azure);
    }
  }
  @keyframes pulse {
    50% {
      opacity: 0.4;
    }
  }
  @media (prefers-reduced-motion: reduce) {
    .wolf,
    circle {
      animation: none !important;
    }
  }
</style>
