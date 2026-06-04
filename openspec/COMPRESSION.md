# OpenSpec Compression Rules

## Purpose

Write OpenSpec artifact prose in compact caveman-style markdown to reduce agent
context load while preserving technical meaning. Any agent UI can apply these
rules by reading this file; no Claude-specific skill, plugin, API, or binary is
required.

## Scope

Applies to prose in:

- `openspec/specs/**`
- `openspec/changes/**`

Use for proposal, design, spec, and task body prose. Do not use as an automatic
rewriter. Human review owns semantic correctness.

## Remove

- Articles: `a`, `an`, `the`
- Filler: `just`, `really`, `basically`, `actually`, `simply`, `essentially`, `generally`
- Pleasantries: `sure`, `certainly`, `of course`, `happy to`, `I'd recommend`
- Hedging: `it might be worth`, `you could consider`, `it would be good to`
- Redundant phrasing: `in order to` -> `to`, `make sure to` -> `ensure`, `the reason is because` -> `because`
- Connective fluff: `however`, `furthermore`, `additionally`, `in addition`

## Preserve EXACTLY

Never modify:

- Code blocks, fenced or indented
- Inline code
- URLs and links
- File paths
- Commands
- Technical terms: library names, API names, protocols, algorithms
- Proper nouns: project names, people, companies
- Dates, version numbers, numeric values
- Environment variables

## Preserve Structure

- Keep all markdown headings byte-identical.
- Keep bullet hierarchy and nesting.
- Keep numbered-list numbering.
- Keep table structure; compress only plain prose cell text when safe.
- Keep frontmatter and YAML headers byte-identical.

## Contract Guard

OpenSpec specs are contracts, so normative structure is read-only:

- Never alter RFC-2119 keywords: `SHALL`, `MUST`, `SHOULD`, `MAY`, `REQUIRED`.
- Never drop, merge, split, or reorder normative clauses.
- Treat `### Requirement:` and `#### Scenario:` headings as byte-identical.
- Treat scenario markers such as `WHEN`, `THEN`, `AND`, `GIVEN`, `BUT` as byte-identical.
- Preserve each requirement/scenario relationship. Do not merge scenarios even when text looks redundant.
- If compression would change meaning, leave text unchanged.

## Compress

- Use short synonyms: `big` not `extensive`, `fix` not `implement a solution for`, `use` not `utilize`.
- Fragments OK: `Run tests before commit` not `You should always run tests before committing`.
- Drop `you should`, `make sure to`, `remember to`; state action directly.
- Merge redundant bullets only when they are non-normative prose and say the same thing.
- Keep one example when multiple non-normative examples show the same pattern.

## Code Block Rule

Anything inside fenced code blocks must be copied exactly.

Do not:

- Remove comments
- Remove spacing
- Reorder lines
- Shorten commands
- Simplify anything

Inline code must be preserved exactly. Do not modify anything inside backticks.

If file contains code blocks:

- Treat code blocks as read-only regions.
- Only compress text outside code blocks.
- Do not merge sections around code.

## Boundaries

- Compress only markdown/natural-language OpenSpec prose.
- Never modify source, config, lockfiles, shell scripts, JSON, YAML, TOML, CSS, HTML, XML, SQL, or env files with these rules.
- If file mixes prose and code, compress only prose.
- If unsure whether text is code or prose, leave it unchanged.
- Never compress backup files such as `*.original.md`.

## Pattern

Original:

> You should always make sure to run the test suite before pushing any changes to the main branch. This is important because it helps catch bugs early and prevents broken builds from being deployed to production.

Compressed:

> Run tests before push to main. Catch bugs early, prevent broken prod deploys.

Original:

> The application uses a microservices architecture with the following components. The API gateway handles all incoming requests and routes them to the appropriate service. The authentication service is responsible for managing user sessions and JWT tokens.

Compressed:

> Microservices architecture. API gateway routes requests to services. Auth service manages user sessions + JWT tokens.
