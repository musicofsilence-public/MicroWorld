# ADR 0005: The Vocabulary Is Downstream Of The Model

- **Status:** Accepted
- **Date:** 2026-07-31
- **Decision owner:** Project owner

## Context

ADR 0004 made the folder tree downstream of the model and left the names alone.
The tree then said `Transport` while the code inside it still said `Net` — a result
enum, a driver interface, a play system and a host all carried the retired prefix.
A reader holding the model and the code had to translate between them, which is
precisely the cost ADR 0004 spent a breaking change to remove from the tree.

Two further gaps surfaced once the sweep began, and neither was visible before it.

**Documentation named types that do not exist.** Composition recipes were written
against a class that had been renamed, and one showed a template taking eight
arguments where it takes one. A reader could not have compiled any of it. Nothing
in the build touches prose, so this had been accumulating silently.

**There were two mechanisms for hiding a helper.** A `Detail` namespace sat under
the product namespace, and two more namespaces carried the word as a suffix, while
the tree already expressed privacy with a private-header folder. ADR 0004 rejected
exactly this shape for headers — two competing mechanisms means neither is the rule.

## Decision

- **A model name is the code name.** Where the two disagree the code changes, in
  identifiers and in prose alike. This is ADR 0004's rule applied to names rather
  than to directories, and it inherits that record's reasoning unchanged.
- **A vocabulary change is a sweep over prose, not a symbol rename.** The sweep is
  case-insensitive and covers comments, guides and documents. A retired word leaves
  the identifiers first and survives for a long time as an English phrase.
- **One namespace for the product**, with one narrowly named exception where a
  helper family is genuinely shared by unrelated types. Privacy is expressed by the
  private-header folder, which is now the only mechanism.
- **Documentation names only types that exist**, and a recipe compiles as written.
  A named type in a document is a claim, not an illustration.
- **A check that proves a name is gone must assert what should be present.** A gate
  that merely fails to find a retired word reports success by detecting nothing, and
  goes on passing forever after the thing it watched is renamed.

The boundary in one sentence a student can quote: **the model owns every word the
code says, and a word is only retired once no comment still speaks it.**

## Consequences

- **Every result code, driver interface and play-system type is renamed**, with no
  aliases left behind. This is a breaking change and is versioned as one.
- **A vocabulary sweep costs far more than a rename.** Five successive passes each
  reported the retired word gone while more than seventy lines of prose still used
  it, because each pass matched the capitalised identifier and not the English word.
  Budget the prose pass separately and expect it to be the larger half.
- **Correctness came from the checkable form of the rule, not from care.** Fourteen
  distinct errors in the change briefs were caught by the people executing them —
  a rename table naming a type that exists nowhere, a marker pointing at the wrong
  prefix, a medium described as the wrong kind. A brief stating a rule survived; a
  brief stating an example did not.
- **The flat namespace gives up compiler-enforced hiding.** Helpers that were
  unreachable are reachable now, and collisions are prevented by naming discipline
  alone. This is the same trade ADR 0004 made for private headers, taken again.
- **A document that records a measurement or a finished run is frozen.** Only a
  document that says what to do next is swept, so benchmark results and generated
  diagrams keep the vocabulary of the day they were produced.

## Alternatives considered

- **Rename the model to match the code.** Rejected for the reason ADR 0004 rejected
  its own version of this: it inverts the dependency between the document and the
  thing the document governs. A class name records where a responsibility lives
  today and decides nothing.
- **Keep the old names as aliases.** Rejected. Two spellings mean the retired
  vocabulary never actually leaves, and a reader meeting one cannot tell which is
  current. The cost of a hard cut is paid once; the cost of two spellings is paid at
  every reading.
- **Sweep identifiers only and let comments age out.** Rejected. Prose is where the
  vocabulary is taught, so a comment holding a retired word teaches it to the next
  reader more effectively than an identifier would have.
- **One shared helper namespace for everything.** Rejected. A name meaning only
  "shared" classifies nothing and becomes the place helpers go to avoid a decision.
  The one exception granted is named for what its helpers do.
- **Nest every helper inside the class it serves.** Adopted where it fits and
  rejected where it does not: a type appearing in a public signature cannot be
  private, and a trait needed by a default template argument cannot wait for its
  class to be complete. Forcing either would add machinery to remove a name.

## Revisit triggers

- The model renames a system. By this decision that is a code change, not a
  documentation change, and the sweep is part of it.
- Collisions in the flat namespace become frequent enough that naming discipline
  stops holding.
- A second genuinely shared helper family appears that the granted exception does
  not describe.
- A gate is added that watches for an absent word rather than asserting a present
  one.
