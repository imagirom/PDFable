// Sample document for the PDFable README screenshot.
// Build: typst compile docs/screenshot/sample.typ docs/screenshot/sample.pdf
#let accent = rgb("#8c1626")

#set document(title: "This document was written in Typst", author: "PDFable")
#set page(paper: "a4", margin: (x: 2.3cm, top: 2.2cm, bottom: 2.4cm),
  footer: context align(center, text(8pt, fill: luma(110))[
    Typeset with Typst · page #counter(page).display()]))
#set text(font: "Libertinus Serif", size: 11pt, lang: "en")
#set par(justify: true, leading: 0.62em)
#set heading(numbering: "1.")
#show heading: set text(fill: accent)
#show link: set text(fill: accent)
#show raw.where(block: true): block.with(fill: luma(246), inset: 9pt, radius: 4pt, width: 100%)

#align(center)[
  #text(22pt, weight: "bold", fill: accent)[This document was written in Typst]
  #v(-0.3em)
  #text(12pt, style: "italic")[A modern typesetting system: as precise as LaTeX, as readable as Markdown]
]
#v(0.6em)

#block(inset: (x: 1.2cm))[
  #set text(10pt)
  *Abstract.* Everything you see on this page (the headings, the equations, the table and
  the code listing) comes from one plain-text file of a few dozen lines. Typst compiles it in
  milliseconds, recompiles incrementally while you type, and reports errors in words you can
  understand. If you write papers, theses or lecture notes, it is worth an afternoon.
]

= Markup that stays out of your way
A heading is a line starting with `=`, emphasis is `_like this_`, strong text is `*like this*`
and lists start with `-` or `+`. You never fight with `\begin` and `\end`, and the source
remains readable for co-authors who have never seen Typst before.

= Mathematics feels natural
Formulas are written between dollar signs, without backslashes for every symbol. The
Thomas--Fermi kinetic energy of a density $rho(bold(r))$, for example, is simply
$
  T_"TF" [rho] = C_F integral rho^(5\/3)(bold(r)) dif bold(r),
  quad C_F = 3/10 (3 pi^2)^(2\/3) approx 2.871 "(atomic units)".
$
Fractions, roots, matrices and aligned equations work the same way, and numbering is a
single `set` rule away.

= Scripting is built in
Typst is also a small programming language. Functions, loops and conditionals live in the
same file as the text, so templates are ordinary code instead of fragile macros:

```typ
#let note(body) = block(fill: luma(240), inset: 8pt, body)
#for name in ("Ada", "Emmy", "Lise") [ Hello, #name! ]
```

= At a glance
#table(
  columns: (1fr, 1.2fr, 1.2fr),
  stroke: 0.5pt + luma(180),
  inset: 6pt,
  fill: (x, y) => if y == 0 { accent.lighten(85%) },
  [*You want*], [*LaTeX*], [*Typst*],
  [a section], [`\section{Intro}`], [`= Intro`],
  [bold text], [`\textbf{word}`], [`*word*`],
  [a fraction], [`\frac{a}{b}`], [`a/b`],
  [a bullet list], [`\begin{itemize}` …], [`- item`],
)

= Try it
The compiler is open source (Apache 2.0) and written in Rust; install it with your package
manager or use the collaborative editor at #link("https://typst.app")[typst.app]. Then open
the resulting PDF in a reader that lets you highlight, annotate and write on it with a pen.

#pagebreak()
#heading(numbering: none)[Appendix: the complete source of this document]
Typst can read files and show them as highlighted code, so this appendix is generated from
the very file that produced it:

#set text(size: 6.9pt)
#set par(leading: 0.5em)
#raw(read("sample.typ"), lang: "typ", block: true)
