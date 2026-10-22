# DEVWORLD — Stage 4: Learning engine and genuine mastery assessment

Status: **spec locked**. This is the Stage 4 target. It builds on
[08-education-system.md](08-education-system.md) (Phase 1: local graph,
mastery model, skill panel, practice recalls) and extends it in three ways:
server authority, a broader curriculum, and a full mission-format and
authoring system. Where this doc and 08 disagree on curriculum organization,
this doc wins — see §5 for the mapping from 08's areas to these areas.

> Note: the pasted spec numbered sections 4.1, 4.2, 4.3, then 4.5. Section
> 4.4 was not in the paste. The deliverables list an AI tutor, and 08 §5
> already covers AI assistance modes, so 4.4 is treated here as the AI tutor
> section unless corrected.

## 4.1 Curriculum architecture

Organize the educational content into connected knowledge areas:

- Computational thinking and logic.
- Variables, data types, expressions and control flow.
- Functions, modules and abstraction.
- Collections, algorithms and data structures.
- Object-oriented and functional programming.
- Memory, execution and debugging.
- Testing, Git and software design.
- Databases and data modeling.
- Networking and web services.
- Operating systems and concurrency.
- Security and reliability.
- Advanced specializations and engineering research.

Each topic needs prerequisites, learning objectives, exercises,
misconceptions, assessments and links to practical projects.

## 4.2 Different learning activities

Provide a broad range of mission formats:

- Predict what a program will do.
- Complete missing code.
- Write code from specifications.
- Find and repair bugs.
- Refactor poorly designed programs.
- Explain why an implementation fails.
- Optimize slow or memory-intensive code.
- Read and modify unfamiliar code.
- Design a solution before implementing it.
- Build projects from incomplete requirements.
- Review another player's implementation.
- Transfer a skill to a different language or environment.

## 4.3 Mastery measurement

Use multiple forms of evidence:

- Automated tests.
- Hidden edge cases.
- Code quality and maintainability checks.
- Performance constraints.
- Oral or written explanations.
- Practical debugging assessments.
- Delayed recall and spaced repetition.
- Unseen transfer problems.
- Larger projects reviewed against a rubric.

Do not confuse speed, XP or tutorial completion with mastery.

The evidence kinds, weights, decay, and independence/mastery thresholds are
already defined in 08 §4.2–§4.4 and implemented in the mastery model
(`mastery.h`): a player who only passes shipped tests scores well but is
never `independent` and never `mastered`. That rule is the mechanism behind
this section, not a restatement of it.

## 4.4 AI tutor (assumed section)

The AI tutor teaches, debugs, and reviews, but never emits a complete working
solution for the current mission, and produces no mastery evidence except
through the `explanation` + `project_rubric` path (08 §5). Independent
challenges disable the tutor entirely. Server-side enforcement of the
no-complete-solution rule is what makes transfer evidence meaningful.

## 4.5 Educator and content tools

Create an authoring system for experts to build missions with:

- Starter repositories.
- Instructions and learning objectives.
- Test suites and reference implementations.
- Hints and progressive disclosures.
- Scoring rules and skill tags.
- Difficulty settings and prerequisites.
- Localization and accessibility support.
- Versioning and regression tests.

## Deliverables

Curriculum graph, mission engine, mastery model, assessment framework,
AI tutor, content editor and skill analytics.

## Completion criterion

The system can distinguish players who merely finish an exercise from
players who independently understand and apply the underlying concept.

## §5 — Relationship to 08 (what is already built, what is new)

**Already built (Phase 1, local):** the knowledge-graph loader and DAG
algorithms (`game-shell/src/curriculum.*`), the evidence + mastery model
(`game-shell/src/mastery.*`), the shipped graph
(`game-shell/content/curriculum.json`: 14 areas, 44 topics, mapped to
`m01`–`m29`), the in-game skill panel, misconception coaching, and practice
re-runs emitting delayed-recall evidence.

**New in Stage 4:**

| Piece | State |
|---|---|
| Mastery model on the C++ server (Phase 2 authority) | Ported from the shell model; server computes, client displays. Evidence endpoints (`GET /skills/me`, `/skills/trees`) still to come. |
| Curriculum reorganization into the 12 areas above | Spec only. The shipped 44 topics still sit in 08's areas. Migrating them is a content decision, not done by this doc. |
| 12 mission formats (§4.2) | Only "write code from specifications" ships today. The rest need mission-type support in the catalog, the IDE, and grading. |
| Authoring system / content editor (§4.5) | Not built. |
| Skill analytics | Not built. |
| AI tutor service (§4.4) | Modes specified in 08 §5; no service yet. |

**Area mapping (08 → Stage 4), for the migration:**

| 08 area | Stage 4 area |
|---|---|
| Foundations | Computational thinking and logic (partly) |
| Values & Types, Control Flow, Iteration | Variables, data types, expressions and control flow |
| Functions & Scope | Functions, modules and abstraction |
| Data Structures | Collections, algorithms and data structures |
| — (new) | Object-oriented and functional programming |
| Debugging; Systems & Performance (part) | Memory, execution and debugging |
| Testing & Evidence; Tooling & Version Control; Software Engineering (part) | Testing, Git and software design |
| Relational Queries | Databases and data modeling |
| — (new) | Networking and web services |
| Systems & Performance (part) | Operating systems and concurrency |
| — (new) | Security and reliability |
| Software Engineering (part) | Advanced specializations and engineering research |
