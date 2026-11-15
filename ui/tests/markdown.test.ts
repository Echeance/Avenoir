import { describe, expect, it } from "vitest";

import { parseInline, parseMarkdown } from "../src/lib/markdown";

describe("Markdown des réponses", () => {
  it("analyse les fragments d'une ligne", () => {
    expect(parseInline("un **gras**, de l'*italique*, du `code` et [un lien](https://a.example)")).toEqual([
      { kind: "text", text: "un " },
      { kind: "strong", children: [{ kind: "text", text: "gras" }] },
      { kind: "text", text: ", de l'" },
      { kind: "em", children: [{ kind: "text", text: "italique" }] },
      { kind: "text", text: ", du " },
      { kind: "code", text: "code" },
      { kind: "text", text: " et " },
      { kind: "link", children: [{ kind: "text", text: "un lien" }], url: "https://a.example" },
    ]);
    // Pas d'italique au milieu d'un mot (noms de fichiers, identifiants).
    expect(parseInline("snake_case_nom")).toEqual([{ kind: "text", text: "snake_case_nom" }]);
  });

  it("analyse les blocs", () => {
    const blocks = parseMarkdown(
      "### Titre\n\n- un\n- deux\n\n1. premier\n\n> cité\n\n```bash\npip install x\n```\n\n---\n\n| a | b |\n|---|---|\n| 1 | 2 |\n\nfin",
    );
    expect(blocks.map((block) => block.kind)).toEqual([
      "heading",
      "list",
      "list",
      "quote",
      "code",
      "rule",
      "table",
      "paragraph",
    ]);
    expect(blocks[4]).toEqual({ kind: "code", language: "bash", text: "pip install x" });
  });

  it("garde le HTML comme du texte", () => {
    expect(parseMarkdown("<script>alert(1)</script>")).toEqual([
      { kind: "paragraph", inline: [{ kind: "text", text: "<script>alert(1)</script>" }] },
    ]);
  });

  it("rend une réponse enveloppée dans un bloc markdown, blocs imbriqués compris", () => {
    const blocks = parseMarkdown("```markdown\n# Exemple\n\n- un **point**\n\n```bash\nls -l\n```\n```");
    expect(blocks.map((block) => block.kind)).toEqual(["heading", "list", "code"]);
    expect(blocks[2]).toEqual({ kind: "code", language: "bash", text: "ls -l" });
  });

  it("ne ferme pas un bloc sur une ligne qui porte un langage", () => {
    expect(parseMarkdown("```\n```bash\nx\n```")).toEqual([
      { kind: "code", language: "", text: "```bash\nx" },
    ]);
  });

  it("supporte un bloc de code resté ouvert (réponse en cours)", () => {
    expect(parseMarkdown("```\nligne")).toEqual([{ kind: "code", language: "", text: "ligne" }]);
  });
});
